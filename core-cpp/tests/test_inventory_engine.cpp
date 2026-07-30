#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/inventory_engine.hpp"

using namespace estoque;

namespace {

Database freshDb() { return Database(":memory:"); }

Product makeProduct(const std::string& id, const std::string& name) {
  Product p;
  p.id = id;
  p.name = name;
  p.unit = "Unidade";
  p.minStock = 0;
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
