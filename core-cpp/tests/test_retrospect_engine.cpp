#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/api.hpp"
#include "estoque/inventory_engine.hpp"
#include "estoque/report_engine.hpp"
#include "estoque/retrospect_engine.hpp"
#include <nlohmann/json.hpp>
#include <string>

using namespace estoque;
using json = nlohmann::json;

namespace {

Database freshDb() { return Database(":memory:"); }

Product makeProduct(const std::string& id, const std::string& name) {
  Product p;
  p.id = id;
  p.name = name;
  p.unit = "Unidade";
  p.createdAt = "2026-01-01T00:00:00.000Z";
  return p;
}

Department makeDept(const std::string& id, const std::string& name, double limite = 0) {
  Department d;
  d.id = id;
  d.name = name;
  d.encarregado = "Fulano";
  d.monthlyLimit = limite;
  d.createdAt = "2026-01-01T00:00:00.000Z";
  return d;
}

// Uma saída de R$ (qty × preço) para `deptId` na data indicada.
void saida(Database& db, const std::string& id, const std::string& deptId, double qty,
           const std::string& date) {
  applySaida(db, id, "p1", qty, deptId, date, "", "", date);
}

json linhaDe(const json& linhas, const std::string& name) {
  for (auto& l : linhas)
    if (l["name"].get<std::string>() == name) return l;
  return json(nullptr);
}

}  // namespace

TEST_CASE("canonDeptKey casa as grafias diferentes das duas fontes") {
  CHECK(canonDeptKey("Contabilidade") == "CONTABILIDADE");
  CHECK(canonDeptKey("  DP  ") == "DP");  // o 'DP ' com espaço solto do arquivo real
  CHECK(canonDeptKey("Diretoria   Comercial") == "DIRETORIA COMERCIAL");
  CHECK(canonDeptKey("gerência") == "GERÊNCIA");
  CHECK(canonDeptKey("cobrança") == "COBRANÇA");
  CHECK(canonDeptKey("expedição") == "EXPEDIÇÃO");
  CHECK(canonDeptKey("") == "");
}

TEST_CASE("matriz do ano: histórico importado alimenta os meses e soma o total anual") {
  auto db = freshDb();
  json payload;
  payload["rows"] = json::array({
      json{{"year", 2025}, {"month0", 0}, {"dept", "PASTAS"}, {"amount", 100.0}},
      json{{"year", 2025}, {"month0", 1}, {"dept", "PASTAS"}, {"amount", 200.0}},
      json{{"year", 2025}, {"month0", 1}, {"dept", "DP"}, {"amount", 50.0}},
  });
  CHECK(importDeptCostHistoryJson(db, payload.dump()) == 3);

  RetrospectParams params{2025, "auto", "2026-08-06T12:00:00.000Z"};
  json r = json::parse(computeRetrospectJson(db, params));

  CHECK(r["ref"]["total"].get<double>() == doctest::Approx(350.0));
  CHECK(r["ref"]["totaisMes"][0].get<double>() == doctest::Approx(100.0));
  CHECK(r["ref"]["totaisMes"][1].get<double>() == doctest::Approx(250.0));
  CHECK(r["ref"]["ultimoMes"].get<int>() == 1);
  CHECK(linhaDe(r["ref"]["linhas"], "PASTAS")["total"].get<double>() == doctest::Approx(300.0));
  // meses sem nenhuma linha importada não têm origem — não são "zero de verdade"
  CHECK(r["ref"]["origem"][0].get<std::string>() == "planilha");
  CHECK(r["ref"]["origem"][2].get<std::string>() == "");
}

TEST_CASE("mesclagem é por mês inteiro: planilha onde existe, razão no resto") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "DP"));
  applyEntrada(db, "e1", "p1", 1000, 10.0, "F", "NF", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");

  // razão tem janeiro (R$ 300) e fevereiro (R$ 500)
  saida(db, "s1", "d1", 30, "2026-01-10T00:00:00.000Z");
  saida(db, "s2", "d1", 50, "2026-02-10T00:00:00.000Z");

  // ...mas a planilha só conhece janeiro, e com outro valor
  json payload;
  payload["rows"] = json::array({json{{"year", 2026}, {"month0", 0}, {"dept", "DP"}, {"amount", 111.0}}});
  importDeptCostHistoryJson(db, payload.dump());

  json r = json::parse(computeRetrospectJson(db, RetrospectParams{2026, "auto", "2026-08-06T12:00:00.000Z"}));
  CHECK(r["ref"]["origem"][0].get<std::string>() == "planilha");
  CHECK(r["ref"]["origem"][1].get<std::string>() == "sistema");
  CHECK(r["ref"]["totaisMes"][0].get<double>() == doctest::Approx(111.0));
  CHECK(r["ref"]["totaisMes"][1].get<double>() == doctest::Approx(500.0));
  CHECK(r["ref"]["total"].get<double>() == doctest::Approx(611.0));

  // no modo "ledger" a planilha é ignorada inteira
  json l = json::parse(computeRetrospectJson(db, RetrospectParams{2026, "ledger", "2026-08-06T12:00:00.000Z"}));
  CHECK(l["ref"]["totaisMes"][0].get<double>() == doctest::Approx(300.0));
  CHECK(l["ref"]["total"].get<double>() == doctest::Approx(800.0));
}

TEST_CASE("comparativo usa o MESMO nº de meses nos dois anos") {
  auto db = freshDb();
  json payload;
  payload["rows"] = json::array({
      // ano anterior inteiro: 12 × 100
      json{{"year", 2025}, {"month0", 0}, {"dept", "DP"}, {"amount", 100.0}},
      json{{"year", 2025}, {"month0", 1}, {"dept", "DP"}, {"amount", 100.0}},
      json{{"year", 2025}, {"month0", 5}, {"dept", "DP"}, {"amount", 100.0}},
      // ano de referência só até fevereiro
      json{{"year", 2026}, {"month0", 0}, {"dept", "DP"}, {"amount", 80.0}},
      json{{"year", 2026}, {"month0", 1}, {"dept", "DP"}, {"amount", 80.0}},
  });
  importDeptCostHistoryJson(db, payload.dump());

  json r = json::parse(computeRetrospectJson(db, RetrospectParams{2026, "auto", "2026-08-06T12:00:00.000Z"}));
  CHECK(r["comparativo"]["mesLimite"].get<int>() == 1);
  CHECK(r["comparativo"]["curTotal"].get<double>() == doctest::Approx(160.0));
  // 200, não 300: junho/2025 fica de fora porque 2026 ainda não tem junho
  CHECK(r["comparativo"]["prevTotal"].get<double>() == doctest::Approx(200.0));
}

TEST_CASE("teto de gastos: fator sobre o ano-base, com piso mensal") {
  auto db = freshDb();
  json payload;
  payload["rows"] = json::array({
      json{{"year", 2025}, {"month0", 0}, {"dept", "PASTAS"}, {"amount", 120000.0}},  // bem acima do piso
      json{{"year", 2025}, {"month0", 0}, {"dept", "TI"}, {"amount", 100.0}},         // abaixo do piso
      json{{"year", 2026}, {"month0", 0}, {"dept", "PASTAS"}, {"amount", 10000.0}},
  });
  importDeptCostHistoryJson(db, payload.dump());
  saveBudgetParams(db, BudgetParams{0.10, 0.045, 300.0});  // fator 0,9405

  json r = json::parse(computeRetrospectJson(db, RetrospectParams{2026, "auto", "2026-08-06T12:00:00.000Z"}));
  CHECK(r["params"]["fator"].get<double>() == doctest::Approx(0.9405));

  auto pastas = linhaDe(r["teto"]["linhas"], "PASTAS");
  CHECK(pastas["base"].get<double>() == doctest::Approx(120000.0));
  CHECK(pastas["tetoMensal"].get<double>() == doctest::Approx(120000.0 * 0.9405 / 12));
  CHECK(pastas["noPiso"].get<bool>() == false);

  auto ti = linhaDe(r["teto"]["linhas"], "TI");
  CHECK(ti["tetoMensal"].get<double>() == doctest::Approx(300.0));  // piso venceu o cálculo
  CHECK(ti["noPiso"].get<bool>() == true);

  // 1 mês decorrido em 2026: o realizado é comparado com 1 mês de teto,
  // não com o teto anual inteiro
  CHECK(r["teto"]["mesesDecorridos"].get<int>() == 1);
  CHECK(pastas["pctPeriodo"].get<double>() == doctest::Approx(10000.0 / (120000.0 * 0.9405 / 12)));
  CHECK(pastas["status"].get<std::string>() == "estourado");
}

TEST_CASE("limite mensal do departamento no relatório: ok, 90% e estouro") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "DP", 1000.0));         // gastará 500 -> ok
  createDepartment(db, makeDept("d2", "PASTAS", 1000.0));     // gastará 950 -> atenção
  createDepartment(db, makeDept("d3", "GERÊNCIA", 1000.0));   // gastará 1200 -> estourado
  createDepartment(db, makeDept("d4", "TI"));                 // sem limite
  applyEntrada(db, "e1", "p1", 10000, 1.0, "F", "NF", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");

  saida(db, "s1", "d1", 500, "2026-03-05T00:00:00.000Z");
  saida(db, "s2", "d2", 950, "2026-03-05T00:00:00.000Z");
  saida(db, "s3", "d3", 1200, "2026-03-05T00:00:00.000Z");
  saida(db, "s4", "d4", 77, "2026-03-05T00:00:00.000Z");

  json r = json::parse(computeReportJson(db, ReportParams{2026, 2, "", 6, "2026-04-01T00:00:00.000Z"}));
  auto& lim = r["limites"];
  CHECK(lim["comLimite"].get<int>() == 3);
  CHECK(lim["atencao"].get<int>() == 1);
  CHECK(lim["estourado"].get<int>() == 1);
  CHECK(linhaDe(lim["linhas"], "DP")["status"].get<std::string>() == "ok");
  CHECK(linhaDe(lim["linhas"], "PASTAS")["status"].get<std::string>() == "atencao");
  CHECK(linhaDe(lim["linhas"], "GERÊNCIA")["status"].get<std::string>() == "estourado");
  CHECK(linhaDe(lim["linhas"], "GERÊNCIA")["saldo"].get<double>() == doctest::Approx(-200.0));
  CHECK(linhaDe(lim["linhas"], "TI")["status"].get<std::string>() == "sem-limite");
  CHECK(linhaDe(lim["linhas"], "TI")["pct"].is_null());
}

TEST_CASE("filtrar o relatório por um departamento não esconde o estouro dos outros") {
  auto db = freshDb();
  createProduct(db, makeProduct("p1", "Papel A4"));
  createDepartment(db, makeDept("d1", "DP", 1000.0));
  createDepartment(db, makeDept("d2", "PASTAS", 100.0));
  applyEntrada(db, "e1", "p1", 10000, 1.0, "F", "NF", "2026-01-01T00:00:00.000Z", "", "2026-01-01T00:00:00.000Z");
  saida(db, "s1", "d1", 200, "2026-03-05T00:00:00.000Z");
  saida(db, "s2", "d2", 500, "2026-03-05T00:00:00.000Z");

  json r = json::parse(computeReportJson(db, ReportParams{2026, 2, "DP", 6, "2026-04-01T00:00:00.000Z"}));
  CHECK(r["kpi"]["consumo"].get<double>() == doctest::Approx(200.0));  // o relatório está filtrado...
  CHECK(r["limites"]["estourado"].get<int>() == 1);                    // ...mas o alerta não
  CHECK(linhaDe(r["limites"]["linhas"], "PASTAS")["gasto"].get<double>() == doctest::Approx(500.0));
}

TEST_CASE("limite mensal sobrevive a backup/restore, junto com o histórico") {
  json payload;
  payload["rows"] = json::array({json{{"year", 2025}, {"month0", 3}, {"dept", "DP"}, {"amount", 99.0}}});

  Api origem(":memory:");
  origem.createDepartment(makeDept("d1", "DP", 1234.5));
  origem.importDeptCostHistory(payload.dump());
  origem.saveBudgetParams(0.2, 0.03, 500.0);

  Api destino(":memory:");
  destino.restoreFromJson(origem.backupJson());
  CHECK(destino.listDepartments().at(0).monthlyLimit == doctest::Approx(1234.5));
  json r = json::parse(destino.computeRetrospectJson(2025, "auto", "2026-08-06T12:00:00.000Z"));
  CHECK(r["ref"]["total"].get<double>() == doctest::Approx(99.0));
  CHECK(r["params"]["pisoMensal"].get<double>() == doctest::Approx(500.0));
  CHECK(r["params"]["metaReducao"].get<double>() == doctest::Approx(0.2));
}

TEST_CASE("backup antigo (sem histórico) não apaga o que já estava no banco") {
  Api api(":memory:");
  json payload;
  payload["rows"] = json::array({json{{"year", 2025}, {"month0", 0}, {"dept", "DP"}, {"amount", 42.0}}});
  api.importDeptCostHistory(payload.dump());

  json antigo;
  antigo["products"] = json::array();
  antigo["movements"] = json::array();
  antigo["departments"] = json::array();
  api.restoreFromJson(antigo.dump());

  json r = json::parse(api.computeRetrospectJson(2025, "auto", "2026-08-06T12:00:00.000Z"));
  CHECK(r["ref"]["total"].get<double>() == doctest::Approx(42.0));
}

TEST_CASE("reimportar um ano substitui o ano inteiro (departamento removido não vira fantasma)") {
  auto db = freshDb();
  json v1;
  v1["rows"] = json::array({
      json{{"year", 2025}, {"month0", 0}, {"dept", "DP"}, {"amount", 10.0}},
      json{{"year", 2025}, {"month0", 0}, {"dept", "TI"}, {"amount", 20.0}},
  });
  importDeptCostHistoryJson(db, v1.dump());

  json v2;
  v2["rows"] = json::array({json{{"year", 2025}, {"month0", 0}, {"dept", "DP"}, {"amount", 10.0}}});
  importDeptCostHistoryJson(db, v2.dump());

  json r = json::parse(computeRetrospectJson(db, RetrospectParams{2025, "auto", "2026-08-06T12:00:00.000Z"}));
  CHECK(r["ref"]["total"].get<double>() == doctest::Approx(10.0));
  CHECK(linhaDe(r["ref"]["linhas"], "TI").is_null());
}

TEST_CASE("linhas repetidas do mesmo mês/departamento somam (detalhe da planilha, não sobrescrevem)") {
  auto db = freshDb();
  json payload;
  payload["rows"] = json::array({
      json{{"year", 2026}, {"month0", 4}, {"dept", "DP"}, {"amount", 442.29}},
      json{{"year", 2026}, {"month0", 4}, {"dept", "DP "}, {"amount", 136.83}},  // grafia solta
  });
  importDeptCostHistoryJson(db, payload.dump());

  json r = json::parse(computeRetrospectJson(db, RetrospectParams{2026, "auto", "2026-08-06T12:00:00.000Z"}));
  CHECK(r["ref"]["totaisMes"][4].get<double>() == doctest::Approx(579.12));
}
