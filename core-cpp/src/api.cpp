#include "estoque/api.hpp"

#include <nlohmann/json.hpp>

#include "estoque/inventory_engine.hpp"
#include "estoque/report_engine.hpp"
#include "estoque/request_engine.hpp"
#include "estoque/retrospect_engine.hpp"
#include "estoque/time_utils.hpp"

namespace estoque {

using json = nlohmann::json;

namespace {

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
                   {"name", a.name},
                   {"unit", a.unit},
                   {"category", a.category},
                   {"qty", a.qty},
                   {"reserved", a.reserved},
                   {"available", a.available},
                   {"avgCost", a.avgCost},
                   {"minStock", a.minStock}});
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
                            const std::string& motivo, const std::string& date, const std::string& createdAt) {
  requireAny({{features::kProdutos, PermAction::Update}, {features::kLinhaDoTempo, PermAction::Create}});
  return estoque::applyCorrecao(db_, movementId, productId, qtyReal, motivo, date, createdAt);
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
      "encarregado, requester, obs, date, resulting_qty, resulting_avg_cost, created_at "
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

  json root;
  root["products"] = products;
  root["movements"] = movements;
  root["departments"] = departments;
  root["deptCostHistory"] = json::parse(estoque::exportDeptCostHistoryJson(db_));
  root["settings"] = settings;
  root["users"] = users;
  root["permissionGroups"] = groups;
  root["departmentPermissionGroups"] = deptGroups;
  root["requests"] = requests;
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

  for (auto& p : root["products"]) {
    auto st = db_.prepare(
        "INSERT INTO products (id, name, unit, min_stock, category, qty, avg_cost, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    st.bind(1, jstr(p, "id")).bind(2, jstr(p, "name")).bind(3, jstr(p, "unit"));
    st.bind(4, jnum(p, "minStock")).bind(5, jstr(p, "category"));
    st.bind(6, jnum(p, "qty")).bind(7, jnum(p, "avgCost")).bind(8, jstr(p, "createdAt"));
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
          "delivered_by_name) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
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

  for (auto& m : root["movements"]) {
    auto st = db_.prepare(
        "INSERT INTO movements (id, type, product_id, qty, unit_price, supplier, nf, department_id, "
        "recipient, encarregado, requester, obs, date, resulting_qty, resulting_avg_cost, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    st.bind(1, jstr(m, "id")).bind(2, jstr(m, "type")).bind(3, jstr(m, "productId"));
    st.bind(4, jnum(m, "qty")).bind(5, jnum(m, "unitPrice"));
    st.bind(6, jstr(m, "supplier")).bind(7, jstr(m, "nf")).bind(8, jstr(m, "departmentId"));
    st.bind(9, jstr(m, "recipient")).bind(10, jstr(m, "encarregado")).bind(11, jstr(m, "requester"));
    st.bind(12, jstr(m, "obs")).bind(13, jstr(m, "date"));
    st.bind(14, jnum(m, "resultingQty")).bind(15, jnum(m, "resultingAvgCost")).bind(16, jstr(m, "createdAt"));
    st.step();
  }

  tx.commit();

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
