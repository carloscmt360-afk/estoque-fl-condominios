#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/inventory_engine.hpp"
#include "estoque/report_engine.hpp"
#include <nlohmann/json.hpp>
#include <map>

using namespace estoque;
using json = nlohmann::json;

namespace {

Database freshDb() { return Database(":memory:"); }

Product makeProduct(const std::string& id, const std::string& name, double minStock = 0) {
  Product p;
  p.id = id;
  p.name = name;
  p.unit = "Unidade";
  p.minStock = minStock;
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

TEST_CASE("mês futuro: comparações viram null (nunca um número inventado por falta de dado)") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");

  // "hoje" é janeiro/2026; pedimos o relatório de dezembro/2026 — ainda não aconteceu
  ReportParams params{2026, 11, "", 6, "2026-01-15T00:00:00.000Z"};
  json r = json::parse(computeReportJson(db, params));

  CHECK(r["refFuturo"].get<bool>() == true);
  CHECK(r["kpi"]["consumoAnt"].is_null());
  CHECK(r["kpi"]["consumoAnoAnt"].is_null());
  CHECK(r["kpi"]["comprasAnt"].is_null());
  CHECK(r["kpi"]["valorEstoqueAnt"].is_null());

  // dezembro no array "anual" também deve vir com ref=null e futuro=true
  auto dez = r["anual"][11];
  CHECK(dez["futuro"].get<bool>() == true);
  CHECK(dez["ref"].is_null());
}

TEST_CASE("mês passado fechado: comparações são números reais, não null") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");
  createDepartment(db, makeDept("d1", "TI"));
  applySaida(db, "m2", "p1", 2, "d1", "2026-03-05T00:00:00.000Z", "", "Fulana", "2026-03-05T00:00:00.000Z");
  applySaida(db, "m3", "p1", 3, "d1", "2026-04-05T00:00:00.000Z", "", "Fulana", "2026-04-05T00:00:00.000Z");

  // "hoje" é julho/2026; pedimos abril/2026 (mês fechado, já decorrido)
  ReportParams params{2026, 3, "", 6, "2026-07-30T00:00:00.000Z"};
  json r = json::parse(computeReportJson(db, params));

  CHECK(r["refFuturo"].get<bool>() == false);
  CHECK(r["refEmCurso"].get<bool>() == false);
  REQUIRE(!r["kpi"]["consumoAnt"].is_null());
  CHECK(r["kpi"]["consumoAnt"].get<double>() == doctest::Approx(2 * 20.0));  // março: 2 unid a R$20
  CHECK(r["kpi"]["consumo"].get<double>() == doctest::Approx(3 * 20.0));    // abril: 3 unid a R$20
}

TEST_CASE("mês em curso: refEmCurso=true e diasDecorridos reflete o dia de 'agora'") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));

  // "hoje" é 15 de julho/2026; pedimos o relatório do próprio julho/2026 (em curso)
  ReportParams params{2026, 6, "", 6, "2026-07-15T09:00:00.000Z"};
  json r = json::parse(computeReportJson(db, params));

  CHECK(r["refFuturo"].get<bool>() == false);
  CHECK(r["refEmCurso"].get<bool>() == true);
  CHECK(r["diasDecorridos"].get<int>() == 15);
  CHECK(r["diasNoMes"].get<int>() == 31);
}

TEST_CASE("YTD é sempre limitado ao último mês decorrido nos dois anos") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "TI"));
  applyEntrada(db, "m0", "p1", 1000, 1.0, "F", "NF", "2025-01-01T00:00:00.000Z", "", "2025-01-01T00:00:00.000Z");
  // saída em setembro/2025 (mês 8, 0-indexado) — não deve entrar no YTD se mYTD < 8
  applySaida(db, "s25", "p1", 5, "d1", "2025-09-10T00:00:00.000Z", "", "Fulana", "2025-09-10T00:00:00.000Z");
  // saída em março/2026 (mês 2) — deve entrar no YTD de 2026
  applySaida(db, "s26", "p1", 3, "d1", "2026-03-10T00:00:00.000Z", "", "Fulana", "2026-03-10T00:00:00.000Z");

  // "hoje" é 15 de junho/2026 -> mYTD para o ano de referência 2026 = min(mesRef, 5)
  ReportParams params{2026, 5, "", 6, "2026-06-15T00:00:00.000Z"};  // pede junho/2026 (mês 5)
  json r = json::parse(computeReportJson(db, params));

  CHECK(r["mYTD"].get<int>() == 5);  // junho (mês 5) já decorreu totalmente até "hoje" 15/jun
  double ytdCur = r["ytdCur"].get<double>();
  double ytdPrev = r["ytdPrev"].get<double>();
  CHECK(ytdCur == doctest::Approx(3.0));   // só a saída de março/2026 (mês 2 <= mYTD 5)
  CHECK(ytdPrev == doctest::Approx(0.0));  // a saída de set/2025 (mês 8) fica FORA do corte (8 > mYTD 5)
}

TEST_CASE("YTD de um ano totalmente no passado usa o mês inteiro pedido, não o mês corrente") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "TI"));
  applyEntrada(db, "m0", "p1", 1000, 1.0, "F", "NF", "2023-01-01T00:00:00.000Z", "", "2023-01-01T00:00:00.000Z");
  applySaida(db, "s23", "p1", 7, "d1", "2023-11-10T00:00:00.000Z", "", "Fulana", "2023-11-10T00:00:00.000Z");

  // "hoje" é 2026; pedimos novembro/2023 (ano totalmente decorrido)
  ReportParams params{2023, 10, "", 6, "2026-07-30T00:00:00.000Z"};
  json r = json::parse(computeReportJson(db, params));

  CHECK(r["mYTD"].get<int>() == 10);  // novembro (mês 10) — o ano já acabou, usa o mês pedido inteiro
  CHECK(r["ytdCur"].get<double>() == doctest::Approx(7.0));
}

TEST_CASE("curva ABC classifica pelo valor acumulado em ordem decrescente (A<=80%, B<=95%, C=resto)") {
  auto db = freshDb();
  // valores desenhados para cair em pontos limpos do corte: 500,300,100,70,30 (total 1000)
  // cumulativo: 500(50%)->A, 800(80%)->A, 900(90%)->B, 970(97%)->C, 1000(100%)->C
  createProduct(db, makeProduct("p1", "Item 500"));
  createProduct(db, makeProduct("p2", "Item 300"));
  createProduct(db, makeProduct("p3", "Item 100"));
  createProduct(db, makeProduct("p4", "Item 70"));
  createProduct(db, makeProduct("p5", "Item 30"));
  applyEntrada(db, "m1", "p1", 1, 500.0, "F", "NF1", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");
  applyEntrada(db, "m2", "p2", 1, 300.0, "F", "NF2", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");
  applyEntrada(db, "m3", "p3", 1, 100.0, "F", "NF3", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");
  applyEntrada(db, "m4", "p4", 1, 70.0, "F", "NF4", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");
  applyEntrada(db, "m5", "p5", 1, 30.0, "F", "NF5", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");

  ReportParams params{2026, 6, "", 6, "2026-07-30T00:00:00.000Z"};
  json r = json::parse(computeReportJson(db, params));

  std::map<std::string, std::string> classePorId;
  for (auto& it : r["itens"]) classePorId[it["id"].get<std::string>()] = it["classe"].get<std::string>();

  CHECK(classePorId["p1"] == "A");
  CHECK(classePorId["p2"] == "A");  // exatamente 80% acumulado, <=0.80 inclui a fronteira
  CHECK(classePorId["p3"] == "B");
  CHECK(classePorId["p4"] == "C");
  CHECK(classePorId["p5"] == "C");
  CHECK(r["valorTotal"].get<double>() == doctest::Approx(1000.0));
  CHECK(r["abc"]["A"]["v"].get<double>() == doctest::Approx(800.0));
  CHECK(r["abc"]["A"]["n"].get<int>() == 2);
}

TEST_CASE("reconciliação: baixa a custo médio divergente do preço da requisição é sinalizada") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "TI"));
  applyEntrada(db, "m1", "p1", 10, 20.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  // saída live (via applySaida) sempre valoriza ao custo médio -> nunca gera divergência sozinha.
  // Simula uma importação histórica onde o preço da requisição diverge do custo médio recalculado.
  db.prepare(
      "INSERT INTO movements (id, type, product_id, qty, unit_price, department_id, recipient, date, "
      "resulting_qty, resulting_avg_cost, created_at) VALUES (?, 'saida', ?, ?, ?, ?, ?, ?, ?, ?, ?)")
      .bind(1, std::string("m2")).bind(2, std::string("p1")).bind(3, 2.0).bind(4, 5.0)
      .bind(5, std::string("d1")).bind(6, std::string("TI")).bind(7, std::string("2026-06-10T00:00:00.000Z"))
      .bind(8, 8.0).bind(9, 20.0).bind(10, std::string("2026-06-10T00:00:00.000Z"))
      .step();

  ReportParams params{2026, 5, "", 6, "2026-07-30T00:00:00.000Z"};
  json r = json::parse(computeReportJson(db, params));

  CHECK(r["reconc"]["linhas"].get<int>() == 1);
  // diferença = qty * (custo_medio - preco_requisicao) = 2 * (20 - 5) = 30
  CHECK(r["reconc"]["dif"].get<double>() == doctest::Approx(30.0));
}

TEST_CASE("filtro por departamento restringe consumo/pedidos mas não a posição geral de estoque") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "TI"));
  createDepartment(db, makeDept("d2", "RH"));
  applyEntrada(db, "m1", "p1", 20, 10.0, "F", "NF", "2026-06-01T00:00:00.000Z", "", "2026-06-01T00:00:00.000Z");
  applySaida(db, "s1", "p1", 5, "d1", "2026-06-05T00:00:00.000Z", "", "Fulana", "2026-06-05T00:00:00.000Z");
  applySaida(db, "s2", "p1", 3, "d2", "2026-06-06T00:00:00.000Z", "", "Maria", "2026-06-06T00:00:00.000Z");

  ReportParams params{2026, 5, "TI", 6, "2026-07-30T00:00:00.000Z"};
  json r = json::parse(computeReportJson(db, params));

  CHECK(r["kpi"]["consumo"].get<double>() == doctest::Approx(5 * 10.0));  // só a saída de TI
  CHECK(r["kpi"]["pedidos"].get<int>() == 1);
  // a posição de estoque (valorTotal) reflete AMBAS as saídas, não é segregada por departamento
  CHECK(r["valorTotal"].get<double>() == doctest::Approx((20 - 5 - 3) * 10.0));
}
