#include "estoque/api.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <set>

#include <nlohmann/json.hpp>

#include "estoque/inventory_engine.hpp"
#include "estoque/report_engine.hpp"
#include "estoque/request_engine.hpp"
#include "estoque/retrospect_engine.hpp"
#include "estoque/time_utils.hpp"

namespace estoque {

using json = nlohmann::json;

namespace {

// Único lugar do core-cpp que toca o relógio de parede — inventory_engine
// não pode (ver o comentário do header dele), mas o reparo automático de
// saldo negativo abaixo precisa datar o ajuste que gera. Formato igual ao
// que o frontend manda em createdAt (ISO 8601 UTC).
std::string nowIso() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000Z", &tm);
  return buf;
}

json productToJson(const Product& p) {
  json j;
  j["id"] = p.id;
  j["name"] = p.name;
  j["unit"] = p.unit;
  j["minStock"] = p.minStock;
  j["category"] = p.category;
  j["qty"] = p.qty;
  j["avgCost"] = p.avgCost;
  j["createdAt"] = p.createdAt;
  j["sku"] = p.sku;
  j["imagePath"] = p.imagePath;
  j["thumbnailPath"] = p.thumbnailPath;
  return j;
}

json departmentToJson(const Department& d) {
  json j;
  j["id"] = d.id;
  j["name"] = d.name;
  j["encarregado"] = d.encarregado;
  j["monthlyLimit"] = d.monthlyLimit;
  j["createdAt"] = d.createdAt;
  return j;
}

json userToJson(const User& u) {
  json j;
  j["id"] = u.id;
  j["name"] = u.name;
  j["email"] = u.email;
  j["role"] = u.role;
  j["departmentId"] = u.departmentId;
  j["active"] = u.active;
  j["createdAt"] = u.createdAt;
  j["lastLoginAt"] = u.lastLoginAt;
  return j;
}

json permsToJson(const std::vector<FeaturePermissions>& perms) {
  json j = json::object();
  for (const auto& p : perms) {
    j[p.feature] = {{"create", p.create}, {"read", p.read}, {"update", p.update}, {"delete", p.del}};
  }
  return j;
}

json catalogToJson() {
  json arr = json::array();
  for (const auto& f : featureCatalog()) {
    arr.push_back({{"key", f.key},
                   {"label", f.label},
                   {"create", f.createLabel},
                   {"read", f.readLabel},
                   {"update", f.updateLabel},
                   {"delete", f.deleteLabel}});
  }
  return arr;
}

json groupToJson(const PermissionGroup& g) {
  json j;
  j["id"] = g.id;
  j["name"] = g.name;
  j["description"] = g.description;
  j["createdAt"] = g.createdAt;
  j["perms"] = permsToJson(g.perms);
  return j;
}

json especialidadeToJson(const Especialidade& e) {
  json j;
  j["id"] = e.id;
  j["setor"] = e.setor;
  j["setorLabel"] = setorLabel(e.setor);
  j["nome"] = e.nome;
  j["createdAt"] = e.createdAt;
  return j;
}

// Empresa com as especialidades RESOLVIDAS (setor e nome de cada uma), e não
// só os ids: as três telas do módulo mostram esses rótulos, e nenhuma delas
// deveria ter que cruzar duas listas em JavaScript para montar uma linha.
json empresaToJson(Database& db, const Empresa& e) {
  json j;
  j["id"] = e.id;
  j["nome"] = e.nome;
  j["nomeFantasia"] = e.nomeFantasia;
  j["cnpj"] = e.cnpj;
  j["endereco"] = e.endereco;
  j["numero"] = e.numero;
  j["complemento"] = e.complemento;
  j["bairro"] = e.bairro;
  j["cep"] = e.cep;
  j["cidade"] = e.cidade;
  j["estado"] = e.estado;
  j["telefone"] = e.telefone;
  j["emails"] = e.emails;
  j["observacoes"] = e.observacoes;
  j["parceira"] = e.parceira;
  j["createdAt"] = e.createdAt;

  json esp = json::array();
  for (const auto& id : e.especialidadeIds) {
    if (auto found = findEspecialidade(db, id)) esp.push_back(especialidadeToJson(*found));
  }
  j["especialidades"] = esp;
  return j;
}

// Vínculo + nome do condomínio/serviço (a tela não faz join nenhum) + o
// vencimento e o status calculados a partir de `hojeIso` — nunca gravados.
json servicoCondominioToJson(Database& db, const ServicoCondominio& s, const std::string& hojeIso) {
  auto condominio = findCondominio(db, s.condominioId);
  auto tipo = findTipoServico(db, s.tipoServicoId);

  json j;
  j["id"] = s.id;
  j["condominioId"] = s.condominioId;
  j["condominioNome"] = condominio ? condominio->nome : "";
  j["tipoServicoId"] = s.tipoServicoId;
  j["tipoServicoNome"] = tipo ? tipo->nome : "";
  j["tipoServicoCor"] = tipo ? tipo->cor : "";
  j["prazoDias"] = tipo ? tipo->prazoDias : 0;
  j["dataUltimaRenovacao"] = s.dataUltimaRenovacao;
  j["empresaContratada"] = s.empresaContratada;
  j["observacoes"] = s.observacoes;
  j["createdAt"] = s.createdAt;

  if (tipo) {
    StatusVencimento st = calcularStatus(s.dataUltimaRenovacao, tipo->prazoDias, hojeIso);
    j["dataVencimento"] = st.dataVencimento;
    j["diasRestantes"] = st.diasRestantes;
    j["status"] = st.status;
  } else {
    j["dataVencimento"] = "";
    j["diasRestantes"] = 0;
    j["status"] = vencimento_status::kOk;
  }
  return j;
}

json renovacaoToJson(Database& db, const Renovacao& r) {
  auto vinculo = findServicoCondominio(db, r.servicoCondominioId);
  auto condominio = vinculo ? findCondominio(db, vinculo->condominioId) : std::nullopt;
  auto tipo = vinculo ? findTipoServico(db, vinculo->tipoServicoId) : std::nullopt;

  json j;
  j["id"] = r.id;
  j["servicoCondominioId"] = r.servicoCondominioId;
  j["condominioNome"] = condominio ? condominio->nome : "";
  j["tipoServicoNome"] = tipo ? tipo->nome : "";
  j["dataRenovacao"] = r.dataRenovacao;
  j["empresaContratada"] = r.empresaContratada;
  j["prazoDiasAplicado"] = r.prazoDiasAplicado;
  j["observacoes"] = r.observacoes;
  j["createdAt"] = r.createdAt;
  return j;
}

json requestToJson(const Request& r) {
  json j;
  j["id"] = r.id;
  j["departmentId"] = r.departmentId;
  j["departmentName"] = r.departmentName;
  j["requesterUserId"] = r.requesterUserId;
  j["requesterName"] = r.requesterName;
  j["status"] = r.status;
  j["obs"] = r.obs;
  j["createdAt"] = r.createdAt;
  j["decidedAt"] = r.decidedAt;
  j["decidedByName"] = r.decidedByName;
  j["decisionNote"] = r.decisionNote;
  j["deliveredAt"] = r.deliveredAt;
  j["deliveredByName"] = r.deliveredByName;
  j["editedAt"] = r.editedAt;
  j["editedByName"] = r.editedByName;
  json items = json::array();
  for (const auto& it : r.items) {
    items.push_back({{"id", it.id},
                     {"productId", it.productId},
                     {"productName", it.productName},
                     {"unit", it.unit},
                     {"qty", it.qty},
                     {"movementId", it.movementId}});
  }
  j["items"] = items;
  return j;
}

json windowToJson(const RequestWindow& w) {
  json j;
  j["id"] = w.id;
  j["opensAt"] = w.opensAt;
  j["closesAt"] = w.closesAt;
  j["obs"] = w.obs;
  j["createdAt"] = w.createdAt;
  j["createdByName"] = w.createdByName;
  j["closedAt"] = w.closedAt;
  j["closedByName"] = w.closedByName;
  return j;
}

// Uma janela já terminada (por prazo ou fechamento antecipado) ainda tem
// pedido `pendente` criado dentro do próprio período? Enquanto houver, ela
// fica em "validacao" em vez de "encerrada"/"concluida" — não basta o prazo
// ter passado, alguém ainda precisa decidir esses pedidos. Não existe coluna
// ligando requisição à janela (ver Request), então o vínculo é reconstruído
// comparando createdAt contra o período efetivo da janela.
bool hasPendingInWindow(const std::vector<Request>& allRequests, const RequestWindow& w) {
  const std::string fimEfetivo = w.closedAt.empty() ? w.closesAt : w.closedAt;
  for (const auto& r : allRequests) {
    if (r.status == request_status::kPendente && r.createdAt >= w.opensAt && r.createdAt < fimEfetivo) {
      return true;
    }
  }
  return false;
}

// Rótulo da linha na tela de gestão. Deriva da MESMA comparação que
// request_engine usa para decidir se aceita um pedido — nada aqui é um
// estado gravado que pudesse divergir da trava de verdade.
const char* windowSituation(const RequestWindow& w, const std::string& agora,
                            const std::vector<Request>& allRequests) {
  if (!w.closedAt.empty()) {
    if (w.closedAt <= w.opensAt) return "cancelada";
    return hasPendingInWindow(allRequests, w) ? "validacao" : "encerrada";
  }
  if (agora < w.opensAt) return "agendada";
  if (agora < w.closesAt) return "aberta";
  return hasPendingInWindow(allRequests, w) ? "validacao" : "concluida";
}

// `key` já resolvida (evita repetir count.contains(...) ? ... em cada chamada)
std::string jstr(const json& j, const char* key, const std::string& def = "") {
  return (j.contains(key) && !j[key].is_null()) ? j[key].get<std::string>() : def;
}
double jnum(const json& j, const char* key, double def = 0.0) {
  return (j.contains(key) && !j[key].is_null()) ? j[key].get<double>() : def;
}
bool jbool(const json& j, const char* key, bool def = false) {
  if (!j.contains(key) || j[key].is_null()) return def;
  if (j[key].is_boolean()) return j[key].get<bool>();
  if (j[key].is_number()) return j[key].get<double>() != 0;
  return def;
}

std::string featureLabel(const std::string& key) {
  for (const auto& f : featureCatalog()) {
    if (f.key == key) return f.label;
  }
  return key;
}

// Usuário sintético das sessões de serviço (core-cli/testes). Superadmin por
// definição, e com um id que não colide com nenhum usuário real do banco.
User serviceUser(const std::string& label) {
  User u;
  u.id = "";
  u.name = label.empty() ? "serviço" : label;
  u.email = "";
  u.role = kRoleSuperadmin;
  u.active = true;
  return u;
}

}  // namespace

Api::Api(const std::string& dbPath) : db_(dbPath) {
  estoque::repairNegativeBalances(db_, nowIso());
  estoque::reconcileAvgCost(db_);
  estoque::backfillMissingSkus(db_);
  estoque::classifyLegacyCategories(db_);
  // Bootstrap: um banco sem nenhum superadministrador ativo é um app em que
  // ninguém consegue entrar. Idempotente e não destrutivo — ver
  // ensureDefaultSuperadmin.
  ensureDefaultSuperadmin(db_, time_utils::systemNowIso());
}

// ------------------------------------------------------------------ sessão

const User& Api::requireSession() {
  if (!currentUser_) {
    throw AuthError("[auth] sessão não iniciada — faça login para continuar");
  }
  return *currentUser_;
}

void Api::requireSuperadmin(const std::string& oQue) {
  const User& u = requireSession();
  if (!u.isSuperadmin()) {
    throw ForbiddenError("[forbidden] só o superadministrador pode " + oQue);
  }
}

void Api::require(const char* feature, PermAction action) {
  const User& u = requireSession();
  if (!userCan(db_, u, feature, action)) {
    throw ForbiddenError("[forbidden] seu perfil não tem permissão para " + permActionToString(action) +
                         " em " + featureLabel(feature));
  }
}

void Api::requireAny(const std::vector<std::pair<const char*, PermAction>>& options) {
  const User& u = requireSession();
  for (const auto& [feature, action] : options) {
    if (userCan(db_, u, feature, action)) return;
  }
  std::string alvos;
  for (const auto& [feature, action] : options) {
    if (!alvos.empty()) alvos += ", ";
    alvos += featureLabel(feature);
  }
  throw ForbiddenError("[forbidden] seu perfil não tem permissão para acessar: " + alvos);
}

std::string Api::login(const std::string& email, const std::string& password, const std::string& nowIso) {
  auto user = authenticate(db_, email, password, nowIso);
  if (!user) {
    // Mensagem única para credencial errada, usuário inexistente e conta
    // desativada — a tela de login não é lugar de descobrir quem existe.
    throw AuthError("[auth] e-mail ou senha inválidos");
  }
  currentUser_ = *user;
  serviceSession_ = false;
  return currentSessionJson();
}

void Api::logout() {
  currentUser_.reset();
  serviceSession_ = false;
}

void Api::loginAsService(const std::string& label) {
  currentUser_ = serviceUser(label);
  serviceSession_ = true;
}

std::string Api::currentSessionJson() {
  if (!currentUser_) return "null";

  json j;
  j["user"] = userToJson(*currentUser_);
  j["user"]["departmentName"] = "";
  if (!currentUser_->departmentId.empty()) {
    if (auto d = findDepartment(db_, currentUser_->departmentId)) j["user"]["departmentName"] = d->name;
  }
  j["service"] = serviceSession_;
  j["permissions"] = permsToJson(effectivePermissions(db_, *currentUser_));
  j["features"] = catalogToJson();
  return j.dump();
}

void Api::changeOwnPassword(const std::string& currentPassword, const std::string& newPassword) {
  const User& u = requireSession();
  if (u.id.empty()) throw ForbiddenError("[forbidden] a sessão de serviço não tem senha para alterar");
  if (!checkUserPassword(db_, u.id, currentPassword)) {
    throw AuthError("[auth] a senha atual não confere");
  }
  setUserPassword(db_, u.id, newPassword);
}

std::string Api::permissionCatalogJson() {
  requireSession();
  return catalogToJson().dump();
}

void Api::assertPodeEditarLogoFl() {
  requireSuperadmin("alterar a logo da FL");
}

// ---------------------------------------------------------------- produtos

// O catálogo de produtos é dado de referência de três telas (Produtos, Linha
// do Tempo e Requisições) — exigir `produtos.read` deixaria a Linha do Tempo
// sem nome de produto para quem só tem permissão nela.
std::vector<Product> Api::listProducts() {
  requireAny({{features::kProdutos, PermAction::Read},
              {features::kLinhaDoTempo, PermAction::Read},
              {features::kRelatorioMensal, PermAction::Read},
              {features::kRequisicoes, PermAction::Read},
              {features::kRequisicoes, PermAction::Create}});
  return estoque::listProducts(db_);
}

Product Api::createProduct(const Product& input) {
  require(features::kProdutos, PermAction::Create);
  return estoque::createProduct(db_, input);
}

Product Api::updateProduct(const Product& input) {
  require(features::kProdutos, PermAction::Update);
  return estoque::updateProduct(db_, input);
}

Product Api::setProductImage(const std::string& productId, const std::string& imagePath,
                             const std::string& thumbnailPath) {
  require(features::kProdutos, PermAction::Update);
  return estoque::setProductImage(db_, productId, imagePath, thumbnailPath);
}

Product Api::clearProductImage(const std::string& productId) {
  require(features::kProdutos, PermAction::Update);
  return estoque::clearProductImage(db_, productId);
}

void Api::deleteProduct(const std::string& id) {
  require(features::kProdutos, PermAction::Delete);
  estoque::deleteProduct(db_, id);
}

std::string Api::stockAvailabilityJson() {
  requireAny({{features::kProdutos, PermAction::Read},
              {features::kRequisicoes, PermAction::Read},
              {features::kRequisicoes, PermAction::Create}});
  json arr = json::array();
  for (const auto& a : estoque::stockAvailability(db_)) {
    arr.push_back({{"productId", a.productId},
                   {"sku", a.sku},
                   {"name", a.name},
                   {"unit", a.unit},
                   {"category", a.category},
                   {"qty", a.qty},
                   {"reserved", a.reserved},
                   {"available", a.available},
                   {"avgCost", a.avgCost},
                   {"minStock", a.minStock},
                   {"imagePath", a.imagePath},
                   {"thumbnailPath", a.thumbnailPath}});
  }
  return arr.dump();
}

// Como o catálogo de produtos, a lista de departamentos é dado de referência:
// a tela de Produtos precisa dela para o modal de baixa, a Linha do Tempo para
// os filtros, o Relatório para o seletor de setor.
std::vector<Department> Api::listDepartments() {
  requireAny({{features::kDepartamentos, PermAction::Read},
              {features::kProdutos, PermAction::Read},
              {features::kLinhaDoTempo, PermAction::Read},
              {features::kRelatorioMensal, PermAction::Read},
              {features::kRetrospecto, PermAction::Read},
              {features::kRequisicoes, PermAction::Read},
              {features::kRequisicoes, PermAction::Create}});
  return estoque::listDepartments(db_);
}

Department Api::createDepartment(const Department& input) {
  require(features::kDepartamentos, PermAction::Create);
  return estoque::createDepartment(db_, input);
}

Department Api::updateDepartment(const Department& input) {
  require(features::kDepartamentos, PermAction::Update);
  return estoque::updateDepartment(db_, input);
}

void Api::deleteDepartment(const std::string& id) {
  require(features::kDepartamentos, PermAction::Delete);
  estoque::deleteDepartment(db_, id);
}

Movement Api::applyEntrada(const std::string& movementId, const std::string& productId, double qty,
                           double unitPrice, const std::string& supplier, const std::string& nf,
                           const std::string& date, const std::string& obs, const std::string& createdAt) {
  // Lançar entrada/saída é a mesma ação vista de duas telas (Produtos e Linha
  // do Tempo): quem pode lançar por uma, pode pela outra.
  requireAny({{features::kProdutos, PermAction::Create}, {features::kLinhaDoTempo, PermAction::Create}});
  return estoque::applyEntrada(db_, movementId, productId, qty, unitPrice, supplier, nf, date, obs, createdAt);
}

Movement Api::applySaida(const std::string& movementId, const std::string& productId, double qty,
                         const std::string& departmentId, const std::string& date, const std::string& obs,
                         const std::string& requester, const std::string& createdAt) {
  requireAny({{features::kProdutos, PermAction::Create}, {features::kLinhaDoTempo, PermAction::Create}});
  return estoque::applySaida(db_, movementId, productId, qty, departmentId, date, obs, requester, createdAt);
}

Movement Api::applyCorrecao(const std::string& movementId, const std::string& productId, double qtyReal,
                            const std::string& motivo, const std::string& date, const std::string& createdAt,
                            double newAvgCost) {
  requireAny({{features::kProdutos, PermAction::Update}, {features::kLinhaDoTempo, PermAction::Create}});
  return estoque::applyCorrecao(db_, movementId, productId, qtyReal, motivo, date, createdAt, newAvgCost);
}

std::vector<Movement> Api::listMovements() {
  require(features::kLinhaDoTempo, PermAction::Read);
  return estoque::listMovements(db_);
}

Movement Api::updateMovement(const MovementPatch& patch) {
  require(features::kLinhaDoTempo, PermAction::Update);
  return estoque::updateMovement(db_, patch);
}

void Api::deleteMovement(const std::string& id) {
  require(features::kLinhaDoTempo, PermAction::Delete);
  estoque::deleteMovement(db_, id);
}

std::string Api::computeReportJson(int year, int month0, const std::string& deptFilter, int windowMonths,
                                   const std::string& nowIso) {
  require(features::kRelatorioMensal, PermAction::Read);
  ReportParams params{year, month0, deptFilter, windowMonths, nowIso};
  return estoque::computeReportJson(db_, params);
}

std::string Api::computeRetrospectJson(int year, const std::string& source, const std::string& nowIso) {
  require(features::kRetrospecto, PermAction::Read);
  RetrospectParams params{year, source, nowIso};
  return estoque::computeRetrospectJson(db_, params);
}

void Api::saveBudgetParams(double metaReducao, double ipca, double pisoMensal) {
  require(features::kRetrospecto, PermAction::Update);
  estoque::saveBudgetParams(db_, BudgetParams{metaReducao, ipca, pisoMensal});
}

int Api::importDeptCostHistory(const std::string& payload) {
  require(features::kImportarExportar, PermAction::Update);
  return estoque::importDeptCostHistoryJson(db_, payload);
}

// ---------------------------------------------------------------- usuários

std::string Api::listUsersJson() {
  requireSuperadmin("gerenciar usuários");
  json arr = json::array();
  for (const auto& u : estoque::listUsers(db_)) {
    json j = userToJson(u);
    j["departmentName"] = "";
    if (!u.departmentId.empty()) {
      if (auto d = findDepartment(db_, u.departmentId)) j["departmentName"] = d->name;
    }
    arr.push_back(j);
  }
  return arr.dump();
}

std::string Api::createUser(const UserInput& input) {
  requireSuperadmin("criar usuários");
  return userToJson(estoque::createUser(db_, input)).dump();
}

std::string Api::updateUser(const UserInput& input) {
  requireSuperadmin("editar usuários");
  // Um superadmin não pode se auto-rebaixar nem se desativar por engano — o
  // motor já barra o ÚLTIMO superadmin, isto barra o tiro no próprio pé
  // mesmo quando existe outro.
  const User& atual = requireSession();
  if (!atual.id.empty() && atual.id == input.id && (input.role != kRoleSuperadmin || !input.active)) {
    throw std::invalid_argument(
        "você não pode remover o próprio acesso de superadministrador — peça a outro superadmin");
  }
  User updated = estoque::updateUser(db_, input);
  if (!input.password.empty()) estoque::setUserPassword(db_, input.id, input.password);
  if (currentUser_ && currentUser_->id == updated.id) currentUser_ = updated;  // sessão reflete a edição
  return userToJson(updated).dump();
}

void Api::deleteUser(const std::string& id) {
  requireSuperadmin("excluir usuários");
  const User& atual = requireSession();
  if (!atual.id.empty() && atual.id == id) {
    throw std::invalid_argument("você não pode excluir o próprio usuário");
  }
  estoque::deleteUser(db_, id);
}

void Api::resetUserPassword(const std::string& id, const std::string& newPassword) {
  requireSuperadmin("redefinir senhas");
  estoque::setUserPassword(db_, id, newPassword);
}

// -------------------------------------------------------------- permissões

namespace {

std::vector<FeaturePermissions> permsFromJson(const json& j) {
  std::vector<FeaturePermissions> out;
  for (const auto& f : featureCatalog()) {
    FeaturePermissions p;
    p.feature = f.key;
    if (j.is_object() && j.contains(f.key) && j[f.key].is_object()) {
      const json& row = j[f.key];
      p.create = jbool(row, "create");
      p.read = jbool(row, "read");
      p.update = jbool(row, "update");
      p.del = jbool(row, "delete");
    }
    out.push_back(p);
  }
  return out;
}

}  // namespace

std::string Api::listPermissionsJson() {
  requireSuperadmin("gerenciar permissões");

  json grupos = json::array();
  for (const auto& g : estoque::listPermissionGroups(db_)) grupos.push_back(groupToJson(g));

  json departamentos = json::array();
  for (const auto& d : estoque::listDepartments(db_)) {
    json j = departmentToJson(d);
    j["permissionGroupId"] = departmentPermissionGroupId(db_, d.id);
    // Quantos usuários herdam a permissão por este setor — é a informação que
    // falta para alguém decidir com segurança se pode mexer no grupo.
    auto st = db_.prepare("SELECT COUNT(*) FROM users WHERE department_id=? AND active=1");
    st.bind(1, d.id);
    st.step();
    j["activeUsers"] = st.columnDouble(0);
    departamentos.push_back(j);
  }

  json root;
  root["groups"] = grupos;
  root["departments"] = departamentos;
  root["features"] = catalogToJson();
  return root.dump();
}

std::string Api::createPermissionGroup(const std::string& payload) {
  requireSuperadmin("criar grupos de permissão");
  json j = json::parse(payload);
  PermissionGroup g;
  g.id = jstr(j, "id");
  g.name = jstr(j, "name");
  g.description = jstr(j, "description");
  g.createdAt = jstr(j, "createdAt");
  g.perms = permsFromJson(j.contains("perms") ? j["perms"] : json::object());
  return groupToJson(estoque::createPermissionGroup(db_, g)).dump();
}

std::string Api::updatePermissionGroup(const std::string& payload) {
  requireSuperadmin("editar grupos de permissão");
  json j = json::parse(payload);
  PermissionGroup g;
  g.id = jstr(j, "id");
  g.name = jstr(j, "name");
  g.description = jstr(j, "description");
  g.perms = permsFromJson(j.contains("perms") ? j["perms"] : json::object());
  return groupToJson(estoque::updatePermissionGroup(db_, g)).dump();
}

void Api::deletePermissionGroup(const std::string& id) {
  requireSuperadmin("excluir grupos de permissão");
  estoque::deletePermissionGroup(db_, id);
}

void Api::setDepartmentPermissionGroup(const std::string& departmentId, const std::string& groupId) {
  requireSuperadmin("vincular grupos de permissão a departamentos");
  estoque::setDepartmentPermissionGroup(db_, departmentId, groupId);
}

// ------------------------------------------------------------- requisições

namespace {

// Quem valida requisições enxerga todas; quem não valida só enxerga o próprio
// setor. É essa distinção que faz "histórico do meu departamento" e "fila de
// aprovação" serem a mesma tela com dados diferentes.
bool podeValidar(Database& db, const User& u) {
  return u.isSuperadmin() || userCan(db, u, features::kRequisicoes, PermAction::Update);
}

}  // namespace

std::string Api::listRequestsJson() {
  const User& u = requireSession();
  require(features::kRequisicoes, PermAction::Read);

  std::string filtro = podeValidar(db_, u) ? "" : u.departmentId;
  // Um usuário comum sem departamento não tem "seu setor" para ver — devolver
  // tudo aqui seria vazar o histórico da empresa inteira.
  if (!podeValidar(db_, u) && filtro.empty()) return "[]";

  json arr = json::array();
  for (const auto& r : estoque::listRequests(db_, filtro)) arr.push_back(requestToJson(r));
  return arr.dump();
}

std::string Api::createRequest(const std::string& payload) {
  const User& u = requireSession();
  require(features::kRequisicoes, PermAction::Create);

  // A trava do prazo vem ANTES de qualquer validação de item: quem chega
  // fora da janela precisa ouvir que o período fechou, não que faltou saldo
  // de um material que ele nem poderia pedir agora.
  //
  // O instante conferido é o do RELÓGIO DO SISTEMA, nunca o `createdAt` do
  // payload — deixar o próprio pedido dizer que horas são anularia o prazo.
  //
  // Exceção: quem já pode VALIDAR requisições (aprovar/rejeitar/entregar)
  // também pode lançar um pedido com a janela fechada — é o caso de alguém
  // que esqueceu de pedir no prazo e o administrador registra por ele. Um
  // solicitante comum continua barrado fora da janela.
  if (!podeValidar(db_, u)) {
    estoque::requireOpenRequestWindow(db_, time_utils::systemNowIso());
  }

  json j = json::parse(payload);
  RequestInput input;
  input.id = jstr(j, "id");
  input.createdAt = jstr(j, "createdAt");
  input.obs = jstr(j, "obs");
  input.requesterUserId = u.id;
  input.requesterName = u.name;

  // Só o superadmin escolhe o setor: um usuário comum requisita SEMPRE para o
  // departamento a que está atrelado, venha o que vier no payload.
  input.departmentId = u.departmentId;
  if (u.isSuperadmin() && !jstr(j, "departmentId").empty()) input.departmentId = jstr(j, "departmentId");
  if (input.departmentId.empty()) {
    throw std::invalid_argument(
        "seu usuário não está atrelado a nenhum departamento — peça ao administrador para vincular");
  }

  if (j.contains("items")) {
    for (const auto& raw : j["items"]) {
      RequestItem it;
      it.id = jstr(raw, "id");
      it.productId = jstr(raw, "productId");
      it.qty = jnum(raw, "qty");
      input.items.push_back(std::move(it));
    }
  }
  return requestToJson(estoque::createRequest(db_, input)).dump();
}

std::string Api::updateRequestItems(const std::string& id, const std::string& payload,
                                    const std::string& nowIso) {
  const User& u = requireSession();
  // Mesma trava de "quem valida" das decisões: editar em nome de outro
  // departamento é atribuição de quem aprova, não do próprio solicitante —
  // senão qualquer um poderia inflar o próprio pedido depois de enviado.
  if (!podeValidar(db_, u)) {
    throw ForbiddenError("[forbidden] seu perfil não pode editar os itens de uma requisição");
  }

  json j = json::parse(payload);
  std::vector<RequestItem> items;
  if (j.contains("items")) {
    for (const auto& raw : j["items"]) {
      RequestItem it;
      it.id = jstr(raw, "id");
      it.productId = jstr(raw, "productId");
      it.qty = jnum(raw, "qty");
      items.push_back(std::move(it));
    }
  }
  return requestToJson(estoque::updateRequestItems(db_, id, items, u, nowIso)).dump();
}

std::string Api::approveRequest(const std::string& id, const std::string& note, const std::string& nowIso) {
  const User& u = requireSession();
  if (!podeValidar(db_, u)) {
    throw ForbiddenError("[forbidden] seu perfil não pode aprovar requisições");
  }
  return requestToJson(estoque::approveRequest(db_, id, u, note, nowIso)).dump();
}

std::string Api::rejectRequest(const std::string& id, const std::string& note, const std::string& nowIso) {
  const User& u = requireSession();
  if (!podeValidar(db_, u)) {
    throw ForbiddenError("[forbidden] seu perfil não pode rejeitar requisições");
  }
  return requestToJson(estoque::rejectRequest(db_, id, u, note, nowIso)).dump();
}

std::string Api::cancelRequest(const std::string& id, const std::string& note, const std::string& nowIso) {
  const User& u = requireSession();
  auto existing = estoque::findRequest(db_, id);
  if (!existing) throw NotFoundError("requisição não encontrada: " + id);

  if (!podeValidar(db_, u)) {
    require(features::kRequisicoes, PermAction::Delete);
    if (existing->departmentId != u.departmentId) {
      throw ForbiddenError("[forbidden] esta requisição é de outro departamento");
    }
  }
  return requestToJson(estoque::cancelRequest(db_, id, u, note, nowIso)).dump();
}

std::string Api::deliverRequest(const std::string& id, const std::string& nowIso,
                                const std::string& movementIdPrefix) {
  const User& u = requireSession();
  if (!podeValidar(db_, u)) {
    throw ForbiddenError("[forbidden] seu perfil não pode confirmar a entrega de requisições");
  }
  return requestToJson(estoque::deliverRequest(db_, id, u, nowIso, movementIdPrefix)).dump();
}

// ----------------------------------------------- janela de requisições

std::string Api::requestWindowStatusJson() {
  // Sem exigir permissão de leitura de requisições: o status é o que a tela
  // usa para explicar por que o botão de pedir está indisponível, e essa
  // explicação não vaza dado nenhum de pedido.
  const User& u = requireSession();
  const std::string agora = time_utils::systemNowIso();

  json j;
  j["now"] = agora;
  j["canManage"] = podeValidar(db_, u);

  auto aberta = estoque::openRequestWindowAt(db_, agora);
  j["open"] = aberta.has_value();
  j["current"] = aberta ? windowToJson(*aberta) : json(nullptr);

  auto proxima = estoque::nextRequestWindowAfter(db_, agora);
  j["next"] = proxima ? windowToJson(*proxima) : json(nullptr);
  return j.dump();
}

std::string Api::listRequestWindowsJson() {
  const User& u = requireSession();
  if (!podeValidar(db_, u)) {
    throw ForbiddenError("[forbidden] seu perfil não pode gerenciar as janelas de requisição");
  }
  const std::string agora = time_utils::systemNowIso();
  const auto allRequests = estoque::listRequests(db_, "");
  json arr = json::array();
  for (const auto& w : estoque::listRequestWindows(db_)) {
    json jw = windowToJson(w);
    jw["situacao"] = windowSituation(w, agora, allRequests);
    arr.push_back(jw);
  }
  return arr.dump();
}

std::string Api::createRequestWindow(const std::string& payload) {
  const User& u = requireSession();
  if (!podeValidar(db_, u)) {
    throw ForbiddenError("[forbidden] seu perfil não pode abrir janelas de requisição");
  }

  json j = json::parse(payload);
  RequestWindow w;
  w.id = jstr(j, "id");
  w.opensAt = jstr(j, "opensAt");
  w.closesAt = jstr(j, "closesAt");
  w.obs = jstr(j, "obs");
  w.createdAt = time_utils::systemNowIso();
  w.createdByUserId = u.id;
  w.createdByName = u.name;
  return windowToJson(estoque::createRequestWindow(db_, w, w.createdAt)).dump();
}

std::string Api::closeRequestWindowNow(const std::string& id) {
  const User& u = requireSession();
  if (!podeValidar(db_, u)) {
    throw ForbiddenError("[forbidden] seu perfil não pode encerrar janelas de requisição");
  }
  return windowToJson(estoque::closeRequestWindowNow(db_, id, u, time_utils::systemNowIso())).dump();
}

void Api::deleteRequestWindow(const std::string& id) {
  const User& u = requireSession();
  if (!podeValidar(db_, u)) {
    throw ForbiddenError("[forbidden] seu perfil não pode excluir janelas de requisição");
  }
  estoque::deleteRequestWindow(db_, id, time_utils::systemNowIso());
}

// ------------------------------------------------------ gestão de prazos

std::vector<Condominio> Api::listCondominios() {
  requireAny({{features::kCondominios, PermAction::Read},
              {features::kGestaoDatas, PermAction::Read},
              {features::kGestaoDatas, PermAction::Create},
              {features::kGestaoDatas, PermAction::Update},
              {features::kGerentes, PermAction::Read}});
  return estoque::listCondominios(db_);
}

Condominio Api::createCondominio(const Condominio& input) {
  require(features::kCondominios, PermAction::Create);
  return estoque::createCondominio(db_, input);
}

Condominio Api::updateCondominio(const Condominio& input) {
  require(features::kCondominios, PermAction::Update);
  return estoque::updateCondominio(db_, input);
}

void Api::deleteCondominio(const std::string& id) {
  require(features::kCondominios, PermAction::Delete);
  estoque::deleteCondominio(db_, id);
}

std::vector<TipoServico> Api::listTiposServico() {
  requireAny({{features::kGestaoDatas, PermAction::Read},
              {features::kGestaoDatas, PermAction::Create},
              {features::kGestaoDatas, PermAction::Update}});
  return estoque::listTiposServico(db_);
}

TipoServico Api::createTipoServico(const TipoServico& input) {
  require(features::kGestaoDatas, PermAction::Create);
  return estoque::createTipoServico(db_, input);
}

TipoServico Api::updateTipoServico(const TipoServico& input) {
  require(features::kGestaoDatas, PermAction::Update);
  return estoque::updateTipoServico(db_, input);
}

void Api::deleteTipoServico(const std::string& id) {
  require(features::kGestaoDatas, PermAction::Delete);
  estoque::deleteTipoServico(db_, id);
}

std::string Api::listServicosCondominioJson(const std::string& nowIso) {
  require(features::kGestaoDatas, PermAction::Read);
  json arr = json::array();
  for (const auto& s : estoque::listServicosCondominio(db_)) {
    arr.push_back(servicoCondominioToJson(db_, s, nowIso));
  }
  return arr.dump();
}

std::string Api::createServicoCondominio(const std::string& payload) {
  require(features::kGestaoDatas, PermAction::Create);
  json j = json::parse(payload);
  ServicoCondominio s;
  s.id = jstr(j, "id");
  s.condominioId = jstr(j, "condominioId");
  s.tipoServicoId = jstr(j, "tipoServicoId");
  s.dataUltimaRenovacao = jstr(j, "dataUltimaRenovacao");
  s.empresaContratada = jstr(j, "empresaContratada");
  s.observacoes = jstr(j, "observacoes");
  s.createdAt = jstr(j, "createdAt");
  auto created = estoque::createServicoCondominio(db_, s);
  return servicoCondominioToJson(db_, created, jstr(j, "nowIso", created.createdAt)).dump();
}

std::string Api::updateServicoCondominio(const std::string& payload) {
  require(features::kGestaoDatas, PermAction::Update);
  json j = json::parse(payload);
  ServicoCondominio s;
  s.id = jstr(j, "id");
  s.condominioId = jstr(j, "condominioId");
  s.tipoServicoId = jstr(j, "tipoServicoId");
  s.dataUltimaRenovacao = jstr(j, "dataUltimaRenovacao");
  s.empresaContratada = jstr(j, "empresaContratada");
  s.observacoes = jstr(j, "observacoes");
  auto updated = estoque::updateServicoCondominio(db_, s);
  return servicoCondominioToJson(db_, updated, jstr(j, "nowIso", updated.createdAt)).dump();
}

void Api::deleteServicoCondominio(const std::string& id) {
  require(features::kGestaoDatas, PermAction::Delete);
  estoque::deleteServicoCondominio(db_, id);
}

std::string Api::renovarServico(const std::string& payload) {
  require(features::kGestaoDatas, PermAction::Update);
  json j = json::parse(payload);
  auto updated = estoque::renovarServico(db_, jstr(j, "servicoCondominioId"), jstr(j, "id"),
                                         jstr(j, "dataRenovacao"), jstr(j, "empresaContratada"),
                                         jstr(j, "observacoes"), jstr(j, "createdAt"));
  return servicoCondominioToJson(db_, updated, jstr(j, "nowIso", jstr(j, "dataRenovacao"))).dump();
}

std::string Api::listRenovacoesJson(const std::string& servicoCondominioFilter) {
  require(features::kGestaoDatas, PermAction::Read);
  json arr = json::array();
  for (const auto& r : estoque::listRenovacoes(db_, servicoCondominioFilter)) {
    arr.push_back(renovacaoToJson(db_, r));
  }
  return arr.dump();
}

// ------------------------------- fornecedores e prestadores de serviços

std::string Api::listSetorizacaoJson() {
  require(features::kEmpresas, PermAction::Read);

  auto todas = estoque::listEspecialidades(db_);
  json setoresJson = json::array();
  for (const auto& s : setorCatalog()) {
    json especialidades = json::array();
    for (const auto& e : todas) {
      if (e.setor != s.key) continue;
      json je = especialidadeToJson(e);
      // Quantas empresas marcam esta especialidade: é o que a tela precisa
      // para avisar antes de excluir, e o que dá utilidade ao Catálogo.
      je["empresas"] = estoque::empresasComEspecialidade(db_, e.id);
      especialidades.push_back(je);
    }
    setoresJson.push_back({{"key", s.key}, {"label", s.label}, {"especialidades", especialidades}});
  }

  json root;
  root["setores"] = setoresJson;
  return root.dump();
}

std::string Api::createEspecialidade(const std::string& payload) {
  require(features::kEmpresas, PermAction::Create);
  json j = json::parse(payload);
  Especialidade e;
  e.id = jstr(j, "id");
  e.setor = jstr(j, "setor");
  e.nome = jstr(j, "nome");
  e.createdAt = jstr(j, "createdAt");
  return especialidadeToJson(estoque::createEspecialidade(db_, e)).dump();
}

std::string Api::updateEspecialidade(const std::string& payload) {
  require(features::kEmpresas, PermAction::Update);
  json j = json::parse(payload);
  Especialidade e;
  e.id = jstr(j, "id");
  e.setor = jstr(j, "setor");
  e.nome = jstr(j, "nome");
  return especialidadeToJson(estoque::updateEspecialidade(db_, e)).dump();
}

void Api::deleteEspecialidade(const std::string& id) {
  require(features::kEmpresas, PermAction::Delete);
  estoque::deleteEspecialidade(db_, id);
}

namespace {

Empresa empresaFromJson(const json& j) {
  Empresa e;
  e.id = jstr(j, "id");
  e.nome = jstr(j, "nome");
  e.nomeFantasia = jstr(j, "nomeFantasia");
  e.cnpj = jstr(j, "cnpj");
  e.endereco = jstr(j, "endereco");
  e.numero = jstr(j, "numero");
  e.complemento = jstr(j, "complemento");
  e.bairro = jstr(j, "bairro");
  e.cep = jstr(j, "cep");
  e.cidade = jstr(j, "cidade");
  e.estado = jstr(j, "estado");
  e.telefone = jstr(j, "telefone");
  e.emails = jstr(j, "emails");
  e.observacoes = jstr(j, "observacoes");
  e.parceira = jbool(j, "parceira");
  e.createdAt = jstr(j, "createdAt");
  if (j.contains("especialidadeIds") && j["especialidadeIds"].is_array()) {
    for (const auto& id : j["especialidadeIds"]) {
      if (id.is_string()) e.especialidadeIds.push_back(id.get<std::string>());
    }
  }
  return e;
}

}  // namespace

std::string Api::listEmpresasJson() {
  require(features::kEmpresas, PermAction::Read);
  json arr = json::array();
  for (const auto& e : estoque::listEmpresas(db_)) arr.push_back(empresaToJson(db_, e));
  return arr.dump();
}

std::string Api::listParceirosJson() {
  require(features::kEmpresas, PermAction::Read);
  json arr = json::array();
  for (const auto& e : estoque::listParceiros(db_)) arr.push_back(empresaToJson(db_, e));
  return arr.dump();
}

std::string Api::createEmpresa(const std::string& payload) {
  require(features::kEmpresas, PermAction::Create);
  auto created = estoque::createEmpresa(db_, empresaFromJson(json::parse(payload)));
  return empresaToJson(db_, created).dump();
}

std::string Api::updateEmpresa(const std::string& payload) {
  require(features::kEmpresas, PermAction::Update);
  auto updated = estoque::updateEmpresa(db_, empresaFromJson(json::parse(payload)));
  return empresaToJson(db_, updated).dump();
}

void Api::deleteEmpresa(const std::string& id) {
  require(features::kEmpresas, PermAction::Delete);
  estoque::deleteEmpresa(db_, id);
}

// ------------------------------------------ gestão sos: gerentes e carteiras

namespace {

// Gerente com a carteira RESOLVIDA (id + nome de cada condomínio), e não só
// os ids: tanto a tela de Gerentes quanto a de Carteiras mostram o nome sem
// precisar cruzar com a lista de condomínios em JavaScript.
json gerenteToJson(Database& db, const Gerente& g) {
  json j;
  j["id"] = g.id;
  j["numero"] = g.numero;
  j["nome"] = g.nome;
  j["telefone"] = g.telefone;
  j["email"] = g.email;
  j["chavePix"] = g.chavePix;
  j["observacoes"] = g.observacoes;
  j["createdAt"] = g.createdAt;

  json conds = json::array();
  for (const auto& id : g.condominioIds) {
    if (auto c = findCondominio(db, id)) conds.push_back({{"id", c->id}, {"nome", c->nome}});
  }
  j["condominios"] = conds;
  return j;
}

Gerente gerenteFromJson(const json& j) {
  Gerente g;
  g.id = jstr(j, "id");
  g.nome = jstr(j, "nome");
  g.telefone = jstr(j, "telefone");
  g.email = jstr(j, "email");
  g.chavePix = jstr(j, "chavePix");
  g.observacoes = jstr(j, "observacoes");
  g.createdAt = jstr(j, "createdAt");
  if (j.contains("condominioIds") && j["condominioIds"].is_array()) {
    for (const auto& id : j["condominioIds"]) {
      if (id.is_string()) g.condominioIds.push_back(id.get<std::string>());
    }
  }
  return g;
}

}  // namespace

std::string Api::listGerentesJson() {
  require(features::kGerentes, PermAction::Read);
  json arr = json::array();
  for (const auto& g : estoque::listGerentes(db_)) arr.push_back(gerenteToJson(db_, g));
  return arr.dump();
}

std::string Api::createGerente(const std::string& payload) {
  require(features::kGerentes, PermAction::Create);
  auto created = estoque::createGerente(db_, gerenteFromJson(json::parse(payload)));
  return gerenteToJson(db_, created).dump();
}

std::string Api::updateGerente(const std::string& payload) {
  require(features::kGerentes, PermAction::Update);
  auto updated = estoque::updateGerente(db_, gerenteFromJson(json::parse(payload)));
  return gerenteToJson(db_, updated).dump();
}

void Api::deleteGerente(const std::string& id) {
  require(features::kGerentes, PermAction::Delete);
  estoque::deleteGerente(db_, id);
}

// ------------------------------------------ gestão sos: delta síndicos

namespace {

// Comissão vai no JSON, mas é sempre recalculada — não existe mais
// deltaSindicoFromJson porque não existe mais create/update aqui (puxado de
// Serviços, ver listDeltaSindicos em commissions_engine.cpp).
json deltaSindicoToJson(const DeltaSindico& d) {
  json j;
  j["id"] = d.id;
  j["numero"] = d.numero;
  j["condominioId"] = d.condominioId;
  j["condominioNome"] = d.condominioNome;
  j["gerenteId"] = d.gerenteId;
  j["gerenteNome"] = d.gerenteNome;
  j["sindico"] = d.sindico;
  j["venda"] = d.venda;
  j["porcentagem"] = d.porcentagem;
  j["comissao"] = comissaoDeltaDe(d);
  j["dataReferencia"] = d.dataReferencia;
  j["observacoes"] = d.observacoes;
  j["createdAt"] = d.createdAt;
  return j;
}

}  // namespace

std::string Api::listDeltaSindicosJson() {
  require(features::kDeltaSindicos, PermAction::Read);
  json arr = json::array();
  for (const auto& d : estoque::listDeltaSindicos(db_)) arr.push_back(deltaSindicoToJson(d));
  return arr.dump();
}

// ------------------------------- gestão sos: dashboard de fechamento

namespace {

json linhaValorToJson(const DashboardLinhaValor& l) {
  return {{"rotulo", l.rotulo}, {"valor", l.valor}};
}

std::vector<DashboardLinhaValor> linhasValorFromJson(const json& j, const char* chave) {
  std::vector<DashboardLinhaValor> out;
  if (!j.contains(chave) || !j[chave].is_array()) return out;
  for (const auto& item : j[chave]) {
    DashboardLinhaValor l;
    l.rotulo = jstr(item, "rotulo");
    l.valor = jnum(item, "valor");
    out.push_back(l);
  }
  return out;
}

DashboardEntrada dashboardEntradaFromJson(const json& j) {
  DashboardEntrada e;
  e.mesReferencia = jstr(j, "mesReferencia");
  e.flLucro = jnum(j, "flLucro");
  e.percentualComissao = jnum(j, "percentualComissao");
  e.percentualDistribuido = jnum(j, "percentualDistribuido");
  e.retido = jnum(j, "retido");
  e.distribuicaoCompras = linhasValorFromJson(j, "distribuicaoCompras");
  e.distribuicaoDelta = linhasValorFromJson(j, "distribuicaoDelta");
  e.observacoes = jstr(j, "observacoes");
  if (j.contains("gerentes") && j["gerentes"].is_array()) {
    for (const auto& g : j["gerentes"]) {
      DashboardGerenteEntrada ge;
      ge.gerenteId = jstr(g, "gerenteId");
      ge.porcentagem = jnum(g, "porcentagem");
      ge.eficacia = jnum(g, "eficacia");
      ge.carteira = static_cast<int>(jnum(g, "carteira"));
      e.gerentes.push_back(ge);
    }
  }
  return e;
}

json dashboardToJson(const DashboardFechamento& d) {
  json j;
  j["mesReferencia"] = d.mesReferencia;
  j["arrecadado"] = d.arrecadado;
  j["flLucro"] = d.flLucro;
  j["liberadoParaComissao"] = d.liberadoParaComissao;
  j["percentualComissao"] = d.percentualComissao;
  j["percentualDistribuido"] = d.percentualDistribuido;
  j["gerenciaLiquido"] = d.gerenciaLiquido;
  j["retido"] = d.retido;
  j["observacoes"] = d.observacoes;

  json compras = json::array();
  for (const auto& l : d.distribuicaoCompras) compras.push_back(linhaValorToJson(l));
  j["distribuicaoCompras"] = compras;

  json delta = json::array();
  for (const auto& l : d.distribuicaoDelta) delta.push_back(linhaValorToJson(l));
  j["distribuicaoDelta"] = delta;

  json gerentes = json::array();
  for (const auto& g : d.gerentes) {
    gerentes.push_back({{"gerenteId", g.gerenteId}, {"gerenteNome", g.gerenteNome},
                        {"recebido", g.recebido},
                        {"carteira", g.carteira}, {"meta", g.meta},
                        {"porcentagem", g.porcentagem}, {"eficacia", g.eficacia},
                        {"descontos", g.descontos}, {"comissao", g.comissao},
                        {"retido", g.retido}});
  }
  j["gerentes"] = gerentes;

  json deltas = json::array();
  for (const auto& x : d.deltaSindicos) deltas.push_back(deltaSindicoToJson(x));
  j["deltaSindicos"] = deltas;

  json empresas = json::array();
  for (const auto& e : d.empresas) {
    empresas.push_back({{"empresaId", e.empresaId}, {"empresaNome", e.empresaNome},
                        {"recebidos", e.recebidos}});
  }
  j["empresas"] = empresas;
  return j;
}

json dashboardSalvoToJson(const DashboardSalvo& d) {
  json j;
  j["id"] = d.id;
  j["mesReferencia"] = d.mesReferencia;
  // O retrato volta como OBJETO (e não string): quem lê o histórico exibe o
  // dashboard direto, sem ter que reparsear no JavaScript.
  j["dados"] = json::parse(d.dadosJson, nullptr, false);
  j["observacoes"] = d.observacoes;
  j["geradoEm"] = d.geradoEm;
  j["createdAt"] = d.createdAt;
  return j;
}

}  // namespace

std::string Api::montarDashboard(const std::string& payload) {
  require(features::kGestaoSosServicos, PermAction::Read);
  auto dash = estoque::montarDashboard(db_, dashboardEntradaFromJson(json::parse(payload)));
  return dashboardToJson(dash).dump();
}

std::string Api::salvarDashboard(const std::string& payload) {
  require(features::kGestaoSosServicos, PermAction::Update);
  json j = json::parse(payload);
  DashboardSalvo snap;
  snap.id = jstr(j, "id");
  snap.mesReferencia = jstr(j, "mesReferencia");
  // `dados` chega como objeto e é serializado aqui — é o retrato imutável.
  snap.dadosJson = j.contains("dados") ? j["dados"].dump() : "";
  snap.observacoes = jstr(j, "observacoes");
  snap.geradoEm = jstr(j, "geradoEm", time_utils::systemNowIso());
  snap.createdAt = jstr(j, "createdAt", time_utils::systemNowIso());
  return dashboardSalvoToJson(estoque::salvarDashboard(db_, snap)).dump();
}

std::string Api::listDashboardsJson() {
  require(features::kGestaoSosServicos, PermAction::Read);
  json arr = json::array();
  for (const auto& d : estoque::listDashboards(db_)) arr.push_back(dashboardSalvoToJson(d));
  return arr.dump();
}

// ------------------------------------------------- gestão sos: pagamentos

namespace {

json pagamentoSalvoToJson(const PagamentoSalvo& p) {
  json j;
  j["id"] = p.id;
  j["mesReferencia"] = p.mesReferencia;
  // Mesmo critério de dashboardSalvoToJson: volta como OBJETO, não string.
  j["dados"] = p.dadosJson.empty() ? json::object() : json::parse(p.dadosJson, nullptr, false);
  j["observacoes"] = p.observacoes;
  j["fechado"] = p.fechado;
  j["geradoEm"] = p.geradoEm;
  j["fechadoEm"] = p.fechadoEm;
  j["createdAt"] = p.createdAt;
  return j;
}

}  // namespace

std::string Api::montarPagamentoSos(const std::string& payload) {
  require(features::kGestaoSosServicos, PermAction::Read);
  json j = json::parse(payload);
  std::string mes = jstr(j, "mesReferencia");

  // Mês já tem pagamento salvo: devolve ELE (reabre pra editar/reautorizar),
  // nunca uma proposta nova por cima — um mês só tem um registro (ver
  // PagamentoSalvo em commissions_engine.hpp).
  auto existente = estoque::findPagamentoSosPorMes(db_, mes);
  if (existente) return pagamentoSalvoToJson(*existente).dump();

  // Sem pagamento salvo: monta a PROPOSTA a partir do Dashboard de
  // Fechamento já salvo daquele mês — nunca recalcula a comissão do zero,
  // só decide quem recebe do total já aprovado ali.
  std::optional<DashboardSalvo> dashSalvo;
  for (const auto& d : estoque::listDashboards(db_)) {
    if (d.mesReferencia == mes) { dashSalvo = d; break; }  // já vem mais recente primeiro
  }
  if (!dashSalvo) {
    throw std::invalid_argument(
        "Nenhum Dashboard de Fechamento salvo para este mês — feche o mês em "
        "Dashboard de Fechamento antes de programar o pagamento.");
  }
  json dj = json::parse(dashSalvo->dadosJson, nullptr, false);
  if (!dj.is_object()) throw std::invalid_argument("o dashboard salvo deste mês está corrompido");

  double arrecadado = jnum(dj, "arrecadado");
  double totalGerentes = jnum(dj, "gerenciaLiquido");
  double totalDelta = 0;
  json linhas = json::array();

  if (dj.contains("gerentes") && dj["gerentes"].is_array()) {
    for (auto& g : dj["gerentes"]) {
      double comissao = jnum(g, "comissao");
      totalDelta += jnum(g, "descontos");
      std::string gid = jstr(g, "gerenteId");
      std::string chavePix;
      auto ger = estoque::findGerente(db_, gid);
      if (ger) chavePix = ger->chavePix;
      linhas.push_back({{"tipo", "gerente"}, {"pessoaId", gid}, {"nome", jstr(g, "gerenteNome")},
                        {"chavePix", chavePix}, {"valor", comissao}, {"autorizado", true}});
    }
  }

  // Suprimentos: o rateio de Configurações fala em "Encarregado"/"Assistente"
  // (nomes históricos do acordo), a Suprimento::categoria fala em
  // Gestor/Assistente/Auxiliar/Vistoriador Predial — Encarregado mapeia pra
  // Gestor (o papel de chefia). Categoria sem NINGUÉM cadastrado hoje não
  // vira linha nenhuma (não faz sentido propor pagamento pra ninguém); com
  // mais de uma pessoa na mesma categoria, a fatia se divide em partes
  // iguais entre elas.
  double totalSuprimentos = 0;
  if (dj.contains("distribuicaoCompras") && dj["distribuicaoCompras"].is_array()) {
    auto suprimentos = estoque::listSuprimentos(db_);
    for (auto& l : dj["distribuicaoCompras"]) {
      std::string rotulo = jstr(l, "rotulo");
      double valor = jnum(l, "valor");
      totalSuprimentos += valor;
      std::string categoria;
      if (rotulo == "Encarregado") categoria = suprimento_categoria::kGestor;
      else if (rotulo == "Assistente") categoria = suprimento_categoria::kAssistente;
      if (categoria.empty()) continue;
      std::vector<Suprimento> pessoas;
      for (auto& s : suprimentos) {
        if (s.categoria == categoria) pessoas.push_back(s);
      }
      if (pessoas.empty()) continue;
      double cada = valor / static_cast<double>(pessoas.size());
      for (auto& s : pessoas) {
        linhas.push_back({{"tipo", "suprimento"}, {"pessoaId", s.id}, {"nome", s.nome},
                          {"chavePix", s.chavePix}, {"valor", cada}, {"autorizado", true}});
      }
    }
  }

  // Delta: uma entidade só (não existe cadastro de síndico com PIX próprio —
  // ver comentário de sos_config::kDeltaChavePix), recebendo o total dos
  // descontos de Delta Síndicos do mês.
  std::string deltaTitular = estoque::getConfig(db_, sos_config::kDeltaTitular, "");
  linhas.push_back({{"tipo", "delta"}, {"pessoaId", ""},
                    {"nome", deltaTitular.empty() ? "Delta" : deltaTitular},
                    {"chavePix", estoque::getConfig(db_, sos_config::kDeltaChavePix, "")},
                    {"valor", totalDelta}, {"autorizado", true}});

  json dados;
  dados["arrecadado"] = arrecadado;
  dados["totalGerentes"] = totalGerentes;
  dados["totalSuprimentos"] = totalSuprimentos;
  dados["totalDelta"] = totalDelta;
  dados["linhas"] = linhas;

  json out;
  out["id"] = "";
  out["mesReferencia"] = mes;
  out["dados"] = dados;
  out["observacoes"] = "";
  out["fechado"] = false;
  out["geradoEm"] = "";
  out["fechadoEm"] = "";
  out["createdAt"] = "";
  return out.dump();
}

std::string Api::salvarPagamentoSos(const std::string& payload) {
  require(features::kGestaoSosServicos, PermAction::Update);
  json j = json::parse(payload);
  PagamentoSalvo p;
  p.id = jstr(j, "id");
  if (p.id.empty()) throw std::invalid_argument("id do pagamento é obrigatório");
  p.mesReferencia = jstr(j, "mesReferencia");
  // `dados` chega como objeto e é serializado aqui — mesmo critério de
  // Api::salvarDashboard.
  p.dadosJson = j.contains("dados") ? j["dados"].dump() : "";
  p.observacoes = jstr(j, "observacoes");
  p.fechado = jbool(j, "fechado", false);
  p.geradoEm = jstr(j, "geradoEm", time_utils::systemNowIso());
  // fechadoEm marca quando foi fechado PELA PRIMEIRA VEZ — uma edição
  // posterior manda o mesmo valor de volta (preservado), então só cai no
  // "agora" quando ainda chega vazio (o caso do primeiro fechar, cuja
  // proposta em montarPagamentoSos sempre traz fechadoEm vazio).
  p.fechadoEm = jstr(j, "fechadoEm", "");
  if (p.fechado && p.fechadoEm.empty()) p.fechadoEm = time_utils::systemNowIso();
  if (!p.fechado) p.fechadoEm = "";
  p.createdAt = jstr(j, "createdAt", time_utils::systemNowIso());
  return pagamentoSalvoToJson(estoque::salvarPagamentoSos(db_, p)).dump();
}

std::string Api::listPagamentosSosJson() {
  require(features::kGestaoSosServicos, PermAction::Read);
  json arr = json::array();
  for (const auto& p : estoque::listPagamentosSos(db_)) arr.push_back(pagamentoSalvoToJson(p));
  return arr.dump();
}

// --------------------------------------------- gestão sos: suprimentos

namespace {

json suprimentoToJson(const Suprimento& s) {
  json j;
  j["id"] = s.id;
  j["numero"] = s.numero;
  j["nome"] = s.nome;
  j["categoria"] = s.categoria;
  j["categoriaLabel"] = categoriaSuprimentoLabel(s.categoria);
  j["telefone"] = s.telefone;
  j["email"] = s.email;
  j["chavePix"] = s.chavePix;
  j["observacoes"] = s.observacoes;
  j["createdAt"] = s.createdAt;
  return j;
}

Suprimento suprimentoFromJson(const json& j) {
  Suprimento s;
  s.id = jstr(j, "id");
  s.nome = jstr(j, "nome");
  s.categoria = jstr(j, "categoria");
  s.telefone = jstr(j, "telefone");
  s.email = jstr(j, "email");
  s.chavePix = jstr(j, "chavePix");
  s.observacoes = jstr(j, "observacoes");
  s.createdAt = jstr(j, "createdAt");
  return s;
}

}  // namespace

std::string Api::listSuprimentosJson() {
  require(features::kSuprimentos, PermAction::Read);
  json arr = json::array();
  for (const auto& s : estoque::listSuprimentos(db_)) arr.push_back(suprimentoToJson(s));
  return arr.dump();
}

std::string Api::createSuprimento(const std::string& payload) {
  require(features::kSuprimentos, PermAction::Create);
  auto created = estoque::createSuprimento(db_, suprimentoFromJson(json::parse(payload)));
  return suprimentoToJson(created).dump();
}

std::string Api::updateSuprimento(const std::string& payload) {
  require(features::kSuprimentos, PermAction::Update);
  auto updated = estoque::updateSuprimento(db_, suprimentoFromJson(json::parse(payload)));
  return suprimentoToJson(updated).dump();
}

void Api::deleteSuprimento(const std::string& id) {
  require(features::kSuprimentos, PermAction::Delete);
  estoque::deleteSuprimento(db_, id);
}

// --------------------------------- gestão sos: serviços e fechamentos

namespace {

// Comissão vai no JSON (venda × porcentagem ÷ 100), mas nunca é lida de
// volta em servicoFromJson — é sempre recalculada, nunca aceita do cliente.
json servicoToJson(const Servico& s) {
  json j;
  j["id"] = s.id;
  j["numero"] = s.numero;
  j["codigo"] = s.codigo;
  j["condominioId"] = s.condominioId;
  j["condominioNome"] = s.condominioNome;
  j["gerenteId"] = s.gerenteId;
  j["gerenteNome"] = s.gerenteNome;
  j["parceiroId"] = s.parceiroId;
  j["parceiroNome"] = s.parceiroNome;
  j["venda"] = s.venda;
  j["porcentagem"] = s.porcentagem;
  j["comissao"] = comissaoDe(s);
  j["dataReferencia"] = s.dataReferencia;
  j["fechamentoId"] = s.fechamentoId;
  j["fechado"] = !s.fechamentoId.empty();
  j["observacoes"] = s.observacoes;
  j["createdAt"] = s.createdAt;
  j["pago"] = s.pago;
  j["dataPagamento"] = s.dataPagamento;
  return j;
}

Servico servicoFromJson(const json& j) {
  Servico s;
  s.id = jstr(j, "id");
  s.codigo = jstr(j, "codigo");
  s.condominioId = jstr(j, "condominioId");
  s.gerenteId = jstr(j, "gerenteId");
  s.parceiroId = jstr(j, "parceiroId");
  s.venda = jnum(j, "venda");
  s.porcentagem = jnum(j, "porcentagem");
  s.dataReferencia = jstr(j, "dataReferencia");
  s.observacoes = jstr(j, "observacoes");
  s.createdAt = jstr(j, "createdAt");
  s.pago = j.contains("pago") && j["pago"].is_boolean() && j["pago"].get<bool>();
  s.dataPagamento = jstr(j, "dataPagamento");
  return s;
}

json fechamentoToJson(const Fechamento& f) {
  json j;
  j["id"] = f.id;
  j["mesReferencia"] = f.mesReferencia;
  j["quantidadeServicos"] = f.quantidadeServicos;
  j["totalVenda"] = f.totalVenda;
  j["totalComissao"] = f.totalComissao;
  j["observacoes"] = f.observacoes;
  j["fechadoEm"] = f.fechadoEm;
  j["reabertoEm"] = f.reabertoEm;
  j["createdAt"] = f.createdAt;
  return j;
}

}  // namespace

std::string Api::listServicosJson() {
  require(features::kGestaoSosServicos, PermAction::Read);
  json arr = json::array();
  for (const auto& s : estoque::listServicos(db_)) arr.push_back(servicoToJson(s));
  return arr.dump();
}

std::string Api::createServico(const std::string& payload) {
  require(features::kGestaoSosServicos, PermAction::Create);
  auto created = estoque::createServico(db_, servicoFromJson(json::parse(payload)));
  return servicoToJson(created).dump();
}

std::string Api::updateServico(const std::string& payload) {
  require(features::kGestaoSosServicos, PermAction::Update);
  auto updated = estoque::updateServico(db_, servicoFromJson(json::parse(payload)));
  return servicoToJson(updated).dump();
}

void Api::deleteServico(const std::string& id) {
  require(features::kGestaoSosServicos, PermAction::Delete);
  estoque::deleteServico(db_, id);
}

std::string Api::listFechamentosJson() {
  require(features::kGestaoSosServicos, PermAction::Read);
  json arr = json::array();
  for (const auto& f : estoque::listFechamentos(db_)) arr.push_back(fechamentoToJson(f));
  return arr.dump();
}

std::string Api::fecharMes(const std::string& payload) {
  require(features::kGestaoSosServicos, PermAction::Update);
  json j = json::parse(payload);
  auto f = estoque::fecharMes(db_, jstr(j, "id"), jstr(j, "mesReferencia"), jstr(j, "observacoes"),
                              jstr(j, "fechadoEm"), jstr(j, "createdAt"));
  return fechamentoToJson(f).dump();
}

void Api::reabrirFechamento(const std::string& id) {
  require(features::kGestaoSosServicos, PermAction::Update);
  estoque::reabrirFechamento(db_, id, time_utils::systemNowIso());
}

namespace {
// String do valor salvo, ou o padrão de fábrica (sos_config_padrao) em
// texto — getSosConfigJson sempre devolve algo preenchido, nunca uma
// string vazia que a tela teria que adivinhar o que significa.
std::string configOuPadrao(Database& db, const char* key, double padrao) {
  std::string v = estoque::getConfig(db, key, "");
  return v.empty() ? std::to_string(padrao) : v;
}
}  // namespace

std::string Api::getSosConfigJson() {
  require(features::kGestaoSosServicos, PermAction::Read);
  json j;
  j["porcentagemPadrao"] = estoque::getConfig(db_, sos_config::kPorcentagemPadrao, "0");
  // Todas as porcentagens do Dashboard de Fechamento — "Sempre o controle de
  // porcentagem deve estar no Botão Configurações": esta é a única tela que
  // grava estes valores; o resto do módulo só lê.
  j["rateioFl"] = configOuPadrao(db_, sos_config::kRateioFl, sos_config_padrao::kRateioFl);
  j["rateioGerentes"] = configOuPadrao(db_, sos_config::kRateioGerentes, sos_config_padrao::kRateioGerentes);
  j["rateioSuprimentos"] =
      configOuPadrao(db_, sos_config::kRateioSuprimentos, sos_config_padrao::kRateioSuprimentos);
  j["suprimentosEncarregado"] =
      configOuPadrao(db_, sos_config::kSuprimentosEncarregado, sos_config_padrao::kSuprimentosEncarregado);
  j["suprimentosAssistente"] =
      configOuPadrao(db_, sos_config::kSuprimentosAssistente, sos_config_padrao::kSuprimentosAssistente);
  j["metaPorCondominio"] =
      configOuPadrao(db_, sos_config::kMetaPorCondominio, sos_config_padrao::kMetaPorCondominio);
  j["deltaSindica"] = configOuPadrao(db_, sos_config::kDeltaSindica, sos_config_padrao::kDeltaSindica);
  j["deltaGerente"] = configOuPadrao(db_, sos_config::kDeltaGerente, sos_config_padrao::kDeltaGerente);
  // Dados bancários da Delta (texto, sem padrão numérico) — usados em
  // Programar Pagamento.
  j["deltaChavePix"] = estoque::getConfig(db_, sos_config::kDeltaChavePix, "");
  j["deltaTitular"] = estoque::getConfig(db_, sos_config::kDeltaTitular, "");
  return j.dump();
}

void Api::setSosConfig(const std::string& payload) {
  require(features::kGestaoSosServicos, PermAction::Update);
  json j = json::parse(payload);
  // Cada chave só grava quando a tela manda ela — a tela de Serviços manda
  // só porcentagemPadrao, a de Configurações manda só os percentuais do
  // Dashboard; nenhuma das duas apaga a chave que a outra é responsável
  // por gravar.
  auto gravarSePresente = [&](const char* chave, const char* jsonKey, double padrao) {
    if (j.contains(jsonKey)) estoque::setConfig(db_, chave, jstr(j, jsonKey, std::to_string(padrao)));
  };
  if (j.contains("porcentagemPadrao")) {
    estoque::setConfig(db_, sos_config::kPorcentagemPadrao, jstr(j, "porcentagemPadrao", "0"));
  }
  gravarSePresente(sos_config::kRateioFl, "rateioFl", sos_config_padrao::kRateioFl);
  gravarSePresente(sos_config::kRateioGerentes, "rateioGerentes", sos_config_padrao::kRateioGerentes);
  gravarSePresente(sos_config::kRateioSuprimentos, "rateioSuprimentos", sos_config_padrao::kRateioSuprimentos);
  gravarSePresente(sos_config::kSuprimentosEncarregado, "suprimentosEncarregado",
                   sos_config_padrao::kSuprimentosEncarregado);
  gravarSePresente(sos_config::kSuprimentosAssistente, "suprimentosAssistente",
                   sos_config_padrao::kSuprimentosAssistente);
  gravarSePresente(sos_config::kMetaPorCondominio, "metaPorCondominio", sos_config_padrao::kMetaPorCondominio);
  gravarSePresente(sos_config::kDeltaSindica, "deltaSindica", sos_config_padrao::kDeltaSindica);
  gravarSePresente(sos_config::kDeltaGerente, "deltaGerente", sos_config_padrao::kDeltaGerente);
  if (j.contains("deltaChavePix")) {
    estoque::setConfig(db_, sos_config::kDeltaChavePix, jstr(j, "deltaChavePix", ""));
  }
  if (j.contains("deltaTitular")) {
    estoque::setConfig(db_, sos_config::kDeltaTitular, jstr(j, "deltaTitular", ""));
  }
}

// --------------------------------------------------------------- compras

namespace {

json aquisicaoToJson(const Aquisicao& a) {
  json j;
  j["id"] = a.id;
  j["fornecedorId"] = a.fornecedorId;
  j["fornecedorNome"] = a.fornecedorNome;
  j["descricao"] = a.descricao;
  j["notaFiscal"] = a.notaFiscal;
  j["valor"] = a.valor;
  j["dataCompra"] = a.dataCompra;
  j["observacoes"] = a.observacoes;
  j["createdAt"] = a.createdAt;
  j["anexoPath"] = a.anexoPath;
  j["anexoTipo"] = a.anexoTipo;
  return j;
}

Aquisicao aquisicaoFromJson(const json& j) {
  Aquisicao a;
  a.id = jstr(j, "id");
  a.fornecedorId = jstr(j, "fornecedorId");
  a.descricao = jstr(j, "descricao");
  a.notaFiscal = jstr(j, "notaFiscal");
  a.valor = jnum(j, "valor");
  a.dataCompra = jstr(j, "dataCompra");
  a.observacoes = jstr(j, "observacoes");
  a.createdAt = jstr(j, "createdAt");
  return a;
}

// "R$ 1.234,56" — não existe formatação de moeda em nenhum outro lugar do
// C++ (é sempre JS, ver format.js), mas o corpo do e-mail é montado AQUI
// (o C++ não tem rede, mas é quem sabe compor o texto — ver comentário de
// enviarOrcamentoParaCliente).
std::string formatarReal(double v) {
  bool negativo = v < 0;
  if (negativo) v = -v;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", v);
  std::string s(buf);
  size_t ponto = s.find('.');
  std::string inteiro = s.substr(0, ponto);
  std::string centavos = s.substr(ponto + 1);
  std::string comSeparador;
  int contador = 0;
  for (auto it = inteiro.rbegin(); it != inteiro.rend(); ++it) {
    if (contador != 0 && contador % 3 == 0) comSeparador.push_back('.');
    comSeparador.push_back(*it);
    ++contador;
  }
  std::reverse(comSeparador.begin(), comSeparador.end());
  return std::string(negativo ? "-R$ " : "R$ ") + comSeparador + "," + centavos;
}

json propostaOrcamentoToJson(const PropostaOrcamento& p) {
  json j;
  j["id"] = p.id;
  j["ordemId"] = p.ordemId;
  j["empresaId"] = p.empresaId;
  j["empresaNome"] = p.empresaNome;
  j["valor"] = p.valor;         // -1 == sem resposta ainda (ver purchases_engine.hpp)
  j["temResposta"] = p.valor >= 0;
  j["anexoPath"] = p.anexoPath;
  j["anexoTipo"] = p.anexoTipo;
  j["emailEnviadoEm"] = p.emailEnviadoEm;
  j["recomendada"] = p.recomendada;
  j["createdAt"] = p.createdAt;
  return j;
}

json ordemOrcamentoToJson(Database& db, const OrdemOrcamento& o, const std::string& hojeIso) {
  json j;
  j["id"] = o.id;
  j["numero"] = o.numero;
  j["condominioId"] = o.condominioId;
  j["condominioNome"] = o.condominioNome;
  std::string condominioEmail;
  if (auto c = findCondominio(db, o.condominioId)) condominioEmail = c->email;
  j["condominioEmail"] = condominioEmail;
  j["descricao"] = o.descricao;
  j["observacoes"] = o.observacoes;
  j["status"] = o.status;
  j["statusEfetivo"] = ordemOrcamentoStatusEfetivo(o, hojeIso);
  j["propostaRecomendadaId"] = o.propostaRecomendadaId;
  j["propostaAprovadaId"] = o.propostaAprovadaId;
  j["dataSolicitacao"] = o.dataSolicitacao;
  j["dataEnvioCliente"] = o.dataEnvioCliente;
  j["dataAprovacao"] = o.dataAprovacao;
  j["reabertoEm"] = o.reabertoEm;
  j["createdAt"] = o.createdAt;
  json propostas = json::array();
  for (const auto& p : o.propostas) propostas.push_back(propostaOrcamentoToJson(p));
  j["propostas"] = propostas;
  return j;
}

// E-mails são compostos aqui (texto/HTML), nunca enviados — o C++ não tem
// acesso a rede. O comando Tauri chamador itera este array chamando
// bridge::mailer::send_email por item (ver comentário de
// Api::solicitarOrcamentoParaEmpresas em api.hpp).
json emailParaJson(const std::string& to, const std::string& subject, const std::string& bodyHtml,
                   const std::vector<std::string>& attachmentPaths) {
  json j;
  j["to"] = to;
  j["subject"] = subject;
  j["bodyHtml"] = bodyHtml;
  j["attachmentPaths"] = attachmentPaths;
  return j;
}

std::string composeSolicitacaoBody(const OrdemOrcamento& ordem) {
  std::string html = "<p>Olá,</p><p>A FL Condomínios solicita um orçamento";
  if (!ordem.condominioNome.empty()) html += " para o condomínio <b>" + ordem.condominioNome + "</b>";
  html += ":</p><p><b>Descrição:</b> " + ordem.descricao + "</p>";
  if (!ordem.observacoes.empty()) html += "<p><b>Observações:</b> " + ordem.observacoes + "</p>";
  html += "<p>Por favor, envie sua proposta em resposta a este e-mail.</p>"
          "<p>Atenciosamente,<br/>FL Condomínios</p>";
  return html;
}

std::string composeEnvioClienteBody(const OrdemOrcamento& ordem, const std::string& mensagemExtra) {
  std::string html = "<p>Olá,</p><p>Seguem as propostas recebidas para o pedido <b>" + ordem.descricao + "</b>";
  if (!ordem.condominioNome.empty()) html += " (" + ordem.condominioNome + ")";
  html += ":</p>";
  if (!mensagemExtra.empty()) html += "<p>" + mensagemExtra + "</p>";
  html += "<table border=\"1\" cellpadding=\"6\" style=\"border-collapse:collapse;\">"
          "<tr><th>Empresa</th><th>Valor</th><th></th></tr>";
  // Já vem ordenado do mais caro pro mais barato (ver carregarPropostas em
  // purchases_engine.cpp) — a tabela do e-mail reflete a mesma ordem da tela.
  for (const auto& p : ordem.propostas) {
    if (p.valor < 0) continue;  // só quem já respondeu entra na análise
    html += "<tr><td>" + p.empresaNome + "</td><td>" + formatarReal(p.valor) + "</td><td>" +
            (p.recomendada ? "<b>Recomendada pela FL</b>" : "") + "</td></tr>";
  }
  html += "</table><p>Os PDFs das propostas seguem em anexo.</p><p>Atenciosamente,<br/>FL Condomínios</p>";
  return html;
}

std::string composeAprovacaoBody(const OrdemOrcamento& ordem, const PropostaOrcamento& vencedora) {
  std::string html = "<p>Olá, " + vencedora.empresaNome + "!</p>"
      "<p>Temos o prazer de informar que sua proposta para <b>" + ordem.descricao + "</b>";
  if (!ordem.condominioNome.empty()) html += " (" + ordem.condominioNome + ")";
  html += " foi <b>aprovada</b>.</p>"
          "<p>Segue em anexo a proposta aprovada, para seu controle.</p>"
          "<p>Atenciosamente,<br/>FL Condomínios</p>";
  return html;
}

json parcelaToJson(const Parcela& p) {
  json j;
  j["id"] = p.id;
  j["numero"] = p.numero;
  j["valor"] = p.valor;
  j["vencimento"] = p.vencimento;
  j["pago"] = p.pago;
  j["dataPagamento"] = p.dataPagamento;
  return j;
}

Parcela parcelaFromJson(const json& j) {
  Parcela p;
  p.id = jstr(j, "id");
  p.numero = static_cast<int>(jnum(j, "numero"));
  p.valor = jnum(j, "valor");
  p.vencimento = jstr(j, "vencimento");
  p.pago = j.contains("pago") && j["pago"].is_boolean() && j["pago"].get<bool>();
  p.dataPagamento = jstr(j, "dataPagamento");
  return p;
}

// Pagamento não denormaliza dado nenhum da Aquisição (ver o comentário de
// topo em purchases_engine.hpp) — aquisicaoDescricao/fornecedorNome/
// notaFiscalAquisicao vêm resolvidos AQUI, na leitura, porque excluir uma
// Aquisição com pagamentos é sempre recusado (a ligação nunca fica órfã).
json pagamentoToJson(Database& db, const Pagamento& p) {
  json j;
  j["id"] = p.id;
  j["aquisicaoId"] = p.aquisicaoId;
  if (auto aq = findAquisicao(db, p.aquisicaoId)) {
    j["aquisicaoDescricao"] = aq->descricao;
    j["fornecedorNome"] = aq->fornecedorNome;
  } else {
    j["aquisicaoDescricao"] = "";
    j["fornecedorNome"] = "";
  }
  j["notaFiscal"] = p.notaFiscal;
  j["valorTotal"] = p.valorTotal;
  j["dataEmissao"] = p.dataEmissao;
  j["observacoes"] = p.observacoes;
  j["createdAt"] = p.createdAt;
  json parcelas = json::array();
  double totalPago = 0;
  for (const auto& parc : p.parcelas) {
    parcelas.push_back(parcelaToJson(parc));
    if (parc.pago) totalPago += parc.valor;
  }
  j["parcelas"] = parcelas;
  j["totalPago"] = totalPago;
  j["totalEmAberto"] = p.valorTotal - totalPago;
  j["quitado"] = !p.parcelas.empty() &&
                 std::all_of(p.parcelas.begin(), p.parcelas.end(), [](const Parcela& x) { return x.pago; });
  return j;
}

Pagamento pagamentoFromJson(const json& j) {
  Pagamento p;
  p.id = jstr(j, "id");
  p.aquisicaoId = jstr(j, "aquisicaoId");
  p.notaFiscal = jstr(j, "notaFiscal");
  p.valorTotal = jnum(j, "valorTotal");
  p.dataEmissao = jstr(j, "dataEmissao");
  p.observacoes = jstr(j, "observacoes");
  p.createdAt = jstr(j, "createdAt");
  if (j.contains("parcelas") && j["parcelas"].is_array()) {
    for (const auto& pj : j["parcelas"]) p.parcelas.push_back(parcelaFromJson(pj));
  }
  return p;
}

}  // namespace

std::string Api::listAquisicoesJson() {
  require(features::kAquisicoes, PermAction::Read);
  json arr = json::array();
  for (const auto& a : estoque::listAquisicoes(db_)) arr.push_back(aquisicaoToJson(a));
  return arr.dump();
}

std::string Api::createAquisicao(const std::string& payload) {
  require(features::kAquisicoes, PermAction::Create);
  auto created = estoque::createAquisicao(db_, aquisicaoFromJson(json::parse(payload)));
  return aquisicaoToJson(created).dump();
}

std::string Api::updateAquisicao(const std::string& payload) {
  require(features::kAquisicoes, PermAction::Update);
  auto updated = estoque::updateAquisicao(db_, aquisicaoFromJson(json::parse(payload)));
  return aquisicaoToJson(updated).dump();
}

void Api::deleteAquisicao(const std::string& id) {
  require(features::kAquisicoes, PermAction::Delete);
  estoque::deleteAquisicao(db_, id);
}

std::string Api::setAquisicaoAnexo(const std::string& aquisicaoId, const std::string& anexoPath,
                                   const std::string& anexoTipo) {
  require(features::kAquisicoes, PermAction::Update);
  auto updated = estoque::setAquisicaoAnexo(db_, aquisicaoId, anexoPath, anexoTipo);
  return aquisicaoToJson(updated).dump();
}

std::string Api::clearAquisicaoAnexo(const std::string& aquisicaoId) {
  require(features::kAquisicoes, PermAction::Update);
  auto updated = estoque::clearAquisicaoAnexo(db_, aquisicaoId);
  return aquisicaoToJson(updated).dump();
}

std::string Api::listOrdensOrcamentoJson(const std::string& nowIso) {
  require(features::kOrcamentos, PermAction::Read);
  json arr = json::array();
  for (const auto& o : estoque::listOrdensOrcamento(db_)) arr.push_back(ordemOrcamentoToJson(db_, o, nowIso));
  return arr.dump();
}

std::string Api::createOrdemOrcamento(const std::string& payload) {
  require(features::kOrcamentos, PermAction::Create);
  auto j = json::parse(payload);
  OrdemOrcamento input;
  input.id = jstr(j, "id");
  input.condominioId = jstr(j, "condominioId");
  input.descricao = jstr(j, "descricao");
  input.observacoes = jstr(j, "observacoes");
  input.createdAt = jstr(j, "createdAt");
  auto created = estoque::createOrdemOrcamento(db_, input);
  return ordemOrcamentoToJson(db_, created, created.createdAt).dump();
}

std::string Api::updateOrdemOrcamentoInfo(const std::string& payload) {
  require(features::kOrcamentos, PermAction::Update);
  auto j = json::parse(payload);
  auto updated = estoque::updateOrdemOrcamentoInfo(db_, jstr(j, "id"), jstr(j, "descricao"), jstr(j, "observacoes"));
  return ordemOrcamentoToJson(db_, updated, updated.createdAt).dump();
}

void Api::deleteOrdemOrcamento(const std::string& id) {
  require(features::kOrcamentos, PermAction::Delete);
  estoque::deleteOrdemOrcamento(db_, id);
}

std::string Api::solicitarOrcamentoParaEmpresas(const std::string& payload, const std::string& nowIso) {
  require(features::kOrcamentos, PermAction::Update);
  auto j = json::parse(payload);
  std::string ordemId = jstr(j, "ordemId");

  std::vector<EmpresaSolicitada> empresas;
  if (j.contains("empresas") && j["empresas"].is_array()) {
    for (const auto& ej : j["empresas"]) {
      empresas.push_back({jstr(ej, "empresaId"), jstr(ej, "empresaNome")});
    }
  }

  auto updated = estoque::solicitarOrcamentoParaEmpresas(db_, ordemId, empresas, nowIso);

  // Manda e-mail pra TODA empresa selecionada nesta chamada — inclusive quem
  // já tinha sido chamada numa rodada anterior. Selecionar de novo é sempre
  // um pedido explícito de reenvio (o usuário decide quantas vezes precisar,
  // mesmo para quem já recebeu — ver solicitarOrcamentoParaEmpresas em
  // purchases_engine.cpp, que atualiza o email_enviado_em em vez de ignorar).
  json emails = json::array();
  std::string corpo = composeSolicitacaoBody(updated);
  for (const auto& e : empresas) {
    if (e.empresaId.empty()) continue;
    auto emp = findEmpresa(db_, e.empresaId);
    std::string to = emp ? emp->emails : "";
    if (to.empty()) continue;
    emails.push_back(emailParaJson(to, "Solicitação de orçamento — " + updated.condominioNome, corpo, {}));
  }

  json out = ordemOrcamentoToJson(db_, updated, nowIso);
  out["emails"] = emails;
  return out.dump();
}

std::string Api::reenviarSolicitacaoProposta(const std::string& propostaId, const std::string& nowIso) {
  require(features::kOrcamentos, PermAction::Update);
  auto proposta = estoque::reenviarSolicitacaoProposta(db_, propostaId, nowIso);
  auto ordem = estoque::findOrdemOrcamento(db_, proposta.ordemId);
  if (!ordem) throw NotFoundError("ordem de orçamento não encontrada: " + proposta.ordemId);

  json emails = json::array();
  auto emp = findEmpresa(db_, proposta.empresaId);
  std::string to = emp ? emp->emails : "";
  if (!to.empty()) {
    emails.push_back(emailParaJson(to, "Solicitação de orçamento — " + ordem->condominioNome,
                                    composeSolicitacaoBody(*ordem), {}));
  }

  json out = ordemOrcamentoToJson(db_, *ordem, nowIso);
  out["emails"] = emails;
  return out.dump();
}

std::string Api::setPropostaValor(const std::string& propostaId, double valor) {
  require(features::kOrcamentos, PermAction::Update);
  auto updated = estoque::setPropostaValor(db_, propostaId, valor);
  return propostaOrcamentoToJson(updated).dump();
}

std::string Api::setPropostaAnexo(const std::string& propostaId, const std::string& anexoPath,
                                  const std::string& anexoTipo) {
  require(features::kOrcamentos, PermAction::Update);
  auto updated = estoque::setPropostaAnexo(db_, propostaId, anexoPath, anexoTipo);
  return propostaOrcamentoToJson(updated).dump();
}

std::string Api::clearPropostaAnexo(const std::string& propostaId) {
  require(features::kOrcamentos, PermAction::Update);
  auto updated = estoque::clearPropostaAnexo(db_, propostaId);
  return propostaOrcamentoToJson(updated).dump();
}

std::string Api::marcarPropostaRecomendada(const std::string& ordemId, const std::string& propostaId) {
  require(features::kOrcamentos, PermAction::Update);
  auto updated = estoque::marcarPropostaRecomendada(db_, ordemId, propostaId);
  return ordemOrcamentoToJson(db_, updated, updated.createdAt).dump();
}

std::string Api::desmarcarPropostaRecomendada(const std::string& ordemId) {
  require(features::kOrcamentos, PermAction::Update);
  auto updated = estoque::desmarcarPropostaRecomendada(db_, ordemId);
  return ordemOrcamentoToJson(db_, updated, updated.createdAt).dump();
}

std::string Api::enviarOrcamentoParaCliente(const std::string& payload, const std::string& nowIso) {
  require(features::kOrcamentos, PermAction::Update);
  auto j = json::parse(payload);
  std::string ordemId = jstr(j, "ordemId");
  std::string destinatarioOverride = jstr(j, "destinatarioEmail");
  std::string mensagemExtra = jstr(j, "mensagemExtra");

  // "Enviar a parte": o mapa impresso e o modal já deixam o usuário
  // desmarcar quem não quer incluir — sem propostaIds no payload (chamada
  // antiga ou script), mantém todas as respondidas, mesmo critério de
  // sempre.
  std::set<std::string> propostaIdsSelecionadas;
  bool temFiltro = j.contains("propostaIds") && j["propostaIds"].is_array();
  if (temFiltro) {
    for (const auto& pid : j["propostaIds"]) propostaIdsSelecionadas.insert(pid.get<std::string>());
  }

  auto updated = estoque::enviarOrcamentoParaCliente(db_, ordemId, nowIso);

  std::string condominioEmail;
  if (auto c = findCondominio(db_, updated.condominioId)) condominioEmail = c->email;
  std::string destinatario = !destinatarioOverride.empty() ? destinatarioOverride : condominioEmail;
  json emails = json::array();
  if (!destinatario.empty()) {
    OrdemOrcamento paraEmail = updated;
    if (temFiltro) {
      std::vector<PropostaOrcamento> filtradas;
      for (auto& p : paraEmail.propostas) {
        if (propostaIdsSelecionadas.count(p.id)) filtradas.push_back(p);
      }
      paraEmail.propostas = std::move(filtradas);
    }
    std::vector<std::string> anexos;
    for (const auto& p : paraEmail.propostas) {
      if (!p.anexoPath.empty()) anexos.push_back(p.anexoPath);
    }
    std::string assunto = "Orçamentos — " + updated.descricao;
    if (!updated.condominioNome.empty()) assunto += " (" + updated.condominioNome + ")";
    emails.push_back(emailParaJson(destinatario, assunto, composeEnvioClienteBody(paraEmail, mensagemExtra), anexos));
  }

  json out = ordemOrcamentoToJson(db_, updated, nowIso);
  out["emails"] = emails;
  return out.dump();
}

std::string Api::aprovarPropostaOrcamento(const std::string& ordemId, const std::string& propostaId,
                                          const std::string& nowIso) {
  require(features::kOrcamentos, PermAction::Update);
  auto updated = estoque::aprovarPropostaOrcamento(db_, ordemId, propostaId, nowIso);

  auto vencedoraIt = std::find_if(updated.propostas.begin(), updated.propostas.end(),
                                  [&](const PropostaOrcamento& p) { return p.id == propostaId; });

  json emails = json::array();
  if (vencedoraIt != updated.propostas.end()) {
    auto emp = findEmpresa(db_, vencedoraIt->empresaId);
    std::string to = emp ? emp->emails : "";
    if (!to.empty()) {
      std::string assunto = "Proposta aprovada — " + updated.descricao;
      if (!updated.condominioNome.empty()) assunto += " (" + updated.condominioNome + ")";
      std::vector<std::string> anexos;
      if (!vencedoraIt->anexoPath.empty()) anexos.push_back(vencedoraIt->anexoPath);
      emails.push_back(emailParaJson(to, assunto, composeAprovacaoBody(updated, *vencedoraIt), anexos));
    }
  }

  json out = ordemOrcamentoToJson(db_, updated, nowIso);
  out["emails"] = emails;
  return out.dump();
}

std::string Api::reativarOrdemOrcamento(const std::string& ordemId, const std::string& nowIso) {
  require(features::kOrcamentos, PermAction::Update);
  auto updated = estoque::reativarOrdemOrcamento(db_, ordemId, nowIso);
  return ordemOrcamentoToJson(db_, updated, nowIso).dump();
}

// ---------------------------------------------------- configuração de e-mail

namespace {
const char* kEmailCfgHost = "email_smtp_host";
const char* kEmailCfgPort = "email_smtp_port";
const char* kEmailCfgUsername = "email_smtp_username";
const char* kEmailCfgPassword = "email_smtp_password";
const char* kEmailCfgFromEmail = "email_smtp_from_email";
const char* kEmailCfgFromName = "email_smtp_from_name";
const char* kEmailCfgUseTls = "email_smtp_use_tls";

std::string getAppSetting(Database& db, const std::string& key, const std::string& def) {
  auto st = db.prepare("SELECT value FROM app_settings WHERE key=?");
  st.bind(1, key);
  if (!st.step()) return def;
  return st.columnText(0);
}

void setAppSetting(Database& db, const std::string& key, const std::string& value) {
  auto st = db.prepare(
      "INSERT INTO app_settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  st.bind(1, key).bind(2, value);
  st.step();
}
}  // namespace

std::string Api::getEmailConfigJson() {
  assertPodeEditarLogoFl();  // mesma regra: só superadministrador
  json j;
  j["host"] = getAppSetting(db_, kEmailCfgHost, "");
  j["port"] = getAppSetting(db_, kEmailCfgPort, "587");
  j["username"] = getAppSetting(db_, kEmailCfgUsername, "");
  j["fromEmail"] = getAppSetting(db_, kEmailCfgFromEmail, "");
  j["fromName"] = getAppSetting(db_, kEmailCfgFromName, "FL Condomínios");
  j["useTls"] = getAppSetting(db_, kEmailCfgUseTls, "1") == "1";
  j["temSenha"] = !getAppSetting(db_, kEmailCfgPassword, "").empty();
  return j.dump();
}

std::string Api::getEmailConfigInternalJson() {
  assertPodeEditarLogoFl();
  json j;
  j["host"] = getAppSetting(db_, kEmailCfgHost, "");
  j["port"] = getAppSetting(db_, kEmailCfgPort, "587");
  j["username"] = getAppSetting(db_, kEmailCfgUsername, "");
  j["password"] = getAppSetting(db_, kEmailCfgPassword, "");
  j["fromEmail"] = getAppSetting(db_, kEmailCfgFromEmail, "");
  j["fromName"] = getAppSetting(db_, kEmailCfgFromName, "FL Condomínios");
  j["useTls"] = getAppSetting(db_, kEmailCfgUseTls, "1") == "1";
  return j.dump();
}

void Api::setEmailConfig(const std::string& payload) {
  assertPodeEditarLogoFl();
  auto j = json::parse(payload);
  if (j.contains("host")) setAppSetting(db_, kEmailCfgHost, jstr(j, "host"));
  if (j.contains("port")) setAppSetting(db_, kEmailCfgPort, jstr(j, "port", "587"));
  if (j.contains("username")) setAppSetting(db_, kEmailCfgUsername, jstr(j, "username"));
  if (j.contains("password")) setAppSetting(db_, kEmailCfgPassword, jstr(j, "password"));
  if (j.contains("fromEmail")) setAppSetting(db_, kEmailCfgFromEmail, jstr(j, "fromEmail"));
  if (j.contains("fromName")) setAppSetting(db_, kEmailCfgFromName, jstr(j, "fromName"));
  if (j.contains("useTls")) setAppSetting(db_, kEmailCfgUseTls, jbool(j, "useTls", true) ? "1" : "0");
}

std::string Api::listPagamentosJson() {
  require(features::kPagamentos, PermAction::Read);
  json arr = json::array();
  for (const auto& p : estoque::listPagamentos(db_)) arr.push_back(pagamentoToJson(db_, p));
  return arr.dump();
}

std::string Api::createPagamento(const std::string& payload) {
  require(features::kPagamentos, PermAction::Create);
  auto created = estoque::createPagamento(db_, pagamentoFromJson(json::parse(payload)));
  return pagamentoToJson(db_, created).dump();
}

std::string Api::updatePagamento(const std::string& payload) {
  require(features::kPagamentos, PermAction::Update);
  auto updated = estoque::updatePagamento(db_, pagamentoFromJson(json::parse(payload)));
  return pagamentoToJson(db_, updated).dump();
}

void Api::deletePagamento(const std::string& id) {
  require(features::kPagamentos, PermAction::Delete);
  estoque::deletePagamento(db_, id);
}

std::string Api::marcarParcela(const std::string& payload) {
  require(features::kPagamentos, PermAction::Update);
  json j = json::parse(payload);
  auto updated = estoque::marcarParcela(db_, jstr(j, "pagamentoId"), jstr(j, "parcelaId"),
                                        j.contains("pago") && j["pago"].is_boolean() && j["pago"].get<bool>(),
                                        jstr(j, "dataPagamento"));
  return pagamentoToJson(db_, updated).dump();
}

// ---------------------------------------------------------- backup/restore

std::string Api::backupJson() {
  require(features::kImportarExportar, PermAction::Create);

  json products = json::array();
  for (auto& p : estoque::listProducts(db_)) products.push_back(productToJson(p));

  json departments = json::array();
  for (auto& d : estoque::listDepartments(db_)) departments.push_back(departmentToJson(d));

  json movements = json::array();
  auto st = db_.prepare(
      "SELECT id, type, product_id, qty, unit_price, supplier, nf, department_id, recipient, "
      "encarregado, requester, obs, date, resulting_qty, resulting_avg_cost, created_at, new_avg_cost "
      "FROM movements ORDER BY date, created_at");
  while (st.step()) {
    json m;
    m["id"] = st.columnText(0);
    m["type"] = st.columnText(1);
    m["productId"] = st.columnText(2);
    m["qty"] = st.columnDouble(3);
    m["unitPrice"] = st.columnIsNull(4) ? json(nullptr) : json(st.columnDouble(4));
    m["supplier"] = st.columnIsNull(5) ? "" : st.columnText(5);
    m["nf"] = st.columnIsNull(6) ? "" : st.columnText(6);
    m["departmentId"] = st.columnIsNull(7) ? "" : st.columnText(7);
    m["recipient"] = st.columnIsNull(8) ? "" : st.columnText(8);
    m["encarregado"] = st.columnIsNull(9) ? "" : st.columnText(9);
    m["requester"] = st.columnIsNull(10) ? "" : st.columnText(10);
    m["obs"] = st.columnIsNull(11) ? "" : st.columnText(11);
    m["date"] = st.columnText(12);
    m["resultingQty"] = st.columnDouble(13);
    m["resultingAvgCost"] = st.columnDouble(14);
    m["createdAt"] = st.columnText(15);
    m["newAvgCost"] = st.columnIsNull(16) ? 0.0 : st.columnDouble(16);
    movements.push_back(m);
  }

  json settings = json::object();
  {
    auto sq = db_.prepare("SELECT key, value FROM app_settings");
    while (sq.step()) settings[sq.columnText(0)] = sq.columnText(1);
  }

  // Usuários vão com o hash da senha (nunca a senha): restaurar o backup em
  // outro PC precisa manter os logins funcionando, senão o "app portátil no
  // pendrive" viraria um app sem ninguém que consegue entrar.
  json users = json::array();
  {
    auto uq = db_.prepare(
        "SELECT id, email, email_key, name, role, department_id, active, password_hash, "
        "password_salt, password_iterations, created_at, last_login_at FROM users ORDER BY name");
    while (uq.step()) {
      json u;
      u["id"] = uq.columnText(0);
      u["email"] = uq.columnText(1);
      u["emailKey"] = uq.columnText(2);
      u["name"] = uq.columnText(3);
      u["role"] = uq.columnText(4);
      u["departmentId"] = uq.columnIsNull(5) ? "" : uq.columnText(5);
      u["active"] = uq.columnDouble(6) != 0;
      u["passwordHash"] = uq.columnText(7);
      u["passwordSalt"] = uq.columnText(8);
      u["passwordIterations"] = uq.columnDouble(9);
      u["createdAt"] = uq.columnText(10);
      u["lastLoginAt"] = uq.columnIsNull(11) ? "" : uq.columnText(11);
      users.push_back(u);
    }
  }

  json groups = json::array();
  json deptGroups = json::array();
  for (const auto& g : estoque::listPermissionGroups(db_)) groups.push_back(groupToJson(g));
  {
    auto dq = db_.prepare(
        "SELECT id, permission_group_id FROM departments WHERE permission_group_id IS NOT NULL");
    while (dq.step()) {
      deptGroups.push_back({{"departmentId", dq.columnText(0)}, {"groupId", dq.columnText(1)}});
    }
  }

  json requests = json::array();
  for (const auto& r : estoque::listRequests(db_, "")) requests.push_back(requestToJson(r));

  // As janelas entram no backup pelo mesmo motivo das requisições: sem elas,
  // restaurar um backup deixaria o app fechado para pedidos sem explicação, e
  // o histórico perderia o registro de qual período autorizou cada pedido.
  json requestWindows = json::array();
  for (const auto& w : estoque::listRequestWindows(db_)) {
    json jw = windowToJson(w);
    jw["createdByUserId"] = w.createdByUserId;
    jw["closedByUserId"] = w.closedByUserId;
    requestWindows.push_back(jw);
  }

  // Gestão de prazos: campos crus (sem vencimento/status calculados — isso é
  // derivado na leitura, não faz sentido num backup) para restauração fiel.
  json condominios = json::array();
  for (const auto& c : estoque::listCondominios(db_)) {
    condominios.push_back({{"id", c.id}, {"nome", c.nome}, {"nomeFantasia", c.nomeFantasia},
                           {"cnpj", c.cnpj}, {"codigo", c.codigo}, {"endereco", c.endereco},
                           {"numero", c.numero}, {"complemento", c.complemento}, {"bairro", c.bairro},
                           {"cidade", c.cidade}, {"estado", c.estado}, {"cep", c.cep},
                           {"localizacao", c.localizacao}, {"sindico", c.sindico}, {"telefone", c.telefone},
                           {"email", c.email}, {"observacoes", c.observacoes}, {"ativo", c.ativo},
                           {"deltaSindica", c.deltaSindica}, {"createdAt", c.createdAt}});
  }
  json tiposServico = json::array();
  for (const auto& t : estoque::listTiposServico(db_)) {
    tiposServico.push_back(
        {{"id", t.id}, {"nome", t.nome}, {"prazoDias", t.prazoDias}, {"cor", t.cor}, {"createdAt", t.createdAt}});
  }
  json servicosCondominio = json::array();
  for (const auto& s : estoque::listServicosCondominio(db_)) {
    servicosCondominio.push_back({{"id", s.id}, {"condominioId", s.condominioId},
                                  {"tipoServicoId", s.tipoServicoId},
                                  {"dataUltimaRenovacao", s.dataUltimaRenovacao},
                                  {"empresaContratada", s.empresaContratada}, {"observacoes", s.observacoes},
                                  {"createdAt", s.createdAt}});
  }
  json renovacoes = json::array();
  for (const auto& r : estoque::listRenovacoes(db_, "")) {
    renovacoes.push_back({{"id", r.id}, {"servicoCondominioId", r.servicoCondominioId},
                          {"dataRenovacao", r.dataRenovacao}, {"empresaContratada", r.empresaContratada},
                          {"prazoDiasAplicado", r.prazoDiasAplicado}, {"observacoes", r.observacoes},
                          {"createdAt", r.createdAt}});
  }

  // Fornecedores e prestadores. As especialidades vão como ids (a marcação é
  // o dado); os rótulos de setor são derivados do catálogo fixo na leitura.
  json especialidades = json::array();
  for (const auto& e : estoque::listEspecialidades(db_)) {
    especialidades.push_back(
        {{"id", e.id}, {"setor", e.setor}, {"nome", e.nome}, {"createdAt", e.createdAt}});
  }
  json empresas = json::array();
  for (const auto& e : estoque::listEmpresas(db_)) {
    empresas.push_back({{"id", e.id}, {"nome", e.nome}, {"nomeFantasia", e.nomeFantasia},
                        {"cnpj", e.cnpj}, {"endereco", e.endereco}, {"numero", e.numero},
                        {"complemento", e.complemento}, {"bairro", e.bairro},
                        {"cep", e.cep}, {"cidade", e.cidade}, {"estado", e.estado},
                        {"telefone", e.telefone}, {"emails", e.emails},
                        {"observacoes", e.observacoes}, {"parceira", e.parceira},
                        {"createdAt", e.createdAt},
                        {"especialidadeIds", e.especialidadeIds}});
  }

  // Gestão SOS: gerentes e a carteira de cada um (ids de condomínio — mesmo
  // critério de especialidadeIds acima).
  json gerentes = json::array();
  for (const auto& g : estoque::listGerentes(db_)) {
    // "numero" viaja explícito, mesmo motivo de sos_servicos.numero logo
    // abaixo: sem isso, restaurar reatribuiria o "ID" que a tela mostra.
    gerentes.push_back({{"id", g.id}, {"numero", g.numero}, {"nome", g.nome}, {"telefone", g.telefone},
                        {"email", g.email}, {"chavePix", g.chavePix}, {"observacoes", g.observacoes},
                        {"createdAt", g.createdAt}, {"condominioIds", g.condominioIds}});
  }

  // Gestão SOS: suprimentos (equipe de campo). "numero" viaja explícito pelo
  // mesmo motivo de gerentes.numero acima.
  json suprimentos = json::array();
  for (const auto& s : estoque::listSuprimentos(db_)) {
    suprimentos.push_back({{"id", s.id}, {"numero", s.numero}, {"nome", s.nome},
                           {"categoria", s.categoria}, {"telefone", s.telefone}, {"email", s.email},
                           {"chavePix", s.chavePix}, {"observacoes", s.observacoes},
                           {"createdAt", s.createdAt}});
  }

  // Gestão SOS: serviços (planilha de vendas/comissões) e fechamentos.
  // "numero" viaja explícito para o backup preservar exatamente o "ID" que o
  // usuário via na planilha — sem isso, restaurar reatribuiria os números pelo
  // AUTOINCREMENT a partir do zero.
  json sosServicos = json::array();
  for (const auto& s : estoque::listServicos(db_)) {
    sosServicos.push_back({{"numero", s.numero}, {"id", s.id}, {"codigo", s.codigo},
                           {"condominioId", s.condominioId}, {"condominioNome", s.condominioNome},
                           {"gerenteId", s.gerenteId}, {"gerenteNome", s.gerenteNome},
                           {"parceiroId", s.parceiroId}, {"parceiroNome", s.parceiroNome},
                           {"venda", s.venda}, {"porcentagem", s.porcentagem},
                           {"dataReferencia", s.dataReferencia}, {"fechamentoId", s.fechamentoId},
                           {"observacoes", s.observacoes}, {"createdAt", s.createdAt},
                           {"pago", s.pago}, {"dataPagamento", s.dataPagamento}});
  }
  json sosFechamentos = json::array();
  for (const auto& f : estoque::listFechamentos(db_)) {
    sosFechamentos.push_back({{"id", f.id}, {"mesReferencia", f.mesReferencia},
                              {"quantidadeServicos", f.quantidadeServicos},
                              {"totalVenda", f.totalVenda}, {"totalComissao", f.totalComissao},
                              {"observacoes", f.observacoes}, {"fechadoEm", f.fechadoEm},
                              {"reabertoEm", f.reabertoEm}, {"createdAt", f.createdAt}});
  }
  // Gestão SOS: Delta Síndicos NÃO entra mais aqui — é puxado de sosServicos
  // + condominios.deltaSindica (ambos já no backup), não tem mais identidade
  // própria (ver listDeltaSindicos em commissions_engine.cpp).

  // Gestão SOS: os retratos de dashboard já fechados. Vão no backup pelo
  // mesmo motivo dos fechamentos: são histórico apresentado à diretoria, e
  // restaurar sem eles perderia meses inteiros de prestação de contas.
  json sosDashboards = json::array();
  for (const auto& d : estoque::listDashboards(db_)) {
    sosDashboards.push_back({{"id", d.id}, {"mesReferencia", d.mesReferencia},
                             {"dadosJson", d.dadosJson}, {"observacoes", d.observacoes},
                             {"geradoEm", d.geradoEm}, {"createdAt", d.createdAt}});
  }

  json sosConfig;
  sosConfig["porcentagemPadrao"] = estoque::getConfig(db_, sos_config::kPorcentagemPadrao, "0");

  // Compras: Aquisições, Orçamentos e Pagamentos (com as parcelas ANINHADAS
  // dentro de cada pagamento — diferente de tudo acima, aqui não há uma
  // segunda chave de nível raiz para as parcelas, porque elas só existem
  // dentro de um pagamento específico).
  json comprasAquisicoes = json::array();
  for (const auto& a : estoque::listAquisicoes(db_)) {
    comprasAquisicoes.push_back({{"id", a.id}, {"fornecedorId", a.fornecedorId},
                                 {"fornecedorNome", a.fornecedorNome}, {"descricao", a.descricao},
                                 {"notaFiscal", a.notaFiscal}, {"valor", a.valor},
                                 {"dataCompra", a.dataCompra}, {"observacoes", a.observacoes},
                                 {"createdAt", a.createdAt}, {"anexoPath", a.anexoPath},
                                 {"anexoTipo", a.anexoTipo}});
  }
  // Ordens de orçamento: campos crus (sem statusEfetivo/condominioEmail —
  // isso é derivado na leitura, ver ordemOrcamentoToJson) para restauração
  // fiel, mesmo critério de Gestão de Prazos logo acima.
  json comprasOrdensOrcamento = json::array();
  for (const auto& o : estoque::listOrdensOrcamento(db_)) {
    json propostas = json::array();
    for (const auto& p : o.propostas) {
      propostas.push_back({{"id", p.id}, {"empresaId", p.empresaId}, {"empresaNome", p.empresaNome},
                           {"valor", p.valor}, {"anexoPath", p.anexoPath}, {"anexoTipo", p.anexoTipo},
                           {"emailEnviadoEm", p.emailEnviadoEm}, {"recomendada", p.recomendada},
                           {"createdAt", p.createdAt}});
    }
    comprasOrdensOrcamento.push_back(
        {{"id", o.id}, {"numero", o.numero}, {"condominioId", o.condominioId},
         {"condominioNome", o.condominioNome}, {"descricao", o.descricao}, {"observacoes", o.observacoes},
         {"status", o.status}, {"propostaRecomendadaId", o.propostaRecomendadaId},
         {"propostaAprovadaId", o.propostaAprovadaId}, {"dataSolicitacao", o.dataSolicitacao},
         {"dataEnvioCliente", o.dataEnvioCliente}, {"dataAprovacao", o.dataAprovacao},
         {"reabertoEm", o.reabertoEm}, {"createdAt", o.createdAt}, {"propostas", propostas}});
  }
  json comprasPagamentos = json::array();
  for (const auto& p : estoque::listPagamentos(db_)) {
    json parcelas = json::array();
    for (const auto& parc : p.parcelas) {
      parcelas.push_back({{"id", parc.id}, {"numero", parc.numero}, {"valor", parc.valor},
                          {"vencimento", parc.vencimento}, {"pago", parc.pago},
                          {"dataPagamento", parc.dataPagamento}, {"createdAt", parc.createdAt}});
    }
    comprasPagamentos.push_back({{"id", p.id}, {"aquisicaoId", p.aquisicaoId},
                                 {"notaFiscal", p.notaFiscal}, {"valorTotal", p.valorTotal},
                                 {"dataEmissao", p.dataEmissao}, {"observacoes", p.observacoes},
                                 {"createdAt", p.createdAt}, {"parcelas", parcelas}});
  }

  json root;
  root["products"] = products;
  root["movements"] = movements;
  root["departments"] = departments;
  root["especialidades"] = especialidades;
  root["empresas"] = empresas;
  root["condominios"] = condominios;
  root["gerentes"] = gerentes;
  root["suprimentos"] = suprimentos;
  root["sosServicos"] = sosServicos;
  root["sosFechamentos"] = sosFechamentos;
  root["sosDashboards"] = sosDashboards;
  root["sosConfig"] = sosConfig;
  root["comprasAquisicoes"] = comprasAquisicoes;
  root["comprasOrdensOrcamento"] = comprasOrdensOrcamento;
  root["comprasPagamentos"] = comprasPagamentos;
  root["tiposServico"] = tiposServico;
  root["servicosCondominio"] = servicosCondominio;
  root["renovacoes"] = renovacoes;
  root["deptCostHistory"] = json::parse(estoque::exportDeptCostHistoryJson(db_));
  root["settings"] = settings;
  root["users"] = users;
  root["permissionGroups"] = groups;
  root["departmentPermissionGroups"] = deptGroups;
  root["requests"] = requests;
  root["requestWindows"] = requestWindows;
  return root.dump();
}

void Api::restoreFromJson(const std::string& payload) {
  require(features::kImportarExportar, PermAction::Update);

  json root = json::parse(payload);
  if (!root.contains("products") || !root.contains("movements")) {
    throw std::runtime_error("backup inválido: faltam as chaves 'products'/'movements'");
  }

  Transaction tx(db_);
  db_.execute("DELETE FROM movements;");
  db_.execute("DELETE FROM products;");
  db_.execute("DELETE FROM departments;");
  // Histórico, preferências, usuários, permissões e requisições só são zerados
  // se o backup os trouxer: importar um backup antigo (anterior a cada um
  // desses recursos) não pode apagar em silêncio o que ele nem conhece — em
  // especial, não pode deixar o app sem nenhum usuário cadastrado.
  if (root.contains("deptCostHistory")) db_.execute("DELETE FROM dept_cost_history;");
  if (root.contains("settings")) db_.execute("DELETE FROM app_settings;");
  if (root.contains("users")) db_.execute("DELETE FROM users;");
  if (root.contains("permissionGroups")) {
    db_.execute("DELETE FROM permission_group_perms;");
    db_.execute("DELETE FROM permission_groups;");
  }
  if (root.contains("requests")) {
    db_.execute("DELETE FROM request_items;");
    db_.execute("DELETE FROM requests;");
  }
  if (root.contains("requestWindows")) db_.execute("DELETE FROM request_windows;");
  // "condominios" é a chave-âncora do conjunto (mesmo critério de "requests"
  // acima para request_items): um backup que traz gestão de prazos sempre traz
  // as quatro juntas. ON DELETE CASCADE (schema v8) já cuidaria da ordem, mas
  // apagar explícito do filho pro pai deixa a intenção clara.
  if (root.contains("condominios")) {
    db_.execute("DELETE FROM renovacoes;");
    db_.execute("DELETE FROM servicos_condominio;");
    db_.execute("DELETE FROM tipos_servico;");
    db_.execute("DELETE FROM condominios;");
  }
  // "especialidades" é a chave-âncora do módulo de empresas (mesmo critério de
  // "condominios" acima): quem traz o catálogo traz as empresas e as marcações.
  if (root.contains("especialidades")) {
    db_.execute("DELETE FROM empresa_especialidades;");
    db_.execute("DELETE FROM empresas;");
    db_.execute("DELETE FROM especialidades;");
  }
  // "gerentes" é sua própria chave-âncora (independente de "condominios"): um
  // backup pode trazer condomínios sem nenhum gerente cadastrado ainda.
  if (root.contains("gerentes")) {
    db_.execute("DELETE FROM gerente_condominios;");
    db_.execute("DELETE FROM gerentes;");
  }
  // "suprimentos" é sua própria chave-âncora: sem carteira nem vínculo com
  // condomínio, é um cadastro totalmente independente.
  if (root.contains("suprimentos")) {
    db_.execute("DELETE FROM sos_suprimentos;");
  }
  // "sosServicos" é a chave-âncora do módulo de comissões: fechamentos não
  // fazem sentido sozinhos sem os serviços que eles fecham.
  if (root.contains("sosServicos")) {
    db_.execute("DELETE FROM sos_servicos;");
    db_.execute("DELETE FROM sos_fechamentos;");
  }
  // sos_delta_sindicos (tabela antiga do lançamento manual) fica sempre
  // vazia agora — Delta Síndicos é puxado de sosServicos, não tem backup
  // próprio.
  if (root.contains("sosDashboards")) {
    db_.execute("DELETE FROM sos_dashboards_fechamento;");
  }
  // "comprasAquisicoes" é a chave-âncora do módulo Compras: pagamentos não
  // fazem sentido sem a aquisição que eles quitam.
  if (root.contains("comprasAquisicoes")) {
    db_.execute("DELETE FROM compras_parcelas;");
    db_.execute("DELETE FROM compras_pagamentos;");
    db_.execute("DELETE FROM compras_aquisicoes;");
  }
  if (root.contains("comprasOrdensOrcamento")) {
    db_.execute("DELETE FROM compras_propostas_orcamento;");
    db_.execute("DELETE FROM compras_ordens_orcamento;");
  }

  for (auto& p : root["products"]) {
    auto st = db_.prepare(
        "INSERT INTO products (id, name, unit, min_stock, category, qty, avg_cost, created_at, sku, "
        "image_path, thumbnail_path) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    st.bind(1, jstr(p, "id")).bind(2, jstr(p, "name")).bind(3, jstr(p, "unit"));
    st.bind(4, jnum(p, "minStock")).bind(5, jstr(p, "category"));
    st.bind(6, jnum(p, "qty")).bind(7, jnum(p, "avgCost")).bind(8, jstr(p, "createdAt"));
    // NULL (não "") quando o backup não trouxer SKU (formato anterior a este
    // recurso) — "" duplicado em duas linhas violaria o índice único; NULL
    // não. backfillMissingSkus (chamado no fim desta função) preenche esses.
    std::string sku = jstr(p, "sku");
    sku.empty() ? st.bindNull(9) : st.bind(9, sku);
    // Só a REFERÊNCIA volta no JSON — o arquivo .webp em si não viaja no
    // backup (item 15: nunca BLOB/base64 na tabela, e o export é só JSON).
    // Cópia de segurança das fotos é a pasta imagens/ copiada junto com
    // dados/ (item 17 do pedido) — mesmo raciocínio do backup do banco
    // inteiro, já documentado no botão "Exportar backup" da tela.
    std::string imagePath = jstr(p, "imagePath");
    std::string thumbnailPath = jstr(p, "thumbnailPath");
    imagePath.empty() ? st.bindNull(10) : st.bind(10, imagePath);
    thumbnailPath.empty() ? st.bindNull(11) : st.bind(11, thumbnailPath);
    st.step();
  }

  if (root.contains("departments")) {
    for (auto& d : root["departments"]) {
      auto st = db_.prepare(
          "INSERT INTO departments (id, name, encarregado, monthly_limit, created_at) VALUES (?, ?, ?, ?, ?)");
      st.bind(1, jstr(d, "id")).bind(2, jstr(d, "name")).bind(3, jstr(d, "encarregado"));
      st.bind(4, jnum(d, "monthlyLimit")).bind(5, jstr(d, "createdAt"));
      st.step();
    }
  }

  if (root.contains("deptCostHistory")) {
    for (auto& h : root["deptCostHistory"]) {
      std::string key = canonDeptKey(jstr(h, "dept"));
      if (key.empty()) continue;
      auto st = db_.prepare(
          "INSERT INTO dept_cost_history (year, month0, dept_key, dept_name, amount) VALUES (?, ?, ?, ?, ?) "
          "ON CONFLICT(year, month0, dept_key) DO UPDATE SET amount = amount + excluded.amount");
      st.bind(1, jnum(h, "year")).bind(2, jnum(h, "month0"));
      st.bind(3, key).bind(4, jstr(h, "dept")).bind(5, jnum(h, "amount"));
      st.step();
    }
  }

  if (root.contains("settings")) {
    for (auto& [k, v] : root["settings"].items()) {
      auto st = db_.prepare("INSERT INTO app_settings (key, value) VALUES (?, ?)");
      st.bind(1, k).bind(2, v.is_string() ? v.get<std::string>() : v.dump());
      st.step();
    }
  }

  if (root.contains("permissionGroups")) {
    for (auto& g : root["permissionGroups"]) {
      auto st = db_.prepare(
          "INSERT INTO permission_groups (id, name, description, created_at) VALUES (?, ?, ?, ?)");
      st.bind(1, jstr(g, "id")).bind(2, jstr(g, "name")).bind(3, jstr(g, "description"));
      st.bind(4, jstr(g, "createdAt"));
      st.step();
      json perms = g.contains("perms") ? g["perms"] : json::object();
      for (const auto& p : permsFromJson(perms)) {
        auto ps = db_.prepare(
            "INSERT INTO permission_group_perms (group_id, feature, can_create, can_read, can_update, "
            "can_delete) VALUES (?, ?, ?, ?, ?, ?)");
        ps.bind(1, jstr(g, "id")).bind(2, p.feature);
        ps.bind(3, p.create ? 1.0 : 0.0).bind(4, p.read ? 1.0 : 0.0);
        ps.bind(5, p.update ? 1.0 : 0.0).bind(6, p.del ? 1.0 : 0.0);
        ps.step();
      }
    }
  }

  if (root.contains("departmentPermissionGroups")) {
    for (auto& dg : root["departmentPermissionGroups"]) {
      auto st = db_.prepare("UPDATE departments SET permission_group_id=? WHERE id=?");
      st.bind(1, jstr(dg, "groupId")).bind(2, jstr(dg, "departmentId"));
      st.step();
    }
  }

  if (root.contains("users")) {
    for (auto& u : root["users"]) {
      std::string email = jstr(u, "email");
      std::string emailKey = jstr(u, "emailKey");
      if (emailKey.empty()) {
        emailKey = email;
        for (char& c : emailKey) {
          if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
      }
      auto st = db_.prepare(
          "INSERT INTO users (id, email, email_key, name, role, department_id, active, password_hash, "
          "password_salt, password_iterations, created_at, last_login_at) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(u, "id")).bind(2, email).bind(3, emailKey).bind(4, jstr(u, "name"));
      st.bind(5, jstr(u, "role", kRoleUsuario));
      jstr(u, "departmentId").empty() ? st.bindNull(6) : st.bind(6, jstr(u, "departmentId"));
      st.bind(7, jbool(u, "active", true) ? 1.0 : 0.0);
      st.bind(8, jstr(u, "passwordHash")).bind(9, jstr(u, "passwordSalt"));
      st.bind(10, jnum(u, "passwordIterations"));
      st.bind(11, jstr(u, "createdAt"));
      jstr(u, "lastLoginAt").empty() ? st.bindNull(12) : st.bind(12, jstr(u, "lastLoginAt"));
      st.step();
    }
  }

  if (root.contains("requests")) {
    for (auto& r : root["requests"]) {
      auto st = db_.prepare(
          "INSERT INTO requests (id, department_id, department_name, requester_user_id, requester_name, "
          "status, obs, created_at, decided_at, decided_by_name, decision_note, delivered_at, "
          "delivered_by_name, edited_at, edited_by_name) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(r, "id"));
      jstr(r, "departmentId").empty() ? st.bindNull(2) : st.bind(2, jstr(r, "departmentId"));
      st.bind(3, jstr(r, "departmentName"));
      jstr(r, "requesterUserId").empty() ? st.bindNull(4) : st.bind(4, jstr(r, "requesterUserId"));
      st.bind(5, jstr(r, "requesterName"));
      st.bind(6, jstr(r, "status", "pendente")).bind(7, jstr(r, "obs")).bind(8, jstr(r, "createdAt"));
      jstr(r, "decidedAt").empty() ? st.bindNull(9) : st.bind(9, jstr(r, "decidedAt"));
      jstr(r, "decidedByName").empty() ? st.bindNull(10) : st.bind(10, jstr(r, "decidedByName"));
      jstr(r, "decisionNote").empty() ? st.bindNull(11) : st.bind(11, jstr(r, "decisionNote"));
      jstr(r, "deliveredAt").empty() ? st.bindNull(12) : st.bind(12, jstr(r, "deliveredAt"));
      jstr(r, "deliveredByName").empty() ? st.bindNull(13) : st.bind(13, jstr(r, "deliveredByName"));
      jstr(r, "editedAt").empty() ? st.bindNull(14) : st.bind(14, jstr(r, "editedAt"));
      jstr(r, "editedByName").empty() ? st.bindNull(15) : st.bind(15, jstr(r, "editedByName"));
      st.step();

      if (!r.contains("items")) continue;
      for (auto& it : r["items"]) {
        auto is = db_.prepare(
            "INSERT INTO request_items (id, request_id, product_id, product_name, unit, qty, movement_id) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)");
        is.bind(1, jstr(it, "id")).bind(2, jstr(r, "id")).bind(3, jstr(it, "productId"));
        is.bind(4, jstr(it, "productName")).bind(5, jstr(it, "unit")).bind(6, jnum(it, "qty"));
        jstr(it, "movementId").empty() ? is.bindNull(7) : is.bind(7, jstr(it, "movementId"));
        is.step();
      }
    }
  }

  if (root.contains("requestWindows")) {
    for (auto& w : root["requestWindows"]) {
      auto st = db_.prepare(
          "INSERT INTO request_windows (id, opens_at, closes_at, obs, created_at, created_by_user_id, "
          "created_by_name, closed_at, closed_by_user_id, closed_by_name) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(w, "id")).bind(2, jstr(w, "opensAt")).bind(3, jstr(w, "closesAt"));
      jstr(w, "obs").empty() ? st.bindNull(4) : st.bind(4, jstr(w, "obs"));
      st.bind(5, jstr(w, "createdAt"));
      jstr(w, "createdByUserId").empty() ? st.bindNull(6) : st.bind(6, jstr(w, "createdByUserId"));
      st.bind(7, jstr(w, "createdByName"));
      jstr(w, "closedAt").empty() ? st.bindNull(8) : st.bind(8, jstr(w, "closedAt"));
      jstr(w, "closedByUserId").empty() ? st.bindNull(9) : st.bind(9, jstr(w, "closedByUserId"));
      jstr(w, "closedByName").empty() ? st.bindNull(10) : st.bind(10, jstr(w, "closedByName"));
      st.step();
    }
  }

  // Fornecedores e prestadores — especialidades antes das empresas, e as
  // marcações por último (as FKs do schema v9 exigem essa ordem).
  if (root.contains("especialidades")) {
    for (auto& e : root["especialidades"]) {
      auto st = db_.prepare(
          "INSERT INTO especialidades (id, setor, nome, created_at) VALUES (?, ?, ?, ?)");
      st.bind(1, jstr(e, "id")).bind(2, jstr(e, "setor")).bind(3, jstr(e, "nome"));
      st.bind(4, jstr(e, "createdAt"));
      st.step();
    }
  }
  if (root.contains("empresas")) {
    for (auto& e : root["empresas"]) {
      auto st = db_.prepare(
          "INSERT INTO empresas (id, nome, nome_fantasia, cnpj, cnpj_key, endereco, numero, "
          "complemento, bairro, cep, cidade, estado, telefone, emails, observacoes, parceira, "
          "created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(e, "id")).bind(2, jstr(e, "nome"));
      jstr(e, "nomeFantasia").empty() ? st.bindNull(3) : st.bind(3, jstr(e, "nomeFantasia"));
      jstr(e, "cnpj").empty() ? st.bindNull(4) : st.bind(4, jstr(e, "cnpj"));
      canonCnpj(jstr(e, "cnpj")).empty() ? st.bindNull(5) : st.bind(5, canonCnpj(jstr(e, "cnpj")));
      st.bind(6, jstr(e, "endereco")).bind(7, jstr(e, "numero")).bind(8, jstr(e, "complemento"));
      st.bind(9, jstr(e, "bairro")).bind(10, jstr(e, "cep")).bind(11, jstr(e, "cidade"));
      st.bind(12, jstr(e, "estado")).bind(13, jstr(e, "telefone")).bind(14, jstr(e, "emails"));
      st.bind(15, jstr(e, "observacoes")).bind(16, jbool(e, "parceira") ? 1.0 : 0.0);
      st.bind(17, jstr(e, "createdAt"));
      st.step();

      if (!e.contains("especialidadeIds")) continue;
      for (auto& id : e["especialidadeIds"]) {
        if (!id.is_string()) continue;
        auto is = db_.prepare(
            "INSERT OR IGNORE INTO empresa_especialidades (empresa_id, especialidade_id) "
            "VALUES (?, ?)");
        is.bind(1, jstr(e, "id")).bind(2, id.get<std::string>());
        is.step();
      }
    }
  }

  // Gestão de prazos — ordem pai→filho (condomínio/tipo de serviço antes do
  // vínculo, vínculo antes da renovação), exigida pelas FKs do schema v8.
  if (root.contains("condominios")) {
    for (auto& c : root["condominios"]) {
      auto st = db_.prepare(
          "INSERT INTO condominios (id, nome, nome_fantasia, cnpj, codigo, endereco, numero, "
          "complemento, bairro, cidade, estado, cep, localizacao, sindico, telefone, email, "
          "observacoes, ativo, delta_sindica, created_at) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(c, "id")).bind(2, jstr(c, "nome")).bind(3, jstr(c, "nomeFantasia"));
      st.bind(4, jstr(c, "cnpj")).bind(5, jstr(c, "codigo")).bind(6, jstr(c, "endereco"));
      st.bind(7, jstr(c, "numero")).bind(8, jstr(c, "complemento")).bind(9, jstr(c, "bairro"));
      st.bind(10, jstr(c, "cidade")).bind(11, jstr(c, "estado")).bind(12, jstr(c, "cep"));
      jstr(c, "localizacao").empty() ? st.bindNull(13) : st.bind(13, jstr(c, "localizacao"));
      st.bind(14, jstr(c, "sindico")).bind(15, jstr(c, "telefone")).bind(16, jstr(c, "email"));
      st.bind(17, jstr(c, "observacoes"));
      st.bind(18, jbool(c, "ativo", true) ? 1.0 : 0.0);
      st.bind(19, jbool(c, "deltaSindica", false) ? 1.0 : 0.0);
      st.bind(20, jstr(c, "createdAt"));
      st.step();
    }
  }
  if (root.contains("tiposServico")) {
    for (auto& t : root["tiposServico"]) {
      auto st = db_.prepare(
          "INSERT INTO tipos_servico (id, nome, prazo_dias, cor, created_at) VALUES (?, ?, ?, ?, ?)");
      st.bind(1, jstr(t, "id")).bind(2, jstr(t, "nome")).bind(3, jnum(t, "prazoDias"));
      st.bind(4, jstr(t, "cor", "#2e6ba6")).bind(5, jstr(t, "createdAt"));
      st.step();
    }
  }
  if (root.contains("servicosCondominio")) {
    for (auto& s : root["servicosCondominio"]) {
      auto st = db_.prepare(
          "INSERT INTO servicos_condominio (id, condominio_id, tipo_servico_id, data_ultima_renovacao, "
          "empresa_contratada, observacoes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(s, "id")).bind(2, jstr(s, "condominioId")).bind(3, jstr(s, "tipoServicoId"));
      st.bind(4, jstr(s, "dataUltimaRenovacao")).bind(5, jstr(s, "empresaContratada"));
      st.bind(6, jstr(s, "observacoes")).bind(7, jstr(s, "createdAt"));
      st.step();
    }
  }
  if (root.contains("renovacoes")) {
    for (auto& r : root["renovacoes"]) {
      auto st = db_.prepare(
          "INSERT INTO renovacoes (id, servico_condominio_id, data_renovacao, empresa_contratada, "
          "prazo_dias_aplicado, observacoes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(r, "id")).bind(2, jstr(r, "servicoCondominioId")).bind(3, jstr(r, "dataRenovacao"));
      st.bind(4, jstr(r, "empresaContratada")).bind(5, jnum(r, "prazoDiasAplicado"));
      st.bind(6, jstr(r, "observacoes")).bind(7, jstr(r, "createdAt"));
      st.step();
    }
  }

  // Gestão SOS: gerentes DEPOIS de condomínios (a carteira referencia
  // condominio_id, e a FK exige que a linha já exista).
  if (root.contains("gerentes")) {
    for (auto& g : root["gerentes"]) {
      // "numero" viaja explícito quando o backup já o tem (mesmo motivo de
      // sos_servicos.numero abaixo). Um backup ANTERIOR a este campo não o
      // traz — cada gerente sem número ganha um novo, na ordem do backup.
      int numero = static_cast<int>(jnum(g, "numero"));
      if (numero <= 0) numero = estoque::nextGerenteNumero(db_);
      auto st = db_.prepare(
          "INSERT INTO gerentes (id, numero, nome, telefone, email, chave_pix, observacoes, "
          "created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(g, "id")).bind(2, static_cast<double>(numero)).bind(3, jstr(g, "nome"));
      st.bind(4, jstr(g, "telefone")).bind(5, jstr(g, "email")).bind(6, jstr(g, "chavePix"));
      st.bind(7, jstr(g, "observacoes")).bind(8, jstr(g, "createdAt"));
      st.step();

      if (!g.contains("condominioIds")) continue;
      for (auto& id : g["condominioIds"]) {
        if (!id.is_string()) continue;
        auto is = db_.prepare(
            "INSERT OR IGNORE INTO gerente_condominios (gerente_id, condominio_id) VALUES (?, ?)");
        is.bind(1, jstr(g, "id")).bind(2, id.get<std::string>());
        is.step();
      }
    }
  }

  // Gestão SOS: suprimentos. "numero" viaja explícito (mesmo critério de
  // sos_servicos.numero abaixo) — AUTOINCREMENT de verdade aqui, então o
  // valor gravado ganha prioridade sobre o que o SQLite atribuiria sozinho.
  if (root.contains("suprimentos")) {
    for (auto& s : root["suprimentos"]) {
      auto st = db_.prepare(
          "INSERT INTO sos_suprimentos (numero, id, nome, categoria, telefone, email, chave_pix, "
          "observacoes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jnum(s, "numero")).bind(2, jstr(s, "id")).bind(3, jstr(s, "nome"));
      st.bind(4, jstr(s, "categoria")).bind(5, jstr(s, "telefone")).bind(6, jstr(s, "email"));
      st.bind(7, jstr(s, "chavePix")).bind(8, jstr(s, "observacoes")).bind(9, jstr(s, "createdAt"));
      st.step();
    }
  }

  // Gestão SOS: fechamentos ANTES dos serviços (fechamento_id referencia
  // sos_fechamentos, e a FK exige que a linha já exista).
  if (root.contains("sosFechamentos")) {
    for (auto& f : root["sosFechamentos"]) {
      auto st = db_.prepare(
          "INSERT INTO sos_fechamentos (id, mes_referencia, quantidade_servicos, total_venda, "
          "total_comissao, observacoes, fechado_em, reaberto_em, created_at) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(f, "id")).bind(2, jstr(f, "mesReferencia"));
      st.bind(3, jnum(f, "quantidadeServicos")).bind(4, jnum(f, "totalVenda"));
      st.bind(5, jnum(f, "totalComissao")).bind(6, jstr(f, "observacoes"));
      st.bind(7, jstr(f, "fechadoEm"));
      jstr(f, "reabertoEm").empty() ? st.bindNull(8) : st.bind(8, jstr(f, "reabertoEm"));
      st.bind(9, jstr(f, "createdAt"));
      st.step();
    }
  }
  if (root.contains("sosServicos")) {
    for (auto& s : root["sosServicos"]) {
      // "numero" viaja explícito para preservar o "ID" da planilha através de
      // um backup/restore — sem isso, o AUTOINCREMENT recomeçaria do zero.
      auto st = db_.prepare(
          "INSERT INTO sos_servicos (numero, id, codigo, condominio_id, condominio_nome, gerente_id, "
          "gerente_nome, parceiro_id, parceiro_nome, venda, porcentagem, data_referencia, "
          "fechamento_id, observacoes, created_at, pago, data_pagamento) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jnum(s, "numero")).bind(2, jstr(s, "id")).bind(3, jstr(s, "codigo"));
      st.bind(4, jstr(s, "condominioId")).bind(5, jstr(s, "condominioNome"));
      jstr(s, "gerenteId").empty() ? st.bindNull(6) : st.bind(6, jstr(s, "gerenteId"));
      jstr(s, "gerenteNome").empty() ? st.bindNull(7) : st.bind(7, jstr(s, "gerenteNome"));
      jstr(s, "parceiroId").empty() ? st.bindNull(8) : st.bind(8, jstr(s, "parceiroId"));
      jstr(s, "parceiroNome").empty() ? st.bindNull(9) : st.bind(9, jstr(s, "parceiroNome"));
      st.bind(10, jnum(s, "venda")).bind(11, jnum(s, "porcentagem")).bind(12, jstr(s, "dataReferencia"));
      jstr(s, "fechamentoId").empty() ? st.bindNull(13) : st.bind(13, jstr(s, "fechamentoId"));
      st.bind(14, jstr(s, "observacoes")).bind(15, jstr(s, "createdAt"));
      st.bind(16, (s.contains("pago") && s["pago"].is_boolean() && s["pago"].get<bool>()) ? 1 : 0);
      jstr(s, "dataPagamento").empty() ? st.bindNull(17) : st.bind(17, jstr(s, "dataPagamento"));
      st.step();
    }
  }
  // Backups antigos podem trazer "sosDeltaSindicos" (lançamento manual, já
  // descontinuado) — ignorado de propósito: sos_delta_sindicos fica vazia, e
  // o retrato certo volta sozinho de sosServicos + condominios.deltaSindica.
  if (root.contains("sosDashboards")) {
    for (auto& d : root["sosDashboards"]) {
      auto st = db_.prepare(
          "INSERT INTO sos_dashboards_fechamento (id, mes_referencia, dados_json, observacoes, "
          "gerado_em, created_at) VALUES (?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(d, "id")).bind(2, jstr(d, "mesReferencia")).bind(3, jstr(d, "dadosJson"));
      st.bind(4, jstr(d, "observacoes")).bind(5, jstr(d, "geradoEm")).bind(6, jstr(d, "createdAt"));
      st.step();
    }
  }
  if (root.contains("sosConfig")) {
    for (auto& [k, v] : root["sosConfig"].items()) {
      estoque::setConfig(db_, k, v.is_string() ? v.get<std::string>() : v.dump());
    }
  }

  // Compras: aquisições ANTES de pagamentos (FK exige a linha já existir);
  // orçamentos são independentes, sem ordem que importe aqui.
  if (root.contains("comprasAquisicoes")) {
    for (auto& a : root["comprasAquisicoes"]) {
      auto st = db_.prepare(
          "INSERT INTO compras_aquisicoes (id, fornecedor_id, fornecedor_nome, descricao, "
          "nota_fiscal, valor, data_compra, observacoes, created_at, anexo_path, anexo_tipo) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(a, "id")).bind(2, jstr(a, "fornecedorId")).bind(3, jstr(a, "fornecedorNome"));
      st.bind(4, jstr(a, "descricao"));
      jstr(a, "notaFiscal").empty() ? st.bindNull(5) : st.bind(5, jstr(a, "notaFiscal"));
      st.bind(6, jnum(a, "valor")).bind(7, jstr(a, "dataCompra"));
      st.bind(8, jstr(a, "observacoes")).bind(9, jstr(a, "createdAt"));
      jstr(a, "anexoPath").empty() ? st.bindNull(10) : st.bind(10, jstr(a, "anexoPath"));
      jstr(a, "anexoTipo").empty() ? st.bindNull(11) : st.bind(11, jstr(a, "anexoTipo"));
      st.step();
    }
  }
  if (root.contains("comprasOrdensOrcamento")) {
    for (auto& o : root["comprasOrdensOrcamento"]) {
      // "numero" viaja explícito pelo mesmo motivo de sos_servicos acima.
      auto st = db_.prepare(
          "INSERT INTO compras_ordens_orcamento (numero, id, condominio_id, condominio_nome, descricao, "
          "observacoes, status, proposta_recomendada_id, proposta_aprovada_id, data_solicitacao, "
          "data_envio_cliente, data_aprovacao, reaberto_em, created_at) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jnum(o, "numero")).bind(2, jstr(o, "id"));
      jstr(o, "condominioId").empty() ? st.bindNull(3) : st.bind(3, jstr(o, "condominioId"));
      st.bind(4, jstr(o, "condominioNome")).bind(5, jstr(o, "descricao"));
      st.bind(6, jstr(o, "observacoes")).bind(7, jstr(o, "status", ordem_orcamento_status::kPendente));
      jstr(o, "propostaRecomendadaId").empty() ? st.bindNull(8) : st.bind(8, jstr(o, "propostaRecomendadaId"));
      jstr(o, "propostaAprovadaId").empty() ? st.bindNull(9) : st.bind(9, jstr(o, "propostaAprovadaId"));
      jstr(o, "dataSolicitacao").empty() ? st.bindNull(10) : st.bind(10, jstr(o, "dataSolicitacao"));
      jstr(o, "dataEnvioCliente").empty() ? st.bindNull(11) : st.bind(11, jstr(o, "dataEnvioCliente"));
      jstr(o, "dataAprovacao").empty() ? st.bindNull(12) : st.bind(12, jstr(o, "dataAprovacao"));
      jstr(o, "reabertoEm").empty() ? st.bindNull(13) : st.bind(13, jstr(o, "reabertoEm"));
      st.bind(14, jstr(o, "createdAt"));
      st.step();

      if (o.contains("propostas") && o["propostas"].is_array()) {
        for (auto& p : o["propostas"]) {
          auto stp = db_.prepare(
              "INSERT INTO compras_propostas_orcamento (id, ordem_id, empresa_id, empresa_nome, valor, "
              "anexo_path, anexo_tipo, email_enviado_em, recomendada, created_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
          stp.bind(1, jstr(p, "id")).bind(2, jstr(o, "id")).bind(3, jstr(p, "empresaId"));
          stp.bind(4, jstr(p, "empresaNome"));
          double valor = jnum(p, "valor", -1);
          valor < 0 ? stp.bindNull(5) : stp.bind(5, valor);
          jstr(p, "anexoPath").empty() ? stp.bindNull(6) : stp.bind(6, jstr(p, "anexoPath"));
          jstr(p, "anexoTipo").empty() ? stp.bindNull(7) : stp.bind(7, jstr(p, "anexoTipo"));
          jstr(p, "emailEnviadoEm").empty() ? stp.bindNull(8) : stp.bind(8, jstr(p, "emailEnviadoEm"));
          stp.bind(9, jbool(p, "recomendada") ? 1.0 : 0.0).bind(10, jstr(p, "createdAt"));
          stp.step();
        }
      }
    }
  }
  if (root.contains("comprasPagamentos")) {
    for (auto& p : root["comprasPagamentos"]) {
      auto st = db_.prepare(
          "INSERT INTO compras_pagamentos (id, aquisicao_id, nota_fiscal, valor_total, "
          "data_emissao, observacoes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
      st.bind(1, jstr(p, "id")).bind(2, jstr(p, "aquisicaoId")).bind(3, jstr(p, "notaFiscal"));
      st.bind(4, jnum(p, "valorTotal")).bind(5, jstr(p, "dataEmissao"));
      st.bind(6, jstr(p, "observacoes")).bind(7, jstr(p, "createdAt"));
      st.step();

      if (!p.contains("parcelas")) continue;
      for (auto& parc : p["parcelas"]) {
        auto ps = db_.prepare(
            "INSERT INTO compras_parcelas (id, pagamento_id, numero, valor, vencimento, pago, "
            "data_pagamento, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        ps.bind(1, jstr(parc, "id")).bind(2, jstr(p, "id")).bind(3, jnum(parc, "numero"));
        ps.bind(4, jnum(parc, "valor")).bind(5, jstr(parc, "vencimento"));
        bool pago = parc.contains("pago") && parc["pago"].is_boolean() && parc["pago"].get<bool>();
        ps.bind(6, pago ? 1.0 : 0.0);
        (pago && !jstr(parc, "dataPagamento").empty()) ? ps.bind(7, jstr(parc, "dataPagamento"))
                                                        : ps.bindNull(7);
        ps.bind(8, jstr(parc, "createdAt"));
        ps.step();
      }
    }
  }

  for (auto& m : root["movements"]) {
    auto st = db_.prepare(
        "INSERT INTO movements (id, type, product_id, qty, unit_price, supplier, nf, department_id, "
        "recipient, encarregado, requester, obs, date, resulting_qty, resulting_avg_cost, created_at, "
        "new_avg_cost) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    st.bind(1, jstr(m, "id")).bind(2, jstr(m, "type")).bind(3, jstr(m, "productId"));
    st.bind(4, jnum(m, "qty")).bind(5, jnum(m, "unitPrice"));
    st.bind(6, jstr(m, "supplier")).bind(7, jstr(m, "nf")).bind(8, jstr(m, "departmentId"));
    st.bind(9, jstr(m, "recipient")).bind(10, jstr(m, "encarregado")).bind(11, jstr(m, "requester"));
    st.bind(12, jstr(m, "obs")).bind(13, jstr(m, "date"));
    st.bind(14, jnum(m, "resultingQty")).bind(15, jnum(m, "resultingAvgCost")).bind(16, jstr(m, "createdAt"));
    double newAvgCost = jnum(m, "newAvgCost");
    newAvgCost > 0 ? st.bind(17, newAvgCost) : st.bindNull(17);
    st.step();
  }

  tx.commit();

  // Fora da transação de restauração (idempotente, com sua própria
  // transação por produto — ver o comentário na declaração): garante que um
  // backup anterior a este recurso (produtos sem "sku" no JSON, gravados
  // como NULL acima) saia da restauração já com todo mundo com SKU, sem
  // esperar o app reabrir. Mesmo raciocínio pra categoria: um backup
  // anterior ao recurso de categorias fixas trazia texto livre.
  estoque::backfillMissingSkus(db_);
  estoque::classifyLegacyCategories(db_);
  // Mesmo raciocínio: um backup pode trazer products.avg_cost gravado direto
  // da planilha de origem, divergente do que os movements dele mesmo
  // reproduziriam pelo razão — sem isso, a tela de Produtos e o Relatório/
  // Retrospecto mostrariam custos diferentes para o mesmo item logo após
  // importar (ver o comentário longo de reconcileAvgCost).
  estoque::reconcileAvgCost(db_);

  // Depois de trocar o banco inteiro, a sessão pode estar apontando para um
  // usuário que não existe mais no arquivo importado. Reancorá-la pelo e-mail
  // (ou derrubá-la) evita continuar operando com permissões de um cadastro
  // que sumiu.
  ensureDefaultSuperadmin(db_, time_utils::systemNowIso());
  if (currentUser_ && !serviceSession_) {
    auto again = findUserByEmail(db_, currentUser_->email);
    if (again && again->active) {
      currentUser_ = again;
    } else {
      currentUser_.reset();
    }
  }
}

}  // namespace estoque
