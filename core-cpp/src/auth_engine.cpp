#include "estoque/auth_engine.hpp"

#include <algorithm>
#include <stdexcept>

#include "estoque/crypto.hpp"

namespace estoque {

namespace {

// Catálogo das funções sujeitas a permissão. É a ÚNICA definição do que cada
// letra do CRUD significa em cada tela: a UI de Permissões lê estes rótulos
// pela ponte em vez de repetir a regra em JavaScript. Rótulo vazio = a ação
// não existe naquela função (a tela desabilita a caixinha).
const std::vector<FeatureInfo> kFeatureCatalog = {
    {features::kRelatorioMensal, "Relatório Mensal",
     "Exportar CSV e imprimir", "Abrir a tela e ver os valores", "", ""},
    {features::kRetrospecto, "Retrospecto",
     "Exportar CSV e imprimir", "Abrir a tela e ver os valores",
     "Salvar premissas do teto e aplicar como limite dos setores", ""},
    {features::kProdutos, "Produtos",
     "Cadastrar produto e registrar entrada", "Ver o catálogo e a posição de estoque",
     "Editar produto e corrigir estoque", "Excluir produto"},
    {features::kLinhaDoTempo, "Linha do Tempo",
     "Registrar lançamentos", "Ver as movimentações", "Editar um lançamento",
     "Excluir um lançamento"},
    {features::kDepartamentos, "Departamentos",
     "Criar departamento", "Ver a lista", "Editar departamento", "Excluir departamento"},
    {features::kImportarExportar, "Importar e exportar",
     "Exportar backup", "Abrir a tela", "Importar backup (substitui todos os dados)", ""},
    {features::kRequisicoes, "Requisições",
     "Solicitar materiais", "Ver o histórico de pedidos do próprio departamento",
     "Aprovar, rejeitar e confirmar entrega (o superadmin já pode, por padrão ninguém mais)",
     "Cancelar um pedido em aberto"},
    {features::kCondominios, "Condomínios",
     "Cadastrar condomínio", "Ver a lista de condomínios",
     "Editar condomínio", "Excluir condomínio"},
    {features::kEmpresas, "Fornecedores e Prestadores",
     "Cadastrar empresa e criar especialidade na Setorização",
     "Abrir o Catálogo, a Setorização e o Cadastro",
     "Editar empresa e especialidade", "Excluir empresa e especialidade"},
    {features::kGestaoDatas, "Gestão de Prazos",
     "Cadastrar tipo de serviço e vincular um serviço a um condomínio",
     "Ver vínculos, vencimentos e o histórico de renovações",
     "Editar tipo de serviço, editar vínculo e registrar renovação",
     "Excluir tipo de serviço e vínculo"},
    {features::kGerentes, "Gerentes e Carteiras",
     "Cadastrar gerente",
     "Ver a lista de gerentes e a carteira (condomínios) de cada um",
     "Editar gerente e alterar os condomínios da carteira",
     "Excluir gerente"},
    {features::kGestaoSosServicos, "Gestão SOS: Serviços e Comissões",
     "Lançar um novo serviço/venda",
     "Ver a planilha de serviços, o histórico de fechamentos, o Painel e as configurações",
     "Editar serviço em aberto, fechar o mês, reabrir um fechamento e alterar as configurações",
     "Excluir serviço em aberto"},
    {features::kSuprimentos, "Gestão SOS: Suprimentos",
     "Cadastrar suprimento", "Ver a lista de suprimentos",
     "Editar suprimento", "Excluir suprimento"},
    {features::kDeltaSindicos, "Gestão SOS: Delta Síndicos",
     "", "Ver a planilha de Delta Síndicos, puxada automaticamente de Serviços (marcar/desmarcar condomínio atendido pela Delta é feito em Condomínios)",
     "", ""},
    {features::kAquisicoes, "Compras: Aquisições FL",
     "Lançar uma aquisição", "Ver a lista de aquisições",
     "Editar aquisição", "Excluir aquisição (sem pagamentos lançados)"},
    {features::kOrcamentos, "Compras: Orçamentos",
     "Lançar um orçamento", "Ver a lista de orçamentos",
     "Editar orçamento e mudar o status (aprovado/recusado)", "Excluir orçamento"},
    {features::kPagamentos, "Compras: Acompanhamento de pagamentos",
     "Lançar uma NF com as parcelas", "Ver os pagamentos e o status de cada parcela",
     "Editar pagamento e marcar parcela como paga", "Excluir pagamento"},
};

std::string lowerAscii(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

constexpr const char* kUserCols =
    "id, name, email, role, department_id, active, created_at, last_login_at";

User rowToUser(Statement& st) {
  User u;
  u.id = st.columnText(0);
  u.name = st.columnText(1);
  u.email = st.columnText(2);
  u.role = st.columnText(3);
  u.departmentId = st.columnIsNull(4) ? "" : st.columnText(4);
  u.active = st.columnDouble(5) != 0;
  u.createdAt = st.columnText(6);
  u.lastLoginAt = st.columnIsNull(7) ? "" : st.columnText(7);
  return u;
}

void validateEmail(const std::string& email) {
  auto at = email.find('@');
  if (at == std::string::npos || at == 0 || at + 1 >= email.size() ||
      email.find('.', at) == std::string::npos || email.find(' ') != std::string::npos) {
    throw std::invalid_argument("e-mail inválido: '" + email + "'");
  }
}

void validateRole(const std::string& role) {
  if (role != kRoleSuperadmin && role != kRoleUsuario) {
    throw std::invalid_argument("papel inválido: '" + role + "'");
  }
}

int countActiveSuperadmins(Database& db, const std::string& exceptUserId) {
  auto st = db.prepare("SELECT COUNT(*) FROM users WHERE role='superadmin' AND active=1 AND id<>?");
  st.bind(1, exceptUserId);
  st.step();
  return static_cast<int>(st.columnDouble(0));
}

void writePassword(Database& db, const std::string& userId, const std::string& password) {
  validatePassword(password);
  std::string saltHex = crypto::randomHex(16);
  std::string hash = crypto::toHex(crypto::pbkdf2Sha256(password, crypto::fromHex(saltHex),
                                                        crypto::kDefaultIterations, 32));
  auto st = db.prepare(
      "UPDATE users SET password_hash=?, password_salt=?, password_iterations=? WHERE id=?");
  st.bind(1, hash).bind(2, saltHex).bind(3, static_cast<double>(crypto::kDefaultIterations)).bind(4, userId);
  st.step();
}

// Linha "tudo desligado" para uma função — base de qualquer matriz.
FeaturePermissions emptyPerm(const std::string& feature) {
  FeaturePermissions p;
  p.feature = feature;
  return p;
}

std::vector<FeaturePermissions> loadGroupPerms(Database& db, const std::string& groupId) {
  std::vector<FeaturePermissions> out;
  for (const auto& f : kFeatureCatalog) out.push_back(emptyPerm(f.key));

  auto st = db.prepare(
      "SELECT feature, can_create, can_read, can_update, can_delete FROM permission_group_perms "
      "WHERE group_id=?");
  st.bind(1, groupId);
  while (st.step()) {
    std::string feature = st.columnText(0);
    auto it = std::find_if(out.begin(), out.end(), [&](const FeaturePermissions& p) {
      return p.feature == feature;
    });
    if (it == out.end()) continue;  // função removida do catálogo: linha antiga é ignorada
    it->create = st.columnDouble(1) != 0;
    it->read = st.columnDouble(2) != 0;
    it->update = st.columnDouble(3) != 0;
    it->del = st.columnDouble(4) != 0;
  }
  return out;
}

void writeGroupPerms(Database& db, const std::string& groupId,
                     const std::vector<FeaturePermissions>& perms) {
  db.prepare("DELETE FROM permission_group_perms WHERE group_id=?").bind(1, groupId).step();
  for (const auto& p : perms) {
    if (!isKnownFeature(p.feature)) continue;
    auto st = db.prepare(
        "INSERT INTO permission_group_perms (group_id, feature, can_create, can_read, can_update, "
        "can_delete) VALUES (?, ?, ?, ?, ?, ?)");
    st.bind(1, groupId).bind(2, p.feature);
    st.bind(3, p.create ? 1.0 : 0.0).bind(4, p.read ? 1.0 : 0.0);
    st.bind(5, p.update ? 1.0 : 0.0).bind(6, p.del ? 1.0 : 0.0);
    st.step();
  }
}

}  // namespace

// ------------------------------------------------------------- catálogo

std::string permActionToString(PermAction a) {
  switch (a) {
    case PermAction::Create: return "criar";
    case PermAction::Read: return "ver";
    case PermAction::Update: return "editar";
    case PermAction::Delete: return "excluir";
  }
  return "?";
}

const std::vector<FeatureInfo>& featureCatalog() { return kFeatureCatalog; }

bool isKnownFeature(const std::string& feature) {
  return std::any_of(kFeatureCatalog.begin(), kFeatureCatalog.end(),
                     [&](const FeatureInfo& f) { return f.key == feature; });
}

bool FeaturePermissions::has(PermAction a) const {
  switch (a) {
    case PermAction::Create: return create;
    case PermAction::Read: return read;
    case PermAction::Update: return update;
    case PermAction::Delete: return del;
  }
  return false;
}

void FeaturePermissions::set(PermAction a, bool v) {
  switch (a) {
    case PermAction::Create: create = v; break;
    case PermAction::Read: read = v; break;
    case PermAction::Update: update = v; break;
    case PermAction::Delete: del = v; break;
  }
}

// -------------------------------------------------------------- usuários

void validatePassword(const std::string& password) {
  if (password.size() < 8) {
    throw std::invalid_argument("a senha precisa ter pelo menos 8 caracteres");
  }
  if (password.size() > 200) {
    throw std::invalid_argument("a senha é longa demais (máximo 200 caracteres)");
  }
}

std::vector<User> listUsers(Database& db) {
  std::vector<User> out;
  auto st = db.prepare(std::string("SELECT ") + kUserCols + " FROM users ORDER BY name");
  while (st.step()) out.push_back(rowToUser(st));
  return out;
}

std::optional<User> findUser(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kUserCols + " FROM users WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToUser(st);
}

std::optional<User> findUserByEmail(Database& db, const std::string& email) {
  auto st = db.prepare(std::string("SELECT ") + kUserCols + " FROM users WHERE email_key=?");
  st.bind(1, lowerAscii(trim(email)));
  if (!st.step()) return std::nullopt;
  return rowToUser(st);
}

User createUser(Database& db, const UserInput& input) {
  std::string name = trim(input.name);
  std::string email = trim(input.email);
  if (name.empty()) throw std::invalid_argument("informe o nome do usuário");
  validateEmail(email);
  validateRole(input.role);
  validatePassword(input.password);
  if (findUserByEmail(db, email)) {
    throw std::invalid_argument("já existe um usuário com o e-mail " + email);
  }

  Transaction tx(db);
  auto st = db.prepare(
      "INSERT INTO users (id, email, email_key, name, role, department_id, active, password_hash, "
      "password_salt, password_iterations, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, '', '', 0, ?)");
  st.bind(1, input.id).bind(2, email).bind(3, lowerAscii(email)).bind(4, name).bind(5, input.role);
  input.departmentId.empty() ? st.bindNull(6) : st.bind(6, input.departmentId);
  st.bind(7, input.active ? 1.0 : 0.0).bind(8, input.createdAt);
  st.step();
  writePassword(db, input.id, input.password);
  tx.commit();

  return *findUser(db, input.id);
}

User updateUser(Database& db, const UserInput& input) {
  auto existing = findUser(db, input.id);
  if (!existing) throw NotFoundError("usuário não encontrado: " + input.id);

  std::string name = trim(input.name);
  std::string email = trim(input.email);
  if (name.empty()) throw std::invalid_argument("informe o nome do usuário");
  validateEmail(email);
  validateRole(input.role);

  auto byEmail = findUserByEmail(db, email);
  if (byEmail && byEmail->id != input.id) {
    throw std::invalid_argument("já existe um usuário com o e-mail " + email);
  }

  // O app não pode ficar sem ninguém que administre: rebaixar ou desativar o
  // último superadmin ativo é recusado aqui, no motor — não só escondendo o
  // botão na tela.
  bool deixaDeAdministrar = existing->isSuperadmin() && (input.role != kRoleSuperadmin || !input.active);
  if (deixaDeAdministrar && countActiveSuperadmins(db, input.id) == 0) {
    throw std::invalid_argument(
        "este é o único superadministrador ativo — promova outro usuário antes de alterar este");
  }

  auto st = db.prepare(
      "UPDATE users SET email=?, email_key=?, name=?, role=?, department_id=?, active=? WHERE id=?");
  st.bind(1, email).bind(2, lowerAscii(email)).bind(3, name).bind(4, input.role);
  input.departmentId.empty() ? st.bindNull(5) : st.bind(5, input.departmentId);
  st.bind(6, input.active ? 1.0 : 0.0).bind(7, input.id);
  st.step();

  return *findUser(db, input.id);
}

void deleteUser(Database& db, const std::string& id) {
  auto existing = findUser(db, id);
  if (!existing) throw NotFoundError("usuário não encontrado: " + id);
  if (existing->isSuperadmin() && existing->active && countActiveSuperadmins(db, id) == 0) {
    throw std::invalid_argument(
        "este é o único superadministrador ativo — promova outro usuário antes de excluir este");
  }
  // As requisições que ele criou/aprovou permanecem: autor e validador ficam
  // denormalizados em requests (requester_name / decided_by_name), então o
  // histórico continua legível depois que a conta some.
  db.prepare("DELETE FROM users WHERE id=?").bind(1, id).step();
}

void setUserPassword(Database& db, const std::string& id, const std::string& password) {
  if (!findUser(db, id)) throw NotFoundError("usuário não encontrado: " + id);
  writePassword(db, id, password);
}

bool checkUserPassword(Database& db, const std::string& id, const std::string& password) {
  auto st = db.prepare("SELECT password_hash, password_salt, password_iterations FROM users WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return false;
  std::string hash = st.columnText(0);
  std::string salt = st.columnText(1);
  int iterations = static_cast<int>(st.columnDouble(2));
  if (hash.empty() || salt.empty() || iterations <= 0) return false;
  std::string calc = crypto::toHex(crypto::pbkdf2Sha256(password, crypto::fromHex(salt), iterations, 32));
  return crypto::constantTimeEquals(calc, hash);
}

std::optional<User> authenticate(Database& db, const std::string& email, const std::string& password,
                                 const std::string& nowIso) {
  auto user = findUserByEmail(db, email);
  if (!user) return std::nullopt;
  if (!checkUserPassword(db, user->id, password)) return std::nullopt;
  // Conta desativada falha DEPOIS da checagem da senha, de propósito: assim o
  // tempo de resposta não diz a um estranho quais e-mails existem no sistema.
  if (!user->active) return std::nullopt;

  db.prepare("UPDATE users SET last_login_at=? WHERE id=?").bind(1, nowIso).bind(2, user->id).step();
  user->lastLoginAt = nowIso;
  return user;
}

void ensureDefaultSuperadmin(Database& db, const std::string& nowIso) {
  {
    auto st = db.prepare("SELECT COUNT(*) FROM users WHERE role='superadmin' AND active=1");
    st.step();
    if (st.columnDouble(0) > 0) return;
  }
  // Se a conta padrão existe mas está desativada/rebaixada, reativá-la em
  // silêncio seria uma porta dos fundos — nesse caso só se recria quando não
  // há NENHUM superadmin ativo, que é exatamente o cenário de app travado.
  auto existing = findUserByEmail(db, kDefaultSuperadminEmail);
  if (existing) {
    db.prepare("UPDATE users SET role='superadmin', active=1 WHERE id=?").bind(1, existing->id).step();
    return;
  }

  UserInput input;
  input.id = "usr_superadmin";
  input.name = "Carlos Matos";
  input.email = kDefaultSuperadminEmail;
  input.role = kRoleSuperadmin;
  input.active = true;
  input.createdAt = nowIso;
  input.password = "Mudar@2025";
  createUser(db, input);
}

// ------------------------------------------------------ grupos de permissão

std::vector<PermissionGroup> listPermissionGroups(Database& db) {
  std::vector<PermissionGroup> out;
  auto st = db.prepare("SELECT id, name, description, created_at FROM permission_groups ORDER BY name");
  while (st.step()) {
    PermissionGroup g;
    g.id = st.columnText(0);
    g.name = st.columnText(1);
    g.description = st.columnIsNull(2) ? "" : st.columnText(2);
    g.createdAt = st.columnText(3);
    out.push_back(std::move(g));
  }
  for (auto& g : out) g.perms = loadGroupPerms(db, g.id);
  return out;
}

std::optional<PermissionGroup> findPermissionGroup(Database& db, const std::string& id) {
  auto st = db.prepare("SELECT id, name, description, created_at FROM permission_groups WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  PermissionGroup g;
  g.id = st.columnText(0);
  g.name = st.columnText(1);
  g.description = st.columnIsNull(2) ? "" : st.columnText(2);
  g.createdAt = st.columnText(3);
  g.perms = loadGroupPerms(db, g.id);
  return g;
}

PermissionGroup createPermissionGroup(Database& db, const PermissionGroup& input) {
  std::string name = trim(input.name);
  if (name.empty()) throw std::invalid_argument("informe o nome do grupo de permissão");

  Transaction tx(db);
  auto st = db.prepare(
      "INSERT INTO permission_groups (id, name, description, created_at) VALUES (?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, name);
  input.description.empty() ? st.bindNull(3) : st.bind(3, input.description);
  st.bind(4, input.createdAt);
  st.step();
  writeGroupPerms(db, input.id, input.perms);
  tx.commit();

  return *findPermissionGroup(db, input.id);
}

PermissionGroup updatePermissionGroup(Database& db, const PermissionGroup& input) {
  if (!findPermissionGroup(db, input.id)) {
    throw NotFoundError("grupo de permissão não encontrado: " + input.id);
  }
  std::string name = trim(input.name);
  if (name.empty()) throw std::invalid_argument("informe o nome do grupo de permissão");

  Transaction tx(db);
  auto st = db.prepare("UPDATE permission_groups SET name=?, description=? WHERE id=?");
  st.bind(1, name);
  input.description.empty() ? st.bindNull(2) : st.bind(2, input.description);
  st.bind(3, input.id);
  st.step();
  writeGroupPerms(db, input.id, input.perms);
  tx.commit();

  return *findPermissionGroup(db, input.id);
}

void deletePermissionGroup(Database& db, const std::string& id) {
  if (!findPermissionGroup(db, id)) {
    throw NotFoundError("grupo de permissão não encontrado: " + id);
  }
  Transaction tx(db);
  // Solta os departamentos ANTES de apagar o grupo: um department apontando
  // para um grupo inexistente resolveria como "sem permissão" de qualquer
  // jeito, mas deixaria a tela de Permissões mostrando um vínculo fantasma.
  db.prepare("UPDATE departments SET permission_group_id=NULL WHERE permission_group_id=?")
      .bind(1, id)
      .step();
  db.prepare("DELETE FROM permission_group_perms WHERE group_id=?").bind(1, id).step();
  db.prepare("DELETE FROM permission_groups WHERE id=?").bind(1, id).step();
  tx.commit();
}

void setDepartmentPermissionGroup(Database& db, const std::string& departmentId,
                                  const std::string& groupId) {
  {
    auto st = db.prepare("SELECT 1 FROM departments WHERE id=?");
    st.bind(1, departmentId);
    if (!st.step()) throw NotFoundError("departamento não encontrado: " + departmentId);
  }
  if (!groupId.empty() && !findPermissionGroup(db, groupId)) {
    throw NotFoundError("grupo de permissão não encontrado: " + groupId);
  }
  auto st = db.prepare("UPDATE departments SET permission_group_id=? WHERE id=?");
  groupId.empty() ? st.bindNull(1) : st.bind(1, groupId);
  st.bind(2, departmentId);
  st.step();
}

std::string departmentPermissionGroupId(Database& db, const std::string& departmentId) {
  if (departmentId.empty()) return "";
  auto st = db.prepare("SELECT permission_group_id FROM departments WHERE id=?");
  st.bind(1, departmentId);
  if (!st.step() || st.columnIsNull(0)) return "";
  return st.columnText(0);
}

// ------------------------------------------------------------- resolução

std::vector<FeaturePermissions> effectivePermissions(Database& db, const User& user) {
  std::vector<FeaturePermissions> out;
  for (const auto& f : kFeatureCatalog) out.push_back(emptyPerm(f.key));

  if (user.isSuperadmin()) {
    for (auto& p : out) {
      p.create = p.read = p.update = p.del = true;
    }
    return out;
  }
  if (!user.active) return out;

  std::string groupId = departmentPermissionGroupId(db, user.departmentId);
  if (groupId.empty()) return out;
  return loadGroupPerms(db, groupId);
}

bool userCan(Database& db, const User& user, const std::string& feature, PermAction action) {
  if (user.isSuperadmin()) return true;
  if (!user.active) return false;
  auto perms = effectivePermissions(db, user);
  auto it = std::find_if(perms.begin(), perms.end(),
                         [&](const FeaturePermissions& p) { return p.feature == feature; });
  return it != perms.end() && it->has(action);
}

}  // namespace estoque
