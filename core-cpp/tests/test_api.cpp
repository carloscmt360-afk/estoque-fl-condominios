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
  // Todo método da Api exige sessão (ver api.hpp). Nos testes usa-se a sessão
  // de serviço, o mesmo caminho do core-cli.
  api.loginAsService("teste");

  Product p;
  p.id = "p1";
  p.name = "Papel A4";
  p.unit = "Unidade";
  p.category = "Papelaria";
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
  api2.loginAsService("teste");
  api2.restoreFromJson(backup);
  auto produtos = api2.listProducts();
  REQUIRE(produtos.size() == 1);
  CHECK(produtos[0].qty == doctest::Approx(7.0));      // 10 - 3
  CHECK(produtos[0].avgCost == doctest::Approx(20.0));  // não recalcula, preserva o valor gravado
}

TEST_CASE("restoreFromJson SUBSTITUI os dados atuais, não anexa") {
  Api api(":memory:");
  api.loginAsService("teste");
  Product p1;
  p1.id = "velho";
  p1.name = "Produto antigo";
  p1.unit = "Unidade";
  p1.category = "Papelaria";
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
  api.loginAsService("teste");
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

TEST_CASE("montarPagamentoSos monta a proposta do Dashboard salvo, com PIX de cada um") {
  Api api(":memory:");
  api.loginAsService("teste");

  Condominio cond;
  cond.id = "cond1";
  cond.nome = "Edifício Sol";
  cond.createdAt = "2026-08-01T00:00:00.000Z";
  api.createCondominio(cond);

  json gerente = {{"id", "ger1"}, {"nome", "RICARDO"}, {"chavePix", "ricardo@pix.com"},
                  {"createdAt", "2026-01-01T00:00:00.000Z"}};
  api.createGerente(gerente.dump());

  // Um Gestor e dois Assistentes — pra conferir que "Encarregado" mapeia pra
  // Gestor e que a fatia de "Assistente" se divide em duas partes iguais.
  api.createSuprimento(json({{"id", "sup1"}, {"nome", "CARLOS"}, {"categoria", "gestor"},
                             {"chavePix", "carlos@pix.com"}, {"createdAt", "2026-01-01T00:00:00.000Z"}})
                           .dump());
  api.createSuprimento(json({{"id", "sup2"}, {"nome", "ANA"}, {"categoria", "assistente"},
                             {"chavePix", "ana@pix.com"}, {"createdAt", "2026-01-01T00:00:00.000Z"}})
                           .dump());
  api.createSuprimento(json({{"id", "sup3"}, {"nome", "BIA"}, {"categoria", "assistente"},
                             {"chavePix", "bia@pix.com"}, {"createdAt", "2026-01-01T00:00:00.000Z"}})
                           .dump());

  api.setSosConfig(json({{"deltaChavePix", "delta@pix.com"}, {"deltaTitular", "Delta Ltda"}}).dump());

  json servico = {{"id", "s1"}, {"condominioId", "cond1"}, {"gerenteId", "ger1"}, {"venda", 1000.0},
                  {"porcentagem", 10.0}, {"dataReferencia", "2026-08"}, {"pago", true},
                  {"dataPagamento", "2026-08-10"}, {"createdAt", "2026-08-01T00:00:00.000Z"}};
  api.createServico(servico.dump());

  // arrecadado = 1000 × 10% = 100 (comissão, não venda bruta — ver commissions_engine.cpp)
  json dash = json::parse(api.montarDashboard(json({{"mesReferencia", "2026-08"}}).dump()));
  CHECK(dash["arrecadado"].get<double>() == doctest::Approx(100.0));

  json snap = {{"id", "dash1"}, {"mesReferencia", "2026-08"}, {"dados", dash},
               {"geradoEm", "2026-08-20T00:00:00.000Z"}, {"createdAt", "2026-08-20T00:00:00.000Z"}};
  api.salvarDashboard(snap.dump());

  json prop = json::parse(api.montarPagamentoSos(json({{"mesReferencia", "2026-08"}}).dump()));
  CHECK(prop["fechado"].get<bool>() == false);
  auto& linhas = prop["dados"]["linhas"];

  auto porNome = [&](const std::string& nome) {
    for (auto& l : linhas) if (l["nome"] == nome) return l;
    FAIL("linha nao encontrada: ", nome);
    return linhas[0];
  };
  // gerente: recebe a própria comissão do dashboard, com a Chave PIX do cadastro
  CHECK(porNome("RICARDO")["valor"].get<double>() == doctest::Approx(dash["gerentes"][0]["comissao"].get<double>()));
  CHECK(porNome("RICARDO")["chavePix"] == "ricardo@pix.com");
  CHECK(porNome("RICARDO")["tipo"] == "gerente");
  // suprimentos: Gestor sozinho recebe o valor de "Encarregado" inteiro
  double valorEncarregado = 0;
  for (auto& l : dash["distribuicaoCompras"]) if (l["rotulo"] == "Encarregado") valorEncarregado = l["valor"];
  CHECK(porNome("CARLOS")["valor"].get<double>() == doctest::Approx(valorEncarregado));
  // dois Assistentes dividem a fatia de "Assistente" em partes iguais
  double valorAssistente = 0;
  for (auto& l : dash["distribuicaoCompras"]) if (l["rotulo"] == "Assistente") valorAssistente = l["valor"];
  CHECK(porNome("ANA")["valor"].get<double>() == doctest::Approx(valorAssistente / 2.0));
  CHECK(porNome("BIA")["valor"].get<double>() == doctest::Approx(valorAssistente / 2.0));
  // Delta: entidade única, com o nome/PIX configurados e o total de descontos
  CHECK(porNome("Delta Ltda")["chavePix"] == "delta@pix.com");
  CHECK(porNome("Delta Ltda")["valor"].get<double>() == doctest::Approx(0.0));  // sem Delta Síndicos neste cenário

  // Fecha o pagamento — grava um registro permanente pro mês.
  json fechar = prop;
  fechar["id"] = "pag1";
  fechar["fechado"] = true;
  fechar["geradoEm"] = "2026-08-21T00:00:00.000Z";
  json fechado = json::parse(api.salvarPagamentoSos(fechar.dump()));
  CHECK(fechado["fechado"].get<bool>() == true);
  CHECK(!fechado["fechadoEm"].get<std::string>().empty());

  // Pedir o pagamento do mesmo mês de novo devolve o registro FECHADO, não
  // monta uma proposta nova por cima.
  json reaberto = json::parse(api.montarPagamentoSos(json({{"mesReferencia", "2026-08"}}).dump()));
  CHECK(reaberto["id"] == "pag1");
  CHECK(reaberto["fechado"].get<bool>() == true);

  // listPagamentosSosJson lista o que foi fechado.
  json lista = json::parse(api.listPagamentosSosJson());
  REQUIRE(lista.size() == 1);
  CHECK(lista[0]["mesReferencia"] == "2026-08");

  // Editar depois de fechado REGRAVA o mesmo registro (não duplica).
  json editado = fechado;
  editado["observacoes"] = "PIX do Carlos corrigido depois";
  api.salvarPagamentoSos(editado.dump());
  json listaDepois = json::parse(api.listPagamentosSosJson());
  CHECK(listaDepois.size() == 1);
  CHECK(listaDepois[0]["observacoes"] == "PIX do Carlos corrigido depois");
}
