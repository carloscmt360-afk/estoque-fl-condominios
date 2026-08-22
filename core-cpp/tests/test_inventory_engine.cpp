#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/inventory_engine.hpp"

#include <algorithm>

using namespace estoque;

namespace {

Database freshDb() { return Database(":memory:"); }

Product makeProduct(const std::string& id, const std::string& name) {
  Product p;
  p.id = id;
  p.name = name;
  p.unit = "Unidade";
  p.minStock = 0;
  p.category = "Papelaria";  // createProduct exige uma das seis categorias válidas
  p.createdAt = "2026-01-01T00:00:00.000Z";
  return p;
}

Department makeDept(const std::string& id, const std::string& name) {
  Department d;
  d.id = id;
  d.name = name;
  d.encarregado = "Fulano";
  d.createdAt = "2026-01-01T00:00:00.000Z";
  return d;
}

}  // namespace

TEST_CASE("createProduct começa com qty=0 e avgCost=0, mesmo se o input trouxer outro valor") {
  auto db = freshDb();
  Product input = makeProduct("p1", "Papel A4");
  input.qty = 999;
  input.avgCost = 123;

  Product created = createProduct(db, input);
  CHECK(created.qty == 0);
  CHECK(created.avgCost == 0);

  auto fetched = findProduct(db, "p1");
  REQUIRE(fetched.has_value());
  CHECK(fetched->qty == 0);
  CHECK(fetched->avgCost == 0);
}

TEST_CASE("applyEntrada calcula o custo médio ponderado móvel corretamente") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));

  auto m1 = applyEntrada(db, "m1", "p1", /*qty*/ 10, /*preco*/ 20.0, "Fornecedor X", "NF1",
                         "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  CHECK(m1.resultingQty == 10);
  CHECK(m1.resultingAvgCost == doctest::Approx(20.0));

  // segunda entrada com preço diferente -> média ponderada, não média simples
  auto m2 = applyEntrada(db, "m2", "p1", /*qty*/ 10, /*preco*/ 30.0, "Fornecedor Y", "NF2",
                         "2026-06-02T00:00:00.000Z", "", "2026-06-02T00:00:00.000Z");
  // (10*20 + 10*30) / 20 = 25
  CHECK(m2.resultingQty == 20);
  CHECK(m2.resultingAvgCost == doctest::Approx(25.0));

  auto p = findProduct(db, "p1");
  REQUIRE(p.has_value());
  CHECK(p->qty == 20);
  CHECK(p->avgCost == doctest::Approx(25.0));
}

TEST_CASE("applyEntrada com saldo negativo não usa o saldo negativo na ponderação") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Item com saldo ruim"));
  // força um saldo negativo direto no banco (simula dado importado malformado,
  // ex.: saída lançada sem entrada correspondente)
  db.prepare("UPDATE products SET qty=?, avg_cost=? WHERE id=?").bind(1, -5.0).bind(2, 10.0).bind(3, std::string("p1")).step();

  auto m = applyEntrada(db, "m1", "p1", /*qty*/ 5, /*preco*/ 50.0, "Fornecedor X", "NF1",
                        "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");

  // se o saldo negativo entrasse na ponderação: (-5*10 + 5*50)/(-5+5) -> divisão por zero
  // com a proteção max(qty,0): (0*10 + 5*50)/(0+5) = 50
  CHECK(m.resultingQty == 0);  // -5 + 5
  CHECK(m.resultingAvgCost == doctest::Approx(50.0));
}

TEST_CASE("applyEntrada com saldo negativo e qty>0 usa só a entrada na ponderação (baseQty vira 0)") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Item zerado"));
  db.prepare("UPDATE products SET qty=?, avg_cost=? WHERE id=?").bind(1, -3.0).bind(2, 7.0).bind(3, std::string("p1")).step();

  auto m = applyEntrada(db, "m1", "p1", /*qty*/ 3, /*preco*/ 99.0, "F", "NF",
                        "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  CHECK(m.resultingQty == 0);  // -3 + 3
  // baseQty=max(-3,0)=0 -> (0*7 + 3*99)/(0+3) = 99 (o saldo negativo de 7 é descartado, não usado)
  CHECK(m.resultingAvgCost == doctest::Approx(99.0));
}

TEST_CASE("applyEntrada com base+qty==0 (entrada de 0 unidades) não divide por zero") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Item zerado"));
  db.prepare("UPDATE products SET qty=?, avg_cost=? WHERE id=?").bind(1, -3.0).bind(2, 7.0).bind(3, std::string("p1")).step();

  // caso defensivo: a UI normalmente rejeita qty<=0 antes de chegar aqui, mas
  // o núcleo não deve nunca produzir NaN/Inf se for chamado mesmo assim.
  auto m = applyEntrada(db, "m1", "p1", /*qty*/ 0, /*preco*/ 99.0, "F", "NF",
                        "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  CHECK(m.resultingQty == -3.0);  // -3 + 0
  CHECK(m.resultingAvgCost == 0.0);  // baseQty(0) + qty(0) == 0 -> guarda contra divisão por zero
}

TEST_CASE("applySaida debita a quantidade e valoriza ao custo médio vigente, não muda o custo médio") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");

  auto saida = applySaida(db, "m2", "p1", 4, "d1", "2026-06-05T00:00:00.000Z", "uso teste", "Fulana",
                          "2026-06-05T00:00:00.000Z");

  CHECK(saida.unitPrice == doctest::Approx(20.0));  // valorizado ao custo médio, não a um preço arbitrário
  CHECK(saida.resultingQty == 6);
  CHECK(saida.resultingAvgCost == doctest::Approx(20.0));  // custo médio não muda numa saída
  CHECK(saida.recipient == "PASTAS");
  CHECK(saida.encarregado == "Fulano");

  auto p = findProduct(db, "p1");
  REQUIRE(p.has_value());
  CHECK(p->qty == 6);
  CHECK(p->avgCost == doctest::Approx(20.0));
}

TEST_CASE("applyCorrecao registra o delta como qty da movimentação, não o valor absoluto") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");

  auto ajuste = applyCorrecao(db, "m2", "p1", /*qtyReal*/ 7, "contagem física mensal",
                              "2026-06-10T00:00:00.000Z", "2026-06-10T00:00:00.000Z");

  CHECK(ajuste.qty == doctest::Approx(-3.0));  // 7 - 10
  CHECK(ajuste.resultingQty == 7);
  CHECK(ajuste.resultingAvgCost == doctest::Approx(20.0));  // custo médio não muda num ajuste

  auto p = findProduct(db, "p1");
  REQUIRE(p.has_value());
  CHECK(p->qty == 7);
  CHECK(p->avgCost == doctest::Approx(20.0));
}

TEST_CASE("applyEntrada/applySaida/applyCorrecao lançam NotFoundError para produto inexistente") {
  auto db = freshDb();
  CHECK_THROWS_AS(
      applyEntrada(db, "m1", "inexistente", 1, 1, "F", "NF", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z"),
      NotFoundError);
  CHECK_THROWS_AS(
      applyCorrecao(db, "m2", "inexistente", 1, "motivo", "2026-01-01T00:00:00.000Z", "2026-01-01T00:00:00.000Z"),
      NotFoundError);
}

TEST_CASE("applySaida lança NotFoundError para departamento inexistente") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  CHECK_THROWS_AS(
      applySaida(db, "m2", "p1", 1, "dept-inexistente", "2026-06-01T00:00:00.000Z", "", "", "2026-06-01T00:00:00.000Z"),
      NotFoundError);
}

TEST_CASE("deleteProduct também remove as movimentações do produto") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");

  deleteProduct(db, "p1");

  CHECK(!findProduct(db, "p1").has_value());
  auto st = db.prepare("SELECT COUNT(*) FROM movements WHERE product_id=?");
  st.bind(1, std::string("p1"));
  st.step();
  CHECK(st.columnDouble(0) == 0);
}

TEST_CASE("deleteDepartment preserva o histórico de movimentações (espelha o app web)") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applySaida(db, "m2", "p1", 2, "d1", "2026-06-02T00:00:00.000Z", "", "Fulana", "2026-06-02T00:00:00.000Z");

  deleteDepartment(db, "d1");

  CHECK(!findDepartment(db, "d1").has_value());
  auto st = db.prepare("SELECT COUNT(*) FROM movements WHERE department_id=?");
  st.bind(1, std::string("d1"));
  st.step();
  CHECK(st.columnDouble(0) == 1);  // a movimentação continua existindo
}

// ===================================================================
// Edição / exclusão de lançamentos + reprocessamento cronológico
// ===================================================================

namespace {

// Patch pré-preenchido com o estado ATUAL do lançamento — os testes só
// sobrescrevem o campo em exame, como faz o formulário de edição.
MovementPatch patchOf(Database& db, const std::string& movementId) {
  auto m = findMovement(db, movementId);
  REQUIRE(m.has_value());
  MovementPatch p;
  p.id = m->id;
  p.qty = m->qty;
  p.qtyReal = m->resultingQty;
  p.unitPrice = m->unitPrice;
  p.supplier = m->supplier;
  p.nf = m->nf;
  p.departmentId = m->departmentId;
  p.requester = m->requester;
  p.obs = m->obs;
  p.date = m->date;
  return p;
}

double productQty(Database& db, const std::string& id) { return findProduct(db, id)->qty; }
double productAvg(Database& db, const std::string& id) { return findProduct(db, id)->avgCost; }

}  // namespace

TEST_CASE("editar a quantidade de uma entrada revaloriza o custo médio e as saídas posteriores") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF1", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applyEntrada(db, "m2", "p1", 10, 30.0, "F", "NF2", "2026-06-02T00:00:00.000Z", "", "2026-06-02T00:00:00.000Z");
  applySaida(db, "m3", "p1", 4, "d1", "2026-06-05T00:00:00.000Z", "", "Fulana", "2026-06-05T00:00:00.000Z");
  CHECK(productAvg(db, "p1") == doctest::Approx(25.0));  // (10*20 + 10*30)/20
  CHECK(productQty(db, "p1") == doctest::Approx(16.0));

  // a segunda nota era de 30 unidades, não 10
  auto patch = patchOf(db, "m2");
  patch.qty = 30;
  auto updated = updateMovement(db, patch);

  CHECK(updated.qty == doctest::Approx(30.0));
  // (10*20 + 30*30)/40 = 27,5
  CHECK(updated.resultingAvgCost == doctest::Approx(27.5));
  CHECK(productQty(db, "p1") == doctest::Approx(36.0));  // 10 + 30 - 4
  CHECK(productAvg(db, "p1") == doctest::Approx(27.5));

  // a saída posterior foi revalorizada pelo novo custo médio
  auto saida = findMovement(db, "m3");
  REQUIRE(saida.has_value());
  CHECK(saida->resultingQty == doctest::Approx(36.0));
  CHECK(saida->resultingAvgCost == doctest::Approx(27.5));
}

TEST_CASE("recomputeProduct NÃO reescreve o unit_price de uma saída (preserva o preço da planilha de origem)") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF1", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applySaida(db, "m2", "p1", 2, "d1", "2026-06-05T00:00:00.000Z", "", "", "2026-06-05T00:00:00.000Z");
  // simula dado importado: a saída foi gravada com o preço histórico da
  // planilha (7,00), diferente do custo médio calculado (20,00) — é essa
  // diferença que a conferência do relatório mensal existe para apontar
  db.prepare("UPDATE movements SET unit_price=? WHERE id=?").bind(1, 7.0).bind(2, std::string("m2")).step();

  // qualquer edição em outro lançamento dispara o reprocessamento do produto
  auto patch = patchOf(db, "m1");
  patch.obs = "corrigindo só a observação";
  updateMovement(db, patch);

  auto saida = findMovement(db, "m2");
  REQUIRE(saida.has_value());
  CHECK(saida->unitPrice == doctest::Approx(7.0));          // preservado
  CHECK(saida->resultingAvgCost == doctest::Approx(20.0));  // o custo médio, esse sim, é derivado
}

TEST_CASE("editar a data joga o lançamento para o lugar certo da linha do tempo") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 10.0, "F", "NF1", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applyEntrada(db, "m2", "p1", 10, 50.0, "F", "NF2", "2026-06-10T00:00:00.000Z", "", "2026-06-10T00:00:00.000Z");

  // a segunda nota é, na verdade, anterior à primeira
  auto patch = patchOf(db, "m2");
  patch.date = "2026-05-01T00:00:00.000Z";
  updateMovement(db, patch);

  // o custo médio final é o mesmo (ponderação é comutativa aqui), mas a
  // posição intermediária de cada lançamento muda de lugar
  auto primeira = findMovement(db, "m2");  // agora é a PRIMEIRA da linha do tempo
  REQUIRE(primeira.has_value());
  CHECK(primeira->resultingQty == doctest::Approx(10.0));
  CHECK(primeira->resultingAvgCost == doctest::Approx(50.0));

  auto segunda = findMovement(db, "m1");
  REQUIRE(segunda.has_value());
  CHECK(segunda->resultingQty == doctest::Approx(20.0));
  CHECK(segunda->resultingAvgCost == doctest::Approx(30.0));  // (10*50 + 10*10)/20
}

TEST_CASE("editar uma saída troca o departamento e redenormaliza nome e encarregado") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  Department d2 = makeDept("d2", "GERÊNCIA");
  d2.encarregado = "Sicrano";
  createDepartment(db, d2);
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applySaida(db, "m2", "p1", 3, "d1", "2026-06-05T00:00:00.000Z", "", "Fulana", "2026-06-05T00:00:00.000Z");

  auto patch = patchOf(db, "m2");
  patch.departmentId = "d2";
  patch.qty = 5;
  patch.requester = "Beltrana";
  auto updated = updateMovement(db, patch);

  CHECK(updated.departmentId == "d2");
  CHECK(updated.recipient == "GERÊNCIA");
  CHECK(updated.encarregado == "Sicrano");
  CHECK(updated.requester == "Beltrana");
  CHECK(updated.resultingQty == doctest::Approx(5.0));  // 10 - 5
  CHECK(productQty(db, "p1") == doctest::Approx(5.0));
}

TEST_CASE("editar um ajuste usa a quantidade CONTADA, não o delta") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  auto ajuste = applyCorrecao(db, "m2", "p1", 7, "contagem", "2026-06-10T00:00:00.000Z", "2026-06-10T00:00:00.000Z");
  CHECK(ajuste.qty == doctest::Approx(-3.0));

  // recontagem: eram 9, não 7
  auto patch = patchOf(db, "m2");
  patch.qtyReal = 9;
  patch.obs = "recontagem";
  auto updated = updateMovement(db, patch);

  CHECK(updated.qty == doctest::Approx(-1.0));  // 9 - 10
  CHECK(updated.resultingQty == doctest::Approx(9.0));
  CHECK(updated.obs == "recontagem");
  CHECK(productQty(db, "p1") == doctest::Approx(9.0));
}

TEST_CASE("ajuste retroativo mede o delta contra o saldo DA DATA dele, não o de hoje") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applyEntrada(db, "m2", "p1", 100, 20.0, "F", "NF", "2026-06-20T00:00:00.000Z", "", "2026-06-20T00:00:00.000Z");

  // contagem física do dia 05: havia 8 unidades (o saldo naquele dia era 10)
  auto ajuste = applyCorrecao(db, "m3", "p1", 8, "contagem de 05/06", "2026-06-05T00:00:00.000Z",
                              "2026-06-25T00:00:00.000Z");

  CHECK(ajuste.qty == doctest::Approx(-2.0));          // 8 - 10, e NÃO 8 - 110
  CHECK(ajuste.resultingQty == doctest::Approx(8.0));  // saldo logo após o ajuste
  CHECK(productQty(db, "p1") == doctest::Approx(108.0));  // 8 + os 100 que entraram depois
}

TEST_CASE("excluir um lançamento reprocessa a cadeia inteira do produto") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF1", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applyEntrada(db, "m2", "p1", 10, 40.0, "F", "NF2", "2026-06-02T00:00:00.000Z", "", "2026-06-02T00:00:00.000Z");
  applySaida(db, "m3", "p1", 5, "d1", "2026-06-05T00:00:00.000Z", "", "", "2026-06-05T00:00:00.000Z");
  CHECK(productAvg(db, "p1") == doctest::Approx(30.0));

  deleteMovement(db, "m2");  // a nota 2 era duplicada

  CHECK(!findMovement(db, "m2").has_value());
  CHECK(productQty(db, "p1") == doctest::Approx(5.0));   // 10 - 5
  CHECK(productAvg(db, "p1") == doctest::Approx(20.0));  // só a nota 1 pondera

  auto saida = findMovement(db, "m3");
  REQUIRE(saida.has_value());
  CHECK(saida->resultingQty == doctest::Approx(5.0));
  CHECK(saida->resultingAvgCost == doctest::Approx(20.0));
}

TEST_CASE("edição preserva a divergência que já existia entre o razão e o saldo gravado") {
  // Reproduz o caso real dos dados do usuário (13 de 157 produtos): a
  // planilha de origem tem saídas sem a entrada correspondente, então o
  // saldo gravado no produto não bate com o replay do razão. Uma edição
  // deve mover o saldo só pelo efeito DELA — nunca re-basear o produto.
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "PILHA AA"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  applyEntrada(db, "m1", "p1", 100, 2.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  // saldo gravado passa a divergir do razão em -80 (contagem física da carga
  // inicial, gravada direto na tabela como fez a importação)
  db.prepare("UPDATE products SET qty=? WHERE id=?").bind(1, 21.0).bind(2, std::string("p1")).step();
  REQUIRE(productQty(db, "p1") == doctest::Approx(21.0));

  // edição que não muda quantidade nenhuma: o saldo não pode se mexer
  auto soObs = patchOf(db, "m1");
  soObs.obs = "conferido";
  updateMovement(db, soObs);
  CHECK(productQty(db, "p1") == doctest::Approx(21.0));  // e NÃO 100

  // agora uma edição com efeito real: a nota era de 90, não 100 (-10)
  auto patch = patchOf(db, "m1");
  patch.qty = 90;
  updateMovement(db, patch);
  CHECK(productQty(db, "p1") == doctest::Approx(11.0));  // 21 - 10, exatamente o efeito da edição

  // e o mesmo vale para exclusão e para lançamentos novos
  applySaida(db, "m2", "p1", 1, "d1", "2026-06-07T00:00:00.000Z", "", "", "2026-06-07T00:00:00.000Z");
  CHECK(productQty(db, "p1") == doctest::Approx(10.0));
  deleteMovement(db, "m2");
  CHECK(productQty(db, "p1") == doctest::Approx(11.0));
}

TEST_CASE("updateMovement recusa quantidade não positiva em entrada e saída") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applySaida(db, "m2", "p1", 3, "d1", "2026-06-05T00:00:00.000Z", "", "", "2026-06-05T00:00:00.000Z");

  auto entrada = patchOf(db, "m1");
  entrada.qty = 0;
  CHECK_THROWS_AS(updateMovement(db, entrada), std::invalid_argument);

  auto saida = patchOf(db, "m2");
  saida.qty = -1;
  CHECK_THROWS_AS(updateMovement(db, saida), std::invalid_argument);

  // a transação foi revertida: nada mudou
  CHECK(productQty(db, "p1") == doctest::Approx(7.0));
}

TEST_CASE("updateMovement/deleteMovement lançam NotFoundError para id inexistente") {
  auto db = freshDb();
  MovementPatch p;
  p.id = "nao-existe";
  p.qty = 1;
  CHECK_THROWS_AS(updateMovement(db, p), NotFoundError);
  CHECK_THROWS_AS(deleteMovement(db, "nao-existe"), NotFoundError);
}

TEST_CASE("editar uma saída para um departamento inexistente falha sem alterar nada") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applySaida(db, "m2", "p1", 3, "d1", "2026-06-05T00:00:00.000Z", "", "", "2026-06-05T00:00:00.000Z");

  auto patch = patchOf(db, "m2");
  patch.departmentId = "d-inexistente";
  patch.qty = 9;
  CHECK_THROWS_AS(updateMovement(db, patch), NotFoundError);

  auto saida = findMovement(db, "m2");
  REQUIRE(saida.has_value());
  CHECK(saida->qty == doctest::Approx(3.0));  // rollback
  CHECK(saida->recipient == "PASTAS");
  CHECK(productQty(db, "p1") == doctest::Approx(7.0));
}

TEST_CASE("listMovements devolve a linha do tempo em ordem cronológica, não de digitação") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 1, 1.0, "F", "NF", "2026-06-10T00:00:00.000Z", "", "2026-06-20T00:00:00.000Z");
  applyEntrada(db, "m2", "p1", 1, 1.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-21T00:00:00.000Z");

  auto all = listMovements(db);
  REQUIRE(all.size() == 2);
  CHECK(all[0].id == "m2");  // lançado depois, mas datado antes
  CHECK(all[1].id == "m1");
}

TEST_CASE("applySaida além do saldo disponível lança e não altera nada") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "PASTAS"));
  applyEntrada(db, "m1", "p1", 5, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");

  CHECK_THROWS_AS(
      applySaida(db, "m2", "p1", 9, "d1", "2026-06-05T00:00:00.000Z", "", "", "2026-06-05T00:00:00.000Z"),
      std::invalid_argument);

  CHECK(productQty(db, "p1") == doctest::Approx(5.0));  // rollback: saída não ficou gravada
  CHECK_FALSE(findMovement(db, "m2").has_value());
}

TEST_CASE("applyCorrecao com newAvgCost corrige o custo médio sem mexer no saldo") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");

  auto ajuste = applyCorrecao(db, "m2", "p1", /*qtyReal*/ 10, "correção de custo médio",
                              "2026-06-10T00:00:00.000Z", "2026-06-10T00:00:00.000Z", /*newAvgCost*/ 35.0);

  CHECK(ajuste.qty == doctest::Approx(0.0));  // saldo não muda

  auto p = findProduct(db, "p1");
  REQUIRE(p.has_value());
  CHECK(p->qty == doctest::Approx(10.0));
  CHECK(p->avgCost == doctest::Approx(35.0));
}

TEST_CASE("applyCorrecao sem newAvgCost preserva o comportamento antigo (custo não muda)") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");

  applyCorrecao(db, "m2", "p1", 8, "contagem", "2026-06-10T00:00:00.000Z", "2026-06-10T00:00:00.000Z");

  auto p = findProduct(db, "p1");
  REQUIRE(p.has_value());
  CHECK(p->avgCost == doctest::Approx(20.0));
}

TEST_CASE("repairNegativeBalances zera saldo negativo herdado e é idempotente") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createProduct(db, makeProduct("p2", "Caneta"));
  // Simula dado legado com saldo negativo: escreve direto, contornando a
  // trava (ela só existe a partir de agora — dados antigos já ficaram assim).
  db.execute(
      "INSERT INTO movements (id, type, product_id, qty, unit_price, date, resulting_qty, resulting_avg_cost, "
      "created_at) VALUES ('legado', 'saida', 'p1', 4, 10.0, '2026-05-01T00:00:00.000Z', -4, 10.0, "
      "'2026-05-01T00:00:00.000Z')");
  db.execute("UPDATE products SET qty = -4, avg_cost = 10.0 WHERE id = 'p1'");

  auto fixed = repairNegativeBalances(db, "2026-08-12T12:00:00.000Z");
  REQUIRE(fixed.size() == 1);
  CHECK(fixed[0] == "p1");
  CHECK(productQty(db, "p1") == doctest::Approx(0.0));
  CHECK(productQty(db, "p2") == doctest::Approx(0.0));  // nunca esteve negativo, não mexe

  auto again = repairNegativeBalances(db, "2026-08-12T12:00:00.000Z");
  CHECK(again.empty());  // já rodou uma vez, não repete
}

TEST_CASE("reconcileAvgCost corrige avg_cost divergente do razão (dado vindo de backup) e preserva o offset de saldo") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Saco 4 furos"));
  applyEntrada(db, "m1", "p1", 25, 4.60, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");

  // Simula exatamente o que Api::restoreFromJson faz: grava products.avg_cost
  // e movements.unit_price/resulting_avg_cost DIRETO, sem passar por
  // recomputeProduct — como um backup cuja planilha de origem trouxe um
  // avg_cost (19,90) que não bate com o preço da própria entrada (4,60).
  db.execute("UPDATE products SET avg_cost = 19.90 WHERE id = 'p1'");
  db.execute("UPDATE movements SET resulting_avg_cost = 19.90 WHERE id = 'm1'");

  // p2 tem histórico de compra real (avg_cost pelo razão = 10.0), MAS além
  // disso um avg_cost corrompido (mesmo cenário de p1) E um saldo com offset
  // herdado LEGÍTIMO (mesmo cenário do comentário de recomputeProduct: saída
  // sem entrada correspondente na planilha de origem) — reconcileAvgCost
  // precisa corrigir o custo SEM "corrigir" o saldo junto.
  createProduct(db, makeProduct("p2", "Item legado com saída sem entrada"));
  applyEntrada(db, "m2", "p2", 10, 10.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  db.execute("UPDATE products SET qty = 7, avg_cost = 99.0 WHERE id = 'p2'");  // 7 = 10 (razão) - 3 (offset herdado)

  auto p3 = createProduct(db, makeProduct("p3", "Produto novo, nunca movimentado"));
  (void)p3;  // avg_cost = 0 e razão = 0: nada a reconciliar, não pode virar "corrigido"

  auto fixed = reconcileAvgCost(db);
  CHECK(std::find(fixed.begin(), fixed.end(), "p1") != fixed.end());
  CHECK(std::find(fixed.begin(), fixed.end(), "p2") != fixed.end());
  CHECK(std::find(fixed.begin(), fixed.end(), "p3") == fixed.end());

  auto p1Depois = findProduct(db, "p1");
  REQUIRE(p1Depois.has_value());
  CHECK(p1Depois->avgCost == doctest::Approx(4.60));  // volta a bater com a entrada real
  CHECK(p1Depois->qty == doctest::Approx(25.0));       // saldo não tinha divergência — não muda

  auto p2Depois = findProduct(db, "p2");
  REQUIRE(p2Depois.has_value());
  CHECK(p2Depois->avgCost == doctest::Approx(10.0));  // custo reconciliado com o razão
  CHECK(p2Depois->qty == doctest::Approx(7.0));       // offset de saldo herdado PRESERVADO, não "corrigido" para 10

  auto again = reconcileAvgCost(db);
  CHECK(again.empty());  // já reconciliado, idempotente
}

// ------------------------------------------------------------------- SKU

TEST_CASE("createProduct atribui SKU sequencial no formato CMT######") {
  auto db = freshDb();
  auto a = createProduct(db, makeProduct("p1", "Papel A4"));
  auto b = createProduct(db, makeProduct("p2", "Caneta"));
  auto c = createProduct(db, makeProduct("p3", "Grampeador"));
  CHECK(a.sku == "CMT000001");
  CHECK(b.sku == "CMT000002");
  CHECK(c.sku == "CMT000003");
}

TEST_CASE("SKU de produto excluído nunca é reaproveitado") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));  // CMT000001
  auto b = createProduct(db, makeProduct("p2", "Caneta"));  // CMT000002
  CHECK(b.sku == "CMT000002");

  deleteProduct(db, "p2");
  auto c = createProduct(db, makeProduct("p3", "Grampeador"));
  CHECK(c.sku == "CMT000003");  // não volta a CMT000002
}

TEST_CASE("updateProduct nunca altera o SKU") {
  auto db = freshDb();
  auto created = createProduct(db, makeProduct("p1", "Papel A4"));
  Product edit = created;
  edit.name = "Papel A4 Reciclado";
  edit.sku = "CMT999999";  // tentativa de forçar outro SKU: deve ser ignorada
  auto updated = updateProduct(db, edit);
  CHECK(updated.sku == created.sku);
  CHECK(updated.name == "Papel A4 Reciclado");
}

TEST_CASE("SKU duplicado é recusado pelo índice único") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));  // CMT000001
  CHECK_THROWS_AS(
      db.prepare("INSERT INTO products (id, name, unit, category, created_at, sku) "
                 "VALUES ('p2', 'Outro', 'Unidade', 'Papelaria', '2026-01-01T00:00:00.000Z', 'CMT000001')")
          .step(),
      SqlError);
}

TEST_CASE("backfillMissingSkus preenche em ordem de criação sem tocar em quem já tem") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));  // CMT000001

  // Simula produtos legados sem SKU (INSERT direto, contornando createProduct).
  db.execute(
      "INSERT INTO products (id, name, unit, category, created_at) "
      "VALUES ('legado1', 'Zebra', 'Unidade', 'Papelaria', '2025-01-01T00:00:00.000Z')");
  db.execute(
      "INSERT INTO products (id, name, unit, category, created_at) "
      "VALUES ('legado2', 'Abacate', 'Unidade', 'Papelaria', '2025-06-01T00:00:00.000Z')");

  backfillMissingSkus(db);

  CHECK(findProduct(db, "p1")->sku == "CMT000001");  // já tinha, não mudou
  CHECK(findProduct(db, "legado1")->sku == "CMT000002");  // mais antigo primeiro
  CHECK(findProduct(db, "legado2")->sku == "CMT000003");

  auto again = createProduct(db, makeProduct("p2", "Novo"));
  CHECK(again.sku == "CMT000004");  // contador continua de onde parou
}

// --------------------------------------------------------------- Categoria

TEST_CASE("createProduct/updateProduct recusam categoria fora das seis válidas") {
  auto db = freshDb();
  Product p = makeProduct("p1", "Papel A4");
  p.category = "Jardinagem";
  CHECK_THROWS_AS(createProduct(db, p), std::invalid_argument);

  p.category = "";
  CHECK_THROWS_AS(createProduct(db, p), std::invalid_argument);

  p.category = "Não Classificado";
  CHECK_THROWS_AS(createProduct(db, p), std::invalid_argument);

  auto created = createProduct(db, makeProduct("p2", "Caneta"));  // categoria válida
  Product edit = created;
  edit.category = "Jardinagem";
  CHECK_THROWS_AS(updateProduct(db, edit), std::invalid_argument);
}

TEST_CASE("classifyLegacyCategories reclassifica por palavra-chave e marca o resto como Não Classificado") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));  // já válido (Papelaria) — não deve mexer

  db.execute(
      "INSERT INTO products (id, name, unit, category, created_at) "
      "VALUES ('mouse1', 'Mouse sem fio', 'Unidade', '', '2026-01-01T00:00:00.000Z')");
  db.execute(
      "INSERT INTO products (id, name, unit, category, created_at) "
      "VALUES ('urna1', 'Urna de votação', 'Unidade', NULL, '2026-01-01T00:00:00.000Z')");
  db.execute(
      "INSERT INTO products (id, name, unit, category, created_at) "
      "VALUES ('legado1', 'Regador de jardim', 'Unidade', 'Jardinagem', '2026-01-01T00:00:00.000Z')");
  db.execute(
      "INSERT INTO products (id, name, unit, category, created_at) "
      "VALUES ('grafica1', 'Cartão de Visita Premium', 'Unidade', 'gráfica', '2026-01-01T00:00:00.000Z')");

  int n = classifyLegacyCategories(db);
  CHECK(n == 4);  // mouse1, urna1, legado1, grafica1 — p1 já era válido

  CHECK(findProduct(db, "p1")->category == "Papelaria");
  CHECK(findProduct(db, "mouse1")->category == "Informática");
  CHECK(findProduct(db, "urna1")->category == "Assembleia");
  CHECK(findProduct(db, "legado1")->category == "Não Classificado");  // nada bate — fica pra revisão
  CHECK(findProduct(db, "grafica1")->category == "Gráfica");  // categoria antiga já batia (case-insensitive)

  int again = classifyLegacyCategories(db);
  CHECK(again == 0);  // já rodou, não repete
}

// ------------------------------------------------------------------ Foto

TEST_CASE("setProductImage grava os caminhos sem mexer em mais nada do produto") {
  auto db = freshDb();
  auto created = createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");

  auto updated = setProductImage(db, "p1", "imagens/produtos/CMT000001/foto.webp",
                                 "imagens/produtos/CMT000001/thumb.webp");
  CHECK(updated.imagePath == "imagens/produtos/CMT000001/foto.webp");
  CHECK(updated.thumbnailPath == "imagens/produtos/CMT000001/thumb.webp");
  // nada mais do produto muda
  CHECK(updated.sku == created.sku);
  CHECK(updated.name == created.name);
  CHECK(updated.qty == doctest::Approx(10.0));
  CHECK(updated.avgCost == doctest::Approx(20.0));
}

TEST_CASE("clearProductImage volta o produto para sem foto") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  setProductImage(db, "p1", "imagens/produtos/CMT000001/foto.webp", "imagens/produtos/CMT000001/thumb.webp");

  auto cleared = clearProductImage(db, "p1");
  CHECK(cleared.imagePath.empty());
  CHECK(cleared.thumbnailPath.empty());
}

TEST_CASE("setProductImage/clearProductImage lançam NotFoundError para produto inexistente") {
  auto db = freshDb();
  CHECK_THROWS_AS(setProductImage(db, "inexistente", "x", "y"), NotFoundError);
  CHECK_THROWS_AS(clearProductImage(db, "inexistente"), NotFoundError);
}

TEST_CASE("produto sem foto tem imagePath/thumbnailPath vazios por padrão") {
  auto db = freshDb();
  auto p = createProduct(db, makeProduct("p1", "Papel A4"));
  CHECK(p.imagePath.empty());
  CHECK(p.thumbnailPath.empty());
  auto found = findProduct(db, "p1");
  REQUIRE(found.has_value());
  CHECK(found->imagePath.empty());
  CHECK(found->thumbnailPath.empty());
}
