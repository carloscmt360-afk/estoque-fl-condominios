#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <fstream>
#include <sstream>

#include "doctest.h"
#include "estoque/api.hpp"
#include <nlohmann/json.hpp>

using namespace estoque;
using json = nlohmann::json;

namespace {

std::string readFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("não encontrou o arquivo: " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

TEST_CASE("backupJson / restoreFromJson fazem round-trip preservando os dados") {
  Api api(":memory:");

  Product p;
  p.id = "p1";
  p.name = "Papel A4";
  p.unit = "Unidade";
  p.category = "Escritório";
  p.createdAt = "2026-01-01T00:00:00.000Z";
  api.createProduct(p);
  api.applyEntrada("m1", "p1", 10, 20.0, "Fornecedor X", "NF1", "2026-06-01T00:00:00.000Z", "",
                    "2026-06-01T00:00:00.000Z");

  Department d;
  d.id = "d1";
  d.name = "TI";
  d.encarregado = "Fulano";
  d.createdAt = "2026-01-01T00:00:00.000Z";
  api.createDepartment(d);
  api.applySaida("m2", "p1", 3, "d1", "2026-06-05T00:00:00.000Z", "uso teste", "Fulana",
                 "2026-06-05T00:00:00.000Z");

  std::string backup = api.backupJson();
  json parsed = json::parse(backup);
  CHECK(parsed["products"].size() == 1);
  CHECK(parsed["movements"].size() == 2);
  CHECK(parsed["departments"].size() == 1);
  CHECK(parsed["products"][0]["avgCost"].get<double>() == doctest::Approx(20.0));

  // restaura numa base NOVA (zerada) e confere que reproduz fielmente
  Api api2(":memory:");
  api2.restoreFromJson(backup);
  auto produtos = api2.listProducts();
  REQUIRE(produtos.size() == 1);
  CHECK(produtos[0].qty == doctest::Approx(7.0));      // 10 - 3
  CHECK(produtos[0].avgCost == doctest::Approx(20.0));  // não recalcula, preserva o valor gravado
}

TEST_CASE("restoreFromJson SUBSTITUI os dados atuais, não anexa") {
  Api api(":memory:");
  Product p1;
  p1.id = "velho";
  p1.name = "Produto antigo";
  p1.unit = "Unidade";
  p1.createdAt = "2026-01-01T00:00:00.000Z";
  api.createProduct(p1);

  json backup;
  backup["products"] = json::array({{{"id", "novo"}, {"name", "Produto novo"}, {"unit", "Unidade"},
                                      {"minStock", 0}, {"category", ""}, {"qty", 5}, {"avgCost", 1.5},
                                      {"createdAt", "2026-01-01T00:00:00.000Z"}}});
  backup["movements"] = json::array();
  backup["departments"] = json::array();

  api.restoreFromJson(backup.dump());
  auto produtos = api.listProducts();
  REQUIRE(produtos.size() == 1);
  CHECK(produtos[0].id == "novo");  // o produto "velho" não sobrou
}

TEST_CASE("importa o backup REAL do usuário e reproduz os números do relatório oficial") {
  // mesmo arquivo já conferido manualmente contra a planilha original:
  // R$ 56.664,37 em estoque e R$ 10.790,36 de consumo em junho/2026.
  std::string path = "/home/cruz/Área de trabalho/Projetos/Carlos FL/carga_inicial_ref_jun26.json";
  std::string backup = readFile(path);

  Api api(":memory:");
  api.restoreFromJson(backup);

  std::string reportStr = api.computeReportJson(2026, 5 /*junho*/, "", 6, "2026-07-30T12:00:00.000Z");
  json r = json::parse(reportStr);

  CHECK(r["valorTotal"].get<double>() == doctest::Approx(56664.37).epsilon(0.001));
  CHECK(r["kpi"]["consumo"].get<double>() == doctest::Approx(10790.36).epsilon(0.001));
  CHECK(r["kpi"]["pedidos"].get<int>() == 67);

  double totalDeptMes = 0;
  for (auto& [k, v] : r["deptMes"].items()) totalDeptMes += v.get<double>();
  CHECK(totalDeptMes == doctest::Approx(10790.36).epsilon(0.001));

  // conferência item a item contra o RESTROSPECTO/PEDIDOS da planilha original
  CHECK(r["deptMes"]["PASTAS"].get<double>() == doctest::Approx(7900.66).epsilon(0.001));
  CHECK(r["deptMes"]["GERÊNCIA"].get<double>() == doctest::Approx(1172.63).epsilon(0.001));
  CHECK(r["deptMes"]["DP"].get<double>() == doctest::Approx(504.47).epsilon(0.001));
}
