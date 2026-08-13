#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <nlohmann/json.hpp>

#include "doctest.h"
#include "estoque/api.hpp"
#include "estoque/inventory_engine.hpp"
#include "estoque/request_engine.hpp"

using namespace estoque;
using json = nlohmann::json;

namespace {

constexpr const char* kNow = "2026-08-10T12:00:00.000Z";
constexpr const char* kDepois = "2026-08-11T09:00:00.000Z";

// Cenário base: um produto com 10 unidades a R$ 20 e um departamento.
struct Cenario {
  Database db{":memory:"};
  User admin;

  Cenario() {
    ensureDefaultSuperadmin(db, kNow);
    admin = *findUserByEmail(db, kDefaultSuperadminEmail);

    Product p;
    p.id = "p1";
    p.name = "Papel A4";
    p.unit = "Resma";
    p.createdAt = kNow;
    createProduct(db, p);
    applyEntrada(db, "m_ini", "p1", 10, 20.0, "Fornecedor", "NF1", kNow, "", kNow);

    Department d;
    d.id = "dep_x";
    d.name = "Manutenção";
    d.encarregado = "Zé";
    d.createdAt = kNow;
    createDepartment(db, d);
  }

  RequestInput pedido(const std::string& id, double qty, const std::string& productId = "p1") {
    RequestInput in;
    in.id = id;
    in.departmentId = "dep_x";
    in.requesterUserId = "u1";
    in.requesterName = "Pedro";
    in.createdAt = kNow;
    RequestItem it;
    it.id = id + "_i1";
    it.productId = productId;
    it.qty = qty;
    in.items.push_back(it);
    return in;
  }

  double saldo(const std::string& productId = "p1") { return findProduct(db, productId)->qty; }
  double disponivel(const std::string& productId = "p1") {
    for (const auto& a : stockAvailability(db)) {
      if (a.productId == productId) return a.available;
    }
    return 0;
  }
};

}  // namespace

TEST_CASE("uma requisição em aberto RESERVA, mas não debita o estoque") {
  Cenario c;
  Request r = createRequest(c.db, c.pedido("req1", 3));

  CHECK(r.status == request_status::kPendente);
  CHECK(r.requesterName == "Pedro");
  CHECK(r.createdAt == kNow);
  CHECK(r.departmentName == "Manutenção");
  REQUIRE(r.items.size() == 1);
  CHECK(r.items[0].productName == "Papel A4");
  CHECK(r.items[0].unit == "Resma");
  CHECK(r.items[0].movementId.empty());

  CHECK(c.saldo() == doctest::Approx(10));       // o estoque não se mexeu
  CHECK(reservedQty(c.db, "p1") == doctest::Approx(3));
  CHECK(c.disponivel() == doctest::Approx(7));   // mas o disponível caiu
  CHECK(listMovements(c.db).size() == 1);        // nenhuma saída foi criada
}

TEST_CASE("aprovar mantém a reserva; a entrega é que debita o estoque") {
  Cenario c;
  createRequest(c.db, c.pedido("req1", 3));
  Request aprovada = approveRequest(c.db, "req1", c.admin, "ok", kDepois);

  CHECK(aprovada.status == request_status::kAprovado);
  CHECK(aprovada.decidedAt == kDepois);
  CHECK(aprovada.decidedByName == c.admin.name);
  CHECK(c.saldo() == doctest::Approx(10));
  CHECK(c.disponivel() == doctest::Approx(7));

  Request entregue = deliverRequest(c.db, "req1", c.admin, kDepois, "m_ent");

  CHECK(entregue.status == request_status::kEntregue);
  CHECK(entregue.deliveredAt == kDepois);
  CHECK(entregue.deliveredByName == c.admin.name);
  CHECK(c.saldo() == doctest::Approx(7));        // AGORA sim o estoque caiu
  CHECK(reservedQty(c.db, "p1") == doctest::Approx(0));  // e a reserva foi liberada
  CHECK(c.disponivel() == doctest::Approx(7));

  // a baixa é uma saída de verdade, valorizada pelo custo médio e vinculada
  // ao pedido — não um UPDATE escondido em products
  auto movs = listMovements(c.db);
  REQUIRE(movs.size() == 2);
  const Movement& saida = movs[1];
  CHECK(saida.id == "m_ent_1");
  CHECK(saida.type == MovementType::Saida);
  CHECK(saida.qty == doctest::Approx(3));
  CHECK(saida.unitPrice == doctest::Approx(20.0));
  CHECK(saida.departmentId == "dep_x");
  CHECK(saida.recipient == "Manutenção");
  CHECK(saida.encarregado == "Zé");
  CHECK(saida.requester == "Pedro");
  CHECK(saida.obs.find("req1") != std::string::npos);
  CHECK(findRequest(c.db, "req1")->items[0].movementId == "m_ent_1");
}

TEST_CASE("rejeitar e cancelar liberam a reserva sem tocar no estoque") {
  Cenario c;
  createRequest(c.db, c.pedido("req1", 4));
  createRequest(c.db, c.pedido("req2", 2));
  REQUIRE(c.disponivel() == doctest::Approx(4));

  rejectRequest(c.db, "req1", c.admin, "sem verba", kDepois);
  CHECK(c.disponivel() == doctest::Approx(8));

  approveRequest(c.db, "req2", c.admin, "", kDepois);
  cancelRequest(c.db, "req2", c.admin, "não precisa mais", kDepois);
  CHECK(c.disponivel() == doctest::Approx(10));
  CHECK(c.saldo() == doctest::Approx(10));
  CHECK(listMovements(c.db).size() == 1);
}

TEST_CASE("não se pede mais do que está disponível — a reserva alheia conta") {
  Cenario c;
  createRequest(c.db, c.pedido("req1", 8));

  // sobrou 2 disponível de um saldo de 10: pedir 3 tem que falhar
  CHECK_THROWS_AS(createRequest(c.db, c.pedido("req2", 3)), std::invalid_argument);
  CHECK_NOTHROW(createRequest(c.db, c.pedido("req3", 2)));
  CHECK(c.disponivel() == doctest::Approx(0));

  // e a requisição recusada não deixou cabeçalho órfão no banco
  CHECK(listRequests(c.db, "").size() == 2);
  CHECK_FALSE(findRequest(c.db, "req2").has_value());
}

TEST_CASE("itens repetidos do mesmo produto somam antes de conferir o disponível") {
  Cenario c;
  RequestInput in = c.pedido("req1", 6);
  RequestItem outro;
  outro.id = "req1_i2";
  outro.productId = "p1";
  outro.qty = 6;
  in.items.push_back(outro);

  // 6 + 6 = 12 > 10: não pode passar por serem duas linhas de 6
  CHECK_THROWS_AS(createRequest(c.db, in), std::invalid_argument);

  in.items[1].qty = 4;
  Request r = createRequest(c.db, in);
  REQUIRE(r.items.size() == 1);  // consolidados numa linha só
  CHECK(r.items[0].qty == doctest::Approx(10));
}

TEST_CASE("quantidade inválida e produto inexistente são recusados") {
  Cenario c;
  CHECK_THROWS_AS(createRequest(c.db, c.pedido("req1", 0)), std::invalid_argument);
  CHECK_THROWS_AS(createRequest(c.db, c.pedido("req2", -1)), std::invalid_argument);
  CHECK_THROWS_AS(createRequest(c.db, c.pedido("req3", 1, "nao_existe")), NotFoundError);

  RequestInput vazio;
  vazio.id = "req4";
  vazio.departmentId = "dep_x";
  vazio.requesterName = "Pedro";
  vazio.createdAt = kNow;
  CHECK_THROWS_AS(createRequest(c.db, vazio), std::invalid_argument);

  RequestInput semSetor = c.pedido("req5", 1);
  semSetor.departmentId = "dep_inexistente";
  CHECK_THROWS_AS(createRequest(c.db, semSetor), NotFoundError);
}

TEST_CASE("as transições respeitam o ciclo de vida") {
  Cenario c;
  createRequest(c.db, c.pedido("req1", 1));

  // entregar antes de aprovar, não
  CHECK_THROWS_AS(deliverRequest(c.db, "req1", c.admin, kDepois, "m_x"), std::invalid_argument);

  approveRequest(c.db, "req1", c.admin, "", kDepois);
  CHECK_THROWS_AS(approveRequest(c.db, "req1", c.admin, "", kDepois), std::invalid_argument);
  CHECK_THROWS_AS(rejectRequest(c.db, "req1", c.admin, "", kDepois), std::invalid_argument);

  deliverRequest(c.db, "req1", c.admin, kDepois, "m_x");
  // depois de entregue, nada mais se faz por aqui
  CHECK_THROWS_AS(cancelRequest(c.db, "req1", c.admin, "", kDepois), std::invalid_argument);
  CHECK_THROWS_AS(deliverRequest(c.db, "req1", c.admin, kDepois, "m_y"), std::invalid_argument);
  CHECK_THROWS_AS(approveRequest(c.db, "inexistente", c.admin, "", kDepois), NotFoundError);
}

TEST_CASE("entrega com vários itens é atômica: um item sem saldo não baixa nenhum") {
  Cenario c;
  Product p2;
  p2.id = "p2";
  p2.name = "Caneta";
  p2.unit = "Unidade";
  p2.createdAt = kNow;
  createProduct(c.db, p2);
  applyEntrada(c.db, "m_ini2", "p2", 5, 2.0, "Fornecedor", "NF2", kNow, "", kNow);

  RequestInput in = c.pedido("req1", 4);
  RequestItem it2;
  it2.id = "req1_i2";
  it2.productId = "p2";
  it2.qty = 5;
  in.items.push_back(it2);
  createRequest(c.db, in);
  approveRequest(c.db, "req1", c.admin, "", kDepois);

  // alguém dá baixa direta na caneta depois da aprovação
  applySaida(c.db, "m_direta", "p2", 3, "dep_x", kDepois, "baixa manual", "Outro", kDepois);
  REQUIRE(c.saldo("p2") == doctest::Approx(2));

  CHECK_THROWS_AS(deliverRequest(c.db, "req1", c.admin, kDepois, "m_ent"), std::invalid_argument);

  // nada foi baixado, nem do papel (que tinha saldo de sobra)
  CHECK(c.saldo("p1") == doctest::Approx(10));
  CHECK(c.saldo("p2") == doctest::Approx(2));
  CHECK(findRequest(c.db, "req1")->status == request_status::kAprovado);
  CHECK(findRequest(c.db, "req1")->items[0].movementId.empty());
}

TEST_CASE("a entrega de vários itens gera uma saída por item") {
  Cenario c;
  Product p2;
  p2.id = "p2";
  p2.name = "Caneta";
  p2.unit = "Unidade";
  p2.createdAt = kNow;
  createProduct(c.db, p2);
  applyEntrada(c.db, "m_ini2", "p2", 5, 2.0, "Fornecedor", "NF2", kNow, "", kNow);

  RequestInput in = c.pedido("req1", 4);
  RequestItem it2;
  it2.id = "req1_i2";
  it2.productId = "p2";
  it2.qty = 5;
  in.items.push_back(it2);
  createRequest(c.db, in);
  approveRequest(c.db, "req1", c.admin, "", kDepois);
  deliverRequest(c.db, "req1", c.admin, kDepois, "m_ent");

  CHECK(c.saldo("p1") == doctest::Approx(6));
  CHECK(c.saldo("p2") == doctest::Approx(0));
  auto entregue = *findRequest(c.db, "req1");
  for (const auto& item : entregue.items) CHECK_FALSE(item.movementId.empty());
}

TEST_CASE("o histórico do departamento traz autor, decisão e entrega") {
  Cenario c;
  Department outro;
  outro.id = "dep_y";
  outro.name = "Administrativo";
  outro.encarregado = "Ana";
  outro.createdAt = kNow;
  createDepartment(c.db, outro);

  createRequest(c.db, c.pedido("req1", 1));
  RequestInput doOutro = c.pedido("req2", 1);
  doOutro.departmentId = "dep_y";
  doOutro.requesterName = "Ana";
  createRequest(c.db, doOutro);

  approveRequest(c.db, "req1", c.admin, "liberado", kDepois);
  deliverRequest(c.db, "req1", c.admin, kDepois, "m_ent");

  auto doSetor = listRequests(c.db, "dep_x");
  REQUIRE(doSetor.size() == 1);
  CHECK(doSetor[0].id == "req1");
  CHECK(doSetor[0].requesterName == "Pedro");
  CHECK(doSetor[0].createdAt == kNow);
  CHECK(doSetor[0].decidedByName == c.admin.name);
  CHECK(doSetor[0].decidedAt == kDepois);
  CHECK(doSetor[0].decisionNote == "liberado");
  CHECK(doSetor[0].deliveredAt == kDepois);

  CHECK(listRequests(c.db, "").size() == 2);  // o superadmin vê os dois setores
}

// ------------------------------------------------------------------ Api

TEST_CASE("pela Api, o usuário comum requisita só para o próprio setor e só vê o dele") {
  Api api(":memory:");
  api.loginAsService("teste");

  Department d1;
  d1.id = "dep_x";
  d1.name = "Manutenção";
  d1.encarregado = "Zé";
  d1.createdAt = kNow;
  api.createDepartment(d1);
  Department d2 = d1;
  d2.id = "dep_y";
  d2.name = "Administrativo";
  api.createDepartment(d2);

  Product p;
  p.id = "p1";
  p.name = "Papel A4";
  p.unit = "Resma";
  p.createdAt = kNow;
  api.createProduct(p);
  api.applyEntrada("m_ini", "p1", 10, 20.0, "Fornecedor", "NF1", kNow, "", kNow);

  json grupo;
  grupo["id"] = "grp";
  grupo["name"] = "Solicitante";
  grupo["createdAt"] = kNow;
  grupo["perms"] = {{"requisicoes", {{"create", true}, {"read", true}, {"delete", true}}}};
  api.createPermissionGroup(grupo.dump());
  api.setDepartmentPermissionGroup("dep_x", "grp");
  api.setDepartmentPermissionGroup("dep_y", "grp");

  UserInput u1;
  u1.id = "u1";
  u1.name = "Pedro";
  u1.email = "pedro@flcondominios.com.br";
  u1.role = kRoleUsuario;
  u1.departmentId = "dep_x";
  u1.createdAt = kNow;
  u1.password = "SenhaForte1";
  api.createUser(u1);
  UserInput u2 = u1;
  u2.id = "u2";
  u2.name = "Ana";
  u2.email = "ana@flcondominios.com.br";
  u2.departmentId = "dep_y";
  api.createUser(u2);

  api.login("pedro@flcondominios.com.br", "SenhaForte1", kNow);

  // tenta forçar outro departamento no payload — é ignorado
  json pedido;
  pedido["id"] = "req1";
  pedido["createdAt"] = kNow;
  pedido["departmentId"] = "dep_y";
  pedido["obs"] = "para a obra";
  pedido["items"] = json::array({{{"id", "req1_i1"}, {"productId", "p1"}, {"qty", 2}}});
  json criada = json::parse(api.createRequest(pedido.dump()));
  CHECK(criada["departmentId"] == "dep_x");
  CHECK(criada["requesterName"] == "Pedro");
  CHECK(criada["status"] == "pendente");

  // sem permissão de validar, não aprova nem entrega
  CHECK_THROWS_AS(api.approveRequest("req1", "", kDepois), ForbiddenError);
  CHECK_THROWS_AS(api.deliverRequest("req1", kDepois, "m_e"), ForbiddenError);

  // a Ana, de outro setor, não enxerga o pedido do Pedro
  api.login("ana@flcondominios.com.br", "SenhaForte1", kNow);
  CHECK(json::parse(api.listRequestsJson()).empty());
  CHECK_THROWS_AS(api.cancelRequest("req1", "", kDepois), ForbiddenError);

  // o superadmin vê tudo, aprova e entrega
  api.login(kDefaultSuperadminEmail, "Mudar@2025", kNow);
  CHECK(json::parse(api.listRequestsJson()).size() == 1);
  api.approveRequest("req1", "ok", kDepois);
  json entregue = json::parse(api.deliverRequest("req1", kDepois, "m_ent"));
  CHECK(entregue["status"] == "entregue");

  json disponibilidade = json::parse(api.stockAvailabilityJson());
  CHECK(disponibilidade[0]["qty"].get<double>() == doctest::Approx(8));
  CHECK(disponibilidade[0]["reserved"].get<double>() == doctest::Approx(0));
}

TEST_CASE("quem não tem permissão de requisição não cria nem lê") {
  Api api(":memory:");
  api.loginAsService("teste");

  Department d;
  d.id = "dep_x";
  d.name = "Manutenção";
  d.encarregado = "Zé";
  d.createdAt = kNow;
  api.createDepartment(d);

  UserInput u;
  u.id = "u1";
  u.name = "Pedro";
  u.email = "pedro@flcondominios.com.br";
  u.role = kRoleUsuario;
  u.departmentId = "dep_x";
  u.createdAt = kNow;
  u.password = "SenhaForte1";
  api.createUser(u);

  api.login("pedro@flcondominios.com.br", "SenhaForte1", kNow);
  CHECK_THROWS_AS(api.listRequestsJson(), ForbiddenError);
  CHECK_THROWS_AS(api.createRequest("{\"id\":\"r\",\"items\":[]}"), ForbiddenError);
  CHECK_THROWS_AS(api.stockAvailabilityJson(), ForbiddenError);
}
