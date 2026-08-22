#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <nlohmann/json.hpp>

#include "doctest.h"
#include "estoque/api.hpp"
#include "estoque/auth_engine.hpp"
#include "estoque/inventory_engine.hpp"

using namespace estoque;
using json = nlohmann::json;

namespace {

constexpr const char* kNow = "2026-08-10T12:00:00.000Z";

Department makeDept(Database& db, const std::string& id, const std::string& name) {
  Department d;
  d.id = id;
  d.name = name;
  d.encarregado = "Encarregado " + name;
  d.createdAt = kNow;
  return createDepartment(db, d);
}

UserInput makeUser(const std::string& id, const std::string& email, const std::string& deptId,
                   const std::string& role = kRoleUsuario) {
  UserInput u;
  u.id = id;
  u.name = "Usuário " + id;
  u.email = email;
  u.role = role;
  u.departmentId = deptId;
  u.active = true;
  u.createdAt = kNow;
  u.password = "SenhaForte1";
  return u;
}

// Grupo com todas as funções desligadas, exceto as ligadas por `ligar`.
PermissionGroup makeGroup(const std::string& id, const std::string& name,
                          const std::vector<std::pair<std::string, PermAction>>& ligar) {
  PermissionGroup g;
  g.id = id;
  g.name = name;
  g.createdAt = kNow;
  for (const auto& f : featureCatalog()) {
    FeaturePermissions p;
    p.feature = f.key;
    for (const auto& [feature, action] : ligar) {
      if (feature == f.key) p.set(action, true);
    }
    g.perms.push_back(p);
  }
  return g;
}

}  // namespace

TEST_CASE("o superadministrador de fábrica é semeado e entra com a senha combinada") {
  Database db(":memory:");
  ensureDefaultSuperadmin(db, kNow);

  auto logado = authenticate(db, kDefaultSuperadminEmail, "Mudar@2025", kNow);
  REQUIRE(logado.has_value());
  CHECK(logado->isSuperadmin());
  CHECK(logado->lastLoginAt == kNow);

  // e-mail não diferencia maiúsculas
  CHECK(authenticate(db, "CARLOS.MATOS@FLCONDOMINIOS.COM.BR", "Mudar@2025", kNow).has_value());
  // senha errada não entra
  CHECK_FALSE(authenticate(db, kDefaultSuperadminEmail, "mudar@2025", kNow).has_value());
  CHECK_FALSE(authenticate(db, "ninguem@flcondominios.com.br", "Mudar@2025", kNow).has_value());
}

TEST_CASE("semear é idempotente e nunca redefine a senha de quem já existe") {
  Database db(":memory:");
  ensureDefaultSuperadmin(db, kNow);
  setUserPassword(db, findUserByEmail(db, kDefaultSuperadminEmail)->id, "OutraSenha123");

  ensureDefaultSuperadmin(db, kNow);  // segundo boot

  CHECK(listUsers(db).size() == 1);
  CHECK(authenticate(db, kDefaultSuperadminEmail, "OutraSenha123", kNow).has_value());
  CHECK_FALSE(authenticate(db, kDefaultSuperadminEmail, "Mudar@2025", kNow).has_value());
}

TEST_CASE("a senha nunca é gravada em texto puro e cada usuário tem salt próprio") {
  Database db(":memory:");
  createUser(db, makeUser("u1", "a@flcondominios.com.br", ""));
  createUser(db, makeUser("u2", "b@flcondominios.com.br", ""));

  auto st = db.prepare("SELECT password_hash, password_salt FROM users ORDER BY id");
  REQUIRE(st.step());
  std::string hash1 = st.columnText(0), salt1 = st.columnText(1);
  REQUIRE(st.step());
  std::string hash2 = st.columnText(0), salt2 = st.columnText(1);

  CHECK(hash1.find("SenhaForte1") == std::string::npos);
  CHECK(salt1 != salt2);
  CHECK(hash1 != hash2);  // mesma senha, hashes diferentes — é o salt fazendo efeito
}

TEST_CASE("conta desativada não entra, mesmo com a senha certa") {
  Database db(":memory:");
  createUser(db, makeUser("u1", "a@flcondominios.com.br", ""));
  auto input = makeUser("u1", "a@flcondominios.com.br", "");
  input.active = false;
  updateUser(db, input);

  CHECK_FALSE(authenticate(db, "a@flcondominios.com.br", "SenhaForte1", kNow).has_value());
}

TEST_CASE("cadastro de usuário recusa o que não faz sentido") {
  Database db(":memory:");
  createUser(db, makeUser("u1", "a@flcondominios.com.br", ""));

  auto duplicado = makeUser("u2", "A@FLCondominios.com.br", "");
  CHECK_THROWS_AS(createUser(db, duplicado), std::invalid_argument);  // mesmo e-mail, outra caixa

  auto semArroba = makeUser("u3", "sem-arroba", "");
  CHECK_THROWS_AS(createUser(db, semArroba), std::invalid_argument);

  auto senhaCurta = makeUser("u4", "c@flcondominios.com.br", "");
  senhaCurta.password = "1234567";
  CHECK_THROWS_AS(createUser(db, senhaCurta), std::invalid_argument);

  auto semNome = makeUser("u5", "d@flcondominios.com.br", "");
  semNome.name = "   ";
  CHECK_THROWS_AS(createUser(db, semNome), std::invalid_argument);

  // e o cadastro recusado não deixou lixo para trás
  CHECK(listUsers(db).size() == 1);
}

TEST_CASE("o último superadministrador ativo não pode ser rebaixado, desativado nem excluído") {
  Database db(":memory:");
  ensureDefaultSuperadmin(db, kNow);
  auto admin = *findUserByEmail(db, kDefaultSuperadminEmail);

  auto rebaixar = makeUser(admin.id, admin.email, "");
  rebaixar.name = admin.name;
  rebaixar.role = kRoleUsuario;
  CHECK_THROWS_AS(updateUser(db, rebaixar), std::invalid_argument);
  CHECK_THROWS_AS(deleteUser(db, admin.id), std::invalid_argument);

  // com um segundo superadmin ativo, aí sim
  createUser(db, makeUser("u2", "outro@flcondominios.com.br", "", kRoleSuperadmin));
  CHECK_NOTHROW(updateUser(db, rebaixar));
  CHECK(findUser(db, admin.id)->role == kRoleUsuario);
}

TEST_CASE("permissão é do DEPARTAMENTO e o usuário herda a do setor dele") {
  Database db(":memory:");
  makeDept(db, "dep_manut", "Manutenção");
  makeDept(db, "dep_adm", "Administrativo");

  createPermissionGroup(db, makeGroup("grp_solicitante", "Solicitante",
                                      {{features::kRequisicoes, PermAction::Create},
                                       {features::kRequisicoes, PermAction::Read}}));
  setDepartmentPermissionGroup(db, "dep_manut", "grp_solicitante");

  User daManutencao = createUser(db, makeUser("u1", "a@flcondominios.com.br", "dep_manut"));
  User doAdm = createUser(db, makeUser("u2", "b@flcondominios.com.br", "dep_adm"));
  User semSetor = createUser(db, makeUser("u3", "c@flcondominios.com.br", ""));

  CHECK(userCan(db, daManutencao, features::kRequisicoes, PermAction::Create));
  CHECK(userCan(db, daManutencao, features::kRequisicoes, PermAction::Read));
  CHECK_FALSE(userCan(db, daManutencao, features::kRequisicoes, PermAction::Update));
  CHECK_FALSE(userCan(db, daManutencao, features::kProdutos, PermAction::Read));

  // o setor sem grupo não herda nada, e quem não tem setor também não
  CHECK_FALSE(userCan(db, doAdm, features::kRequisicoes, PermAction::Create));
  CHECK_FALSE(userCan(db, semSetor, features::kRequisicoes, PermAction::Create));

  // mover a pessoa de setor muda o que ela pode, sem tocar em nenhuma lista
  auto mudanca = makeUser("u2", "b@flcondominios.com.br", "dep_manut");
  updateUser(db, mudanca);
  CHECK(userCan(db, *findUser(db, "u2"), features::kRequisicoes, PermAction::Create));
}

TEST_CASE("superadministrador ignora a matriz inteira") {
  Database db(":memory:");
  makeDept(db, "dep_x", "Setor X");
  User chefe = createUser(db, makeUser("u1", "chefe@flcondominios.com.br", "dep_x", kRoleSuperadmin));

  for (const auto& f : featureCatalog()) {
    CHECK(userCan(db, chefe, f.key, PermAction::Create));
    CHECK(userCan(db, chefe, f.key, PermAction::Read));
    CHECK(userCan(db, chefe, f.key, PermAction::Update));
    CHECK(userCan(db, chefe, f.key, PermAction::Delete));
  }
}

TEST_CASE("excluir um grupo solta os departamentos em vez de deixar vínculo fantasma") {
  Database db(":memory:");
  makeDept(db, "dep_x", "Setor X");
  createPermissionGroup(db, makeGroup("grp", "Grupo", {{features::kProdutos, PermAction::Read}}));
  setDepartmentPermissionGroup(db, "dep_x", "grp");
  User u = createUser(db, makeUser("u1", "a@flcondominios.com.br", "dep_x"));
  REQUIRE(userCan(db, u, features::kProdutos, PermAction::Read));

  deletePermissionGroup(db, "grp");

  CHECK(departmentPermissionGroupId(db, "dep_x").empty());
  CHECK_FALSE(userCan(db, u, features::kProdutos, PermAction::Read));
  auto sobrou = db.prepare("SELECT COUNT(*) FROM permission_group_perms");
  sobrou.step();
  CHECK(sobrou.columnDouble(0) == 0);
}

TEST_CASE("editar o grupo muda na hora o que o usuário do setor enxerga") {
  Database db(":memory:");
  makeDept(db, "dep_x", "Setor X");
  createPermissionGroup(db, makeGroup("grp", "Grupo", {{features::kProdutos, PermAction::Read}}));
  setDepartmentPermissionGroup(db, "dep_x", "grp");
  User u = createUser(db, makeUser("u1", "a@flcondominios.com.br", "dep_x"));

  auto ampliado = makeGroup("grp", "Grupo",
                            {{features::kProdutos, PermAction::Read},
                             {features::kProdutos, PermAction::Update}});
  updatePermissionGroup(db, ampliado);

  CHECK(userCan(db, u, features::kProdutos, PermAction::Update));

  updatePermissionGroup(db, makeGroup("grp", "Grupo", {}));  // tira tudo
  CHECK_FALSE(userCan(db, u, features::kProdutos, PermAction::Read));
}

// ------------------------------------------------------------------ Api

TEST_CASE("a Api recusa qualquer operação sem sessão") {
  Api api(":memory:");
  CHECK_THROWS_AS(api.listProducts(), AuthError);
  CHECK_THROWS_AS(api.listMovements(), AuthError);
  CHECK_THROWS_AS(api.backupJson(), AuthError);
  CHECK_THROWS_AS(api.listUsersJson(), AuthError);
  CHECK_THROWS_AS(api.computeReportJson(2026, 5, "", 6, kNow), AuthError);
  CHECK_THROWS_AS(api.assertPodeEditarLogoFl(), AuthError);
  CHECK(api.currentSessionJson() == "null");
}

TEST_CASE("login pela Api devolve a sessão com a matriz de permissões efetiva") {
  Api api(":memory:");
  api.loginAsService("teste");

  Department d;
  d.id = "dep_manut";
  d.name = "Manutenção";
  d.encarregado = "Zé";
  d.createdAt = kNow;
  api.createDepartment(d);

  json grupo;
  grupo["id"] = "grp";
  grupo["name"] = "Solicitante";
  grupo["createdAt"] = kNow;
  grupo["perms"] = {{"requisicoes", {{"create", true}, {"read", true}, {"update", false}, {"delete", true}}}};
  api.createPermissionGroup(grupo.dump());
  api.setDepartmentPermissionGroup("dep_manut", "grp");

  UserInput u = makeUser("u1", "pedro@flcondominios.com.br", "dep_manut");
  api.createUser(u);
  api.logout();

  json sessao = json::parse(api.login("pedro@flcondominios.com.br", "SenhaForte1", kNow));
  CHECK(sessao["user"]["email"] == "pedro@flcondominios.com.br");
  CHECK(sessao["user"]["departmentName"] == "Manutenção");
  CHECK(sessao["permissions"]["requisicoes"]["create"] == true);
  CHECK(sessao["permissions"]["requisicoes"]["update"] == false);
  CHECK(sessao["permissions"]["produtos"]["read"] == false);
  CHECK(sessao["features"].size() == featureCatalog().size());

  // e a sessão realmente limita: ler produtos é permitido (dado de apoio das
  // requisições), mas cadastrar produto não é.
  CHECK_NOTHROW(api.listProducts());
  Product p;
  p.id = "p1";
  p.name = "Papel";
  p.unit = "Unidade";
  p.category = "Papelaria";
  p.createdAt = kNow;
  CHECK_THROWS_AS(api.createProduct(p), ForbiddenError);
  CHECK_THROWS_AS(api.listMovements(), ForbiddenError);
  CHECK_THROWS_AS(api.listUsersJson(), ForbiddenError);
  CHECK_THROWS_AS(api.backupJson(), ForbiddenError);
  // Logo da FL: mesmo com departamento/grupo dando tudo em requisições, um
  // usuário comum não é superadministrador — a logo continua fora do alcance
  // (ver assertPodeEditarLogoFl, chamado pelo comando Tauri antes de gravar
  // ou apagar o arquivo).
  CHECK_THROWS_AS(api.assertPodeEditarLogoFl(), ForbiddenError);

  api.login("carlos.matos@flcondominios.com.br", "Mudar@2025", kNow);
  CHECK_NOTHROW(api.createProduct(p));
  CHECK_NOTHROW(api.assertPodeEditarLogoFl());
}

TEST_CASE("quem só pode ver Produtos consegue abrir a tela inteira") {
  // A tela de Produtos precisa da lista de DEPARTAMENTOS (modal de baixa) e da
  // disponibilidade (coluna de reservado). Se qualquer uma dessas exigir uma
  // permissão que o perfil não tem, a tela quebra inteira ao abrir — foi
  // exatamente o que este teste pegou.
  Api api(":memory:");
  api.loginAsService("teste");

  Department d;
  d.id = "dep_x";
  d.name = "Setor X";
  d.encarregado = "Zé";
  d.createdAt = kNow;
  api.createDepartment(d);

  json grupo;
  grupo["id"] = "grp";
  grupo["name"] = "Consulta de estoque";
  grupo["createdAt"] = kNow;
  grupo["perms"] = {{"produtos", {{"read", true}}}};
  api.createPermissionGroup(grupo.dump());
  api.setDepartmentPermissionGroup("dep_x", "grp");
  api.createUser(makeUser("u1", "consulta@flcondominios.com.br", "dep_x"));

  api.login("consulta@flcondominios.com.br", "SenhaForte1", kNow);
  CHECK_NOTHROW(api.listProducts());
  CHECK_NOTHROW(api.listDepartments());
  CHECK_NOTHROW(api.stockAvailabilityJson());
  // e nada além disso
  CHECK_THROWS_AS(api.listMovements(), ForbiddenError);
  CHECK_THROWS_AS(api.listRequestsJson(), ForbiddenError);
}

TEST_CASE("login errado não abre sessão e não diz o que estava errado") {
  Api api(":memory:");
  CHECK_THROWS_AS(api.login(kDefaultSuperadminEmail, "errada", kNow), AuthError);
  CHECK(api.currentSessionJson() == "null");

  try {
    api.login("naoexiste@flcondominios.com.br", "seja-o-que-for", kNow);
    FAIL("deveria ter lançado");
  } catch (const AuthError& e) {
    CHECK(std::string(e.what()) == "[auth] e-mail ou senha inválidos");
  }
}

TEST_CASE("trocar a própria senha exige acertar a atual") {
  Api api(":memory:");
  api.login(kDefaultSuperadminEmail, "Mudar@2025", kNow);

  CHECK_THROWS_AS(api.changeOwnPassword("errada", "NovaSenha123"), AuthError);
  CHECK_THROWS_AS(api.changeOwnPassword("Mudar@2025", "curta"), std::invalid_argument);

  api.changeOwnPassword("Mudar@2025", "NovaSenha123");
  api.logout();
  CHECK_THROWS_AS(api.login(kDefaultSuperadminEmail, "Mudar@2025", kNow), AuthError);
  CHECK_NOTHROW(api.login(kDefaultSuperadminEmail, "NovaSenha123", kNow));
}

TEST_CASE("o superadmin não consegue remover o próprio acesso pela Api") {
  Api api(":memory:");
  api.login(kDefaultSuperadminEmail, "Mudar@2025", kNow);
  // outro superadmin existe, então o motor deixaria — quem barra aqui é a Api
  api.createUser(makeUser("u2", "outro@flcondominios.com.br", "", kRoleSuperadmin));

  json sessao = json::parse(api.currentSessionJson());
  UserInput eu = makeUser(sessao["user"]["id"].get<std::string>(), kDefaultSuperadminEmail, "");
  eu.role = kRoleUsuario;
  CHECK_THROWS_AS(api.updateUser(eu), std::invalid_argument);
  CHECK_THROWS_AS(api.deleteUser(sessao["user"]["id"].get<std::string>()), std::invalid_argument);
}

TEST_CASE("o backup leva usuários e permissões, e restaurar não tranca o app para fora") {
  Api api(":memory:");
  api.loginAsService("teste");

  Department d;
  d.id = "dep_x";
  d.name = "Setor X";
  d.encarregado = "Zé";
  d.createdAt = kNow;
  api.createDepartment(d);

  json grupo;
  grupo["id"] = "grp";
  grupo["name"] = "Solicitante";
  grupo["createdAt"] = kNow;
  grupo["perms"] = {{"requisicoes", {{"create", true}, {"read", true}}}};
  api.createPermissionGroup(grupo.dump());
  api.setDepartmentPermissionGroup("dep_x", "grp");
  api.createUser(makeUser("u1", "pedro@flcondominios.com.br", "dep_x"));

  std::string backup = api.backupJson();
  json parsed = json::parse(backup);
  CHECK(parsed["users"].size() == 2);  // o semeado + o criado aqui
  CHECK(parsed["permissionGroups"].size() == 1);
  CHECK(parsed["departmentPermissionGroups"][0]["groupId"] == "grp");
  // hash sim, senha não
  CHECK(parsed["users"][0]["passwordHash"].get<std::string>().size() == 64);
  CHECK(backup.find("SenhaForte1") == std::string::npos);

  Api api2(":memory:");
  api2.loginAsService("teste");
  api2.restoreFromJson(backup);
  json sessao = json::parse(api2.login("pedro@flcondominios.com.br", "SenhaForte1", kNow));
  CHECK(sessao["permissions"]["requisicoes"]["create"] == true);

  // e um backup ANTIGO (sem a chave "users") não pode apagar os usuários
  json antigo;
  antigo["products"] = json::array();
  antigo["movements"] = json::array();
  antigo["departments"] = json::array();
  Api api3(":memory:");
  api3.loginAsService("teste");
  api3.createUser(makeUser("u9", "sobrevive@flcondominios.com.br", ""));
  api3.restoreFromJson(antigo.dump());
  CHECK(json::parse(api3.listUsersJson()).size() == 2);
}
