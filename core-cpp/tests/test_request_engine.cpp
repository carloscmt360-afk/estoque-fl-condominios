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
    p.category = "Papelaria";
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
  p2.category = "Papelaria";
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
  p2.category = "Papelaria";
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

  // A Api confere a janela contra o RELÓGIO DO SISTEMA (não contra kNow), então
  // um teste que cria requisição por ela precisa de uma janela que cubra o
  // instante real da execução — daí o período propositalmente absurdo.
  json janelaAmpla;
  janelaAmpla["id"] = "jan_teste";
  janelaAmpla["opensAt"] = "2000-01-01T00:00:00.000Z";
  janelaAmpla["closesAt"] = "2099-12-31T23:59:59.999Z";
  api.createRequestWindow(janelaAmpla.dump());

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
  p.category = "Papelaria";
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

// ------------------------------------------------------- janela de pedidos

namespace {

// Datas em torno de kNow (10/08 12:00) para montar janelas passadas, valendo
// agora e futuras sem depender do relógio da máquina que roda o teste.
constexpr const char* kOntem = "2026-08-09T12:00:00.000Z";
constexpr const char* kHojeCedo = "2026-08-10T08:00:00.000Z";
constexpr const char* kHojeTarde = "2026-08-10T18:00:00.000Z";
constexpr const char* kAmanha = "2026-08-11T08:00:00.000Z";
constexpr const char* kAmanhaTarde = "2026-08-11T18:00:00.000Z";

RequestWindow janela(const std::string& id, const std::string& abre, const std::string& fecha) {
  RequestWindow w;
  w.id = id;
  w.opensAt = abre;
  w.closesAt = fecha;
  w.createdByName = "Carlos";
  return w;
}

}  // namespace

TEST_CASE("banco sem nenhuma janela cadastrada não aceita requisição") {
  Cenario c;
  CHECK_FALSE(openRequestWindowAt(c.db, kNow).has_value());
  CHECK_THROWS_AS(requireOpenRequestWindow(c.db, kNow), std::invalid_argument);
}

TEST_CASE("a janela vale exatamente do instante de abertura ao de fechamento") {
  Cenario c;
  createRequestWindow(c.db, janela("j1", kHojeCedo, kHojeTarde), kOntem);

  // Antes de abrir: fechado.
  CHECK_FALSE(openRequestWindowAt(c.db, kOntem).has_value());
  // O instante da abertura JÁ vale (limite fechado à esquerda).
  CHECK(openRequestWindowAt(c.db, kHojeCedo).has_value());
  CHECK(openRequestWindowAt(c.db, kNow).has_value());
  // O instante do fechamento NÃO vale mais (limite aberto à direita) — senão
  // um pedido feito no milissegundo do prazo entraria fora dele.
  CHECK_FALSE(openRequestWindowAt(c.db, kHojeTarde).has_value());
  CHECK_NOTHROW(requireOpenRequestWindow(c.db, kNow));
  CHECK_THROWS_AS(requireOpenRequestWindow(c.db, kHojeTarde), std::invalid_argument);
}

TEST_CASE("passado o prazo o sistema tranca sozinho, sem ninguém mexer no banco") {
  Cenario c;
  createRequestWindow(c.db, janela("j1", kHojeCedo, kHojeTarde), kOntem);
  CHECK_NOTHROW(requireOpenRequestWindow(c.db, kNow));

  // Nada é atualizado entre uma chamada e outra: a MESMA linha do banco
  // responde "aberta" às 12h e "fechada" às 18h, só porque a pergunta mudou.
  CHECK_THROWS_AS(requireOpenRequestWindow(c.db, kAmanha), std::invalid_argument);
  auto ainda = findRequestWindow(c.db, "j1");
  REQUIRE(ainda.has_value());
  CHECK(ainda->closedAt.empty());
}

TEST_CASE("fora do prazo a mensagem aponta a próxima janela quando existe uma") {
  Cenario c;
  createRequestWindow(c.db, janela("j1", kAmanha, kAmanhaTarde), kNow);

  auto proxima = nextRequestWindowAfter(c.db, kNow);
  REQUIRE(proxima.has_value());
  CHECK(proxima->id == "j1");

  try {
    requireOpenRequestWindow(c.db, kNow);
    FAIL("deveria ter recusado: a janela ainda não abriu");
  } catch (const std::invalid_argument& e) {
    CHECK(std::string(e.what()).find(kAmanha) != std::string::npos);
  }
}

TEST_CASE("encerrar antes do prazo fecha as requisições na hora") {
  Cenario c;
  createRequestWindow(c.db, janela("j1", kHojeCedo, kHojeTarde), kOntem);
  CHECK_NOTHROW(requireOpenRequestWindow(c.db, kNow));

  closeRequestWindowNow(c.db, "j1", c.admin, kNow);

  // Fechada a partir de kNow, mesmo faltando horas para kHojeTarde.
  CHECK_THROWS_AS(requireOpenRequestWindow(c.db, kNow), std::invalid_argument);
  // E continua valendo para o passado dela: quem pediu às 8h pediu dentro.
  CHECK(openRequestWindowAt(c.db, kHojeCedo).has_value());
  // Encerrar de novo não faz sentido.
  CHECK_THROWS_AS(closeRequestWindowNow(c.db, "j1", c.admin, kNow), std::invalid_argument);
}

TEST_CASE("período inválido é recusado na abertura da janela") {
  Cenario c;
  // Fechamento antes da abertura.
  CHECK_THROWS_AS(createRequestWindow(c.db, janela("a", kHojeTarde, kHojeCedo), kOntem),
                  std::invalid_argument);
  // Fechamento igual à abertura (janela de duração zero).
  CHECK_THROWS_AS(createRequestWindow(c.db, janela("b", kHojeCedo, kHojeCedo), kOntem),
                  std::invalid_argument);
  // Janela que já nasceria encerrada.
  CHECK_THROWS_AS(createRequestWindow(c.db, janela("c", kOntem, kHojeCedo), kNow),
                  std::invalid_argument);
}

TEST_CASE("duas janelas não podem valer ao mesmo tempo") {
  Cenario c;
  createRequestWindow(c.db, janela("j1", kHojeCedo, kAmanhaTarde), kOntem);
  CHECK_THROWS_AS(createRequestWindow(c.db, janela("j2", kNow, kAmanha), kOntem),
                  std::invalid_argument);

  // Mas encerrar a primeira devolve o período restante: a segunda janela
  // passa a caber onde a primeira teria ficado.
  closeRequestWindowNow(c.db, "j1", c.admin, kNow);
  CHECK_NOTHROW(createRequestWindow(c.db, janela("j2", kHojeTarde, kAmanhaTarde), kNow));
}

TEST_CASE("janela que já começou não pode ser apagada, só encerrada") {
  Cenario c;
  createRequestWindow(c.db, janela("passada", kHojeCedo, kHojeTarde), kOntem);
  createRequestWindow(c.db, janela("futura", kAmanha, kAmanhaTarde), kOntem);

  CHECK_THROWS_AS(deleteRequestWindow(c.db, "passada", kNow), std::invalid_argument);
  CHECK_NOTHROW(deleteRequestWindow(c.db, "futura", kNow));
  CHECK_FALSE(findRequestWindow(c.db, "futura").has_value());
  CHECK(findRequestWindow(c.db, "passada").has_value());
}

TEST_CASE("pela Api, sem janela aberta a requisição é recusada — e o status diz por quê") {
  Api api(":memory:");
  api.loginAsService("teste");

  Department d;
  d.id = "dep_x";
  d.name = "Manutenção";
  d.encarregado = "Zé";
  d.createdAt = kNow;
  api.createDepartment(d);

  Product p;
  p.id = "p1";
  p.name = "Papel A4";
  p.unit = "Resma";
  p.category = "Papelaria";
  p.createdAt = kNow;
  api.createProduct(p);
  api.applyEntrada("m_ini", "p1", 10, 20.0, "Fornecedor", "NF1", kNow, "", kNow);

  json pedido;
  pedido["id"] = "req1";
  pedido["createdAt"] = kNow;
  pedido["departmentId"] = "dep_x";
  pedido["items"] = json::array({{{"id", "req1_i1"}, {"productId", "p1"}, {"qty", 2}}});

  // Banco novo = nenhuma janela = fechado. Nem o superadministrador escapa: a
  // trava é de prazo, não de permissão.
  json status = json::parse(api.requestWindowStatusJson());
  CHECK(status["open"] == false);
  CHECK(status["current"].is_null());
  CHECK_THROWS_AS(api.createRequest(pedido.dump()), std::invalid_argument);

  // Aberta a janela, o mesmo pedido passa.
  json janela;
  janela["id"] = "jan1";
  janela["opensAt"] = "2000-01-01T00:00:00.000Z";
  janela["closesAt"] = "2099-12-31T23:59:59.999Z";
  janela["obs"] = "período de teste";
  api.createRequestWindow(janela.dump());

  status = json::parse(api.requestWindowStatusJson());
  CHECK(status["open"] == true);
  CHECK(status["current"]["obs"] == "período de teste");
  CHECK_NOTHROW(api.createRequest(pedido.dump()));

  // Encerrada a janela, volta a recusar — sem ninguém tocar em mais nada.
  api.closeRequestWindowNow("jan1");
  CHECK(json::parse(api.requestWindowStatusJson())["open"] == false);
  pedido["id"] = "req2";
  pedido["items"] = json::array({{{"id", "req2_i1"}, {"productId", "p1"}, {"qty", 1}}});
  CHECK_THROWS_AS(api.createRequest(pedido.dump()), std::invalid_argument);

  // A janela encerrada continua no histórico, com quem encerrou.
  json lista = json::parse(api.listRequestWindowsJson());
  REQUIRE(lista.size() == 1);
  CHECK(lista[0]["situacao"] == "encerrada");
  CHECK_FALSE(lista[0]["closedAt"].get<std::string>().empty());

  // E o backup carrega as janelas junto (senão restaurar deixaria o app
  // fechado para pedidos sem explicação).
  json backup = json::parse(api.backupJson());
  REQUIRE(backup.contains("requestWindows"));
  CHECK(backup["requestWindows"].size() == 1);
  CHECK(backup["requestWindows"][0]["id"] == "jan1");
}

TEST_CASE("quem não valida requisições não abre nem encerra janela") {
  Api api(":memory:");
  api.loginAsService("teste");

  Department d;
  d.id = "dep_x";
  d.name = "Manutenção";
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

  json janela;
  janela["id"] = "jan1";
  janela["opensAt"] = "2000-01-01T00:00:00.000Z";
  janela["closesAt"] = "2099-12-31T23:59:59.999Z";
  CHECK_THROWS_AS(api.createRequestWindow(janela.dump()), ForbiddenError);
  CHECK_THROWS_AS(api.listRequestWindowsJson(), ForbiddenError);
  CHECK_THROWS_AS(api.closeRequestWindowNow("jan1"), ForbiddenError);
  CHECK_THROWS_AS(api.deleteRequestWindow("jan1"), ForbiddenError);

  // Mas CONSULTAR o status ele pode: é o que explica na tela por que não dá
  // para pedir agora.
  json status = json::parse(api.requestWindowStatusJson());
  CHECK(status["open"] == false);
  CHECK(status["canManage"] == false);
}
