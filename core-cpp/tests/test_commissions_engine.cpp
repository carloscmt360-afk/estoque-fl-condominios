#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/commissions_engine.hpp"

#include <algorithm>

#include "estoque/companies_engine.hpp"
#include "estoque/dates_engine.hpp"
#include "estoque/managers_engine.hpp"

using namespace estoque;

namespace {

constexpr const char* kNow = "2026-08-16T12:00:00.000Z";

struct Cenario {
  Database db{":memory:"};

  Cenario() {
    Condominio c;
    c.id = "cond1";
    c.nome = "Edifício Central";
    c.createdAt = kNow;
    createCondominio(db, c);

    Gerente g;
    g.id = "ger1";
    g.nome = "Fulano";
    g.createdAt = kNow;
    createGerente(db, g);

    Empresa parceira;
    parceira.id = "par1";
    parceira.nome = "BioPrag";
    parceira.parceira = true;
    parceira.createdAt = kNow;
    createEmpresa(db, parceira);

    Empresa naoParceira;
    naoParceira.id = "np1";
    naoParceira.nome = "Fornecedor Comum";
    naoParceira.parceira = false;
    naoParceira.createdAt = kNow;
    createEmpresa(db, naoParceira);
  }

  // pago=true por padrão: a maioria dos testes quer um serviço que já entra
  // em fecharMes/montarDashboard (mesmo critério de "só pago conta"). Um
  // teste que precisa de um serviço em aberto sobrescreve pago=false depois.
  Servico servico(const std::string& id, double venda, double porcentagem,
                  const std::string& mes = "2026-08") {
    Servico s;
    s.id = id;
    s.condominioId = "cond1";
    s.venda = venda;
    s.porcentagem = porcentagem;
    s.dataReferencia = mes;
    s.createdAt = kNow;
    s.pago = true;
    s.dataPagamento = mes + "-01";
    return s;
  }
};

}  // namespace

TEST_CASE("comissaoDe é sempre venda * porcentagem / 100") {
  Servico s;
  s.venda = 1000;
  s.porcentagem = 12.5;
  CHECK(comissaoDe(s) == doctest::Approx(125.0));
}

TEST_CASE("createServico: resolve nomes por id e recusa condomínio/gerente/parceiro inexistente") {
  Cenario c;
  auto s = c.servico("s1", 1000, 10);
  s.gerenteId = "ger1";
  s.parceiroId = "par1";
  auto criado = createServico(c.db, s);
  CHECK(criado.condominioNome == "Edifício Central");
  CHECK(criado.gerenteNome == "Fulano");
  CHECK(criado.parceiroNome == "BioPrag");
  CHECK(criado.numero > 0);
  CHECK(criado.fechamentoId.empty());

  auto semCondominio = c.servico("x", 100, 10);
  semCondominio.condominioId = "fantasma";
  CHECK_THROWS_AS(createServico(c.db, semCondominio), NotFoundError);

  auto gerenteRuim = c.servico("x", 100, 10);
  gerenteRuim.gerenteId = "fantasma";
  CHECK_THROWS_AS(createServico(c.db, gerenteRuim), NotFoundError);

  // Empresa existe, mas não é parceira: recusado mesmo assim.
  auto parceiroRuim = c.servico("x", 100, 10);
  parceiroRuim.parceiroId = "np1";
  CHECK_THROWS_AS(createServico(c.db, parceiroRuim), std::invalid_argument);
}

TEST_CASE("createServico: recusa venda negativa, porcentagem fora de 0-100 e mês inválido") {
  Cenario c;
  CHECK_THROWS_AS(createServico(c.db, c.servico("x", -1, 10)), std::invalid_argument);
  CHECK_THROWS_AS(createServico(c.db, c.servico("x", 100, -1)), std::invalid_argument);
  CHECK_THROWS_AS(createServico(c.db, c.servico("x", 100, 101)), std::invalid_argument);
  CHECK_THROWS_AS(createServico(c.db, c.servico("x", 100, 10, "2026-13")), std::invalid_argument);
  CHECK_THROWS_AS(createServico(c.db, c.servico("x", 100, 10, "agosto/2026")), std::invalid_argument);
}

TEST_CASE("numero é sequencial e nunca se repete, mesmo depois de excluir") {
  Cenario c;
  auto s1 = createServico(c.db, c.servico("s1", 100, 10));
  auto s2 = createServico(c.db, c.servico("s2", 100, 10));
  CHECK(s2.numero == s1.numero + 1);

  deleteServico(c.db, s2.id);
  auto s3 = createServico(c.db, c.servico("s3", 100, 10));
  CHECK(s3.numero > s2.numero);  // não reaproveita o número do excluído
}

TEST_CASE("fecharMes: trava os serviços em aberto do mês e grava os totais") {
  Cenario c;
  createServico(c.db, c.servico("s1", 1000, 10, "2026-08"));  // comissão 100
  createServico(c.db, c.servico("s2", 2000, 5, "2026-08"));   // comissão 100
  createServico(c.db, c.servico("s3", 500, 10, "2026-09"));   // mês diferente, não entra

  auto f = fecharMes(c.db, "fec1", "2026-08", "", kNow, kNow);
  CHECK(f.quantidadeServicos == 2);
  CHECK(f.totalVenda == doctest::Approx(3000.0));
  CHECK(f.totalComissao == doctest::Approx(200.0));

  auto s1 = *findServico(c.db, "s1");
  CHECK(s1.fechamentoId == "fec1");
  auto s3 = *findServico(c.db, "s3");
  CHECK(s3.fechamentoId.empty());  // mês de setembro continua aberto

  CHECK_THROWS_AS(updateServico(c.db, s1), std::invalid_argument);
  CHECK_THROWS_AS(deleteServico(c.db, "s1"), std::invalid_argument);
}

TEST_CASE("fecharMes: recusa só quando não há nenhum serviço em aberto no mês") {
  Cenario c;
  CHECK_THROWS_AS(fecharMes(c.db, "fec1", "2026-08", "", kNow, kNow), std::invalid_argument);

  createServico(c.db, c.servico("s1", 100, 10, "2026-08"));
  CHECK_NOTHROW(fecharMes(c.db, "fec1", "2026-08", "", kNow, kNow));
  // Sem nenhum serviço em aberto sobrando: fechar de novo o mesmo mês agora
  // recusa por falta de serviço em aberto, não porque "já foi fechado" —
  // esse motivo de recusa não existe mais (ver reabrirFechamento).
  CHECK_THROWS_AS(fecharMes(c.db, "fec2", "2026-08", "", kNow, kNow), std::invalid_argument);
}

TEST_CASE("reabrirFechamento: solta os serviços, mas o registro do fechamento nunca desaparece") {
  Cenario c;
  createServico(c.db, c.servico("s1", 1000, 10, "2026-08"));
  fecharMes(c.db, "fec1", "2026-08", "", kNow, kNow);

  reabrirFechamento(c.db, "fec1", kNow);
  auto s1 = *findServico(c.db, "s1");
  CHECK(s1.fechamentoId.empty());

  auto historico = listFechamentos(c.db);
  REQUIRE(historico.size() == 1);  // continua no histórico — só marcado como reaberto
  CHECK(historico[0].id == "fec1");
  CHECK(historico[0].reabertoEm == kNow);
  CHECK(historico[0].totalVenda == doctest::Approx(1000.0));  // total congelado, não recalculado

  // Volta a poder editar normalmente.
  s1.venda = 1200;
  CHECK_NOTHROW(updateServico(c.db, s1));

  CHECK_THROWS_AS(reabrirFechamento(c.db, "fantasma", kNow), NotFoundError);
}

TEST_CASE("o mesmo mês pode ser fechado de novo depois de reaberto — cada fechamento é uma entrada permanente") {
  Cenario c;
  createServico(c.db, c.servico("s1", 1000, 10, "2026-08"));
  createServico(c.db, c.servico("s2", 500, 10, "2026-08"));
  auto f1 = fecharMes(c.db, "fec1", "2026-08", "", kNow, kNow);
  CHECK(f1.quantidadeServicos == 2);

  reabrirFechamento(c.db, "fec1", kNow);
  auto s1 = *findServico(c.db, "s1");
  s1.venda = 1100;  // corrige um valor errado
  updateServico(c.db, s1);

  auto f2 = fecharMes(c.db, "fec2", "2026-08", "correção", kNow, kNow);
  CHECK(f2.quantidadeServicos == 2);  // os dois voltaram a ficar em aberto ao reabrir
  CHECK(f2.totalVenda == doctest::Approx(1600.0));

  auto historico = listFechamentos(c.db);
  REQUIRE(historico.size() == 2);  // fec1 (reaberto) E fec2 — nenhum some
  CHECK(std::any_of(historico.begin(), historico.end(), [](const Fechamento& f) { return f.id == "fec1"; }));
  CHECK(std::any_of(historico.begin(), historico.end(), [](const Fechamento& f) { return f.id == "fec2"; }));
}

TEST_CASE("editar serviço em aberto preserva o nome quando o cadastro de origem foi excluído") {
  Cenario c;
  Gerente g2;
  g2.id = "ger2";
  g2.nome = "Ciclano";
  g2.createdAt = kNow;
  createGerente(c.db, g2);

  auto s = c.servico("s1", 1000, 10);
  s.gerenteId = "ger2";
  auto criado = createServico(c.db, s);
  CHECK(criado.gerenteNome == "Ciclano");

  deleteGerente(c.db, "ger2");  // "gerente saiu da empresa"

  // Editar outro campo (venda) SEM trocar o gerenteId não trava, e preserva
  // o nome já gravado em vez de exigir que o gerente ainda exista.
  criado.venda = 1234;
  auto editado = updateServico(c.db, criado);
  CHECK(editado.venda == doctest::Approx(1234.0));
  CHECK(editado.gerenteNome == "Ciclano");

  // Só uma troca EXPLÍCITA para outro id inexistente continua sendo recusada.
  editado.gerenteId = "fantasma";
  CHECK_THROWS_AS(updateServico(c.db, editado), NotFoundError);
}

TEST_CASE("configurações: valor default e round-trip") {
  Cenario c;
  CHECK(getConfig(c.db, sos_config::kPorcentagemPadrao, "0") == "0");
  setConfig(c.db, sos_config::kPorcentagemPadrao, "8.5");
  CHECK(getConfig(c.db, sos_config::kPorcentagemPadrao, "0") == "8.5");
  setConfig(c.db, sos_config::kPorcentagemPadrao, "12");
  CHECK(getConfig(c.db, sos_config::kPorcentagemPadrao, "0") == "12");
}

// ------------------------------------------------------- Delta Síndicos
//
// Não é mais lançado à mão: listDeltaSindicos é inteiramente PUXADO de
// listServicos + Condominio::deltaSindica. Os testes abaixo substituem os
// antigos de create/update/delete (que não existem mais).

TEST_CASE("comissaoDeltaDe é sempre venda * porcentagem / 100") {
  DeltaSindico d;
  d.venda = 2000;
  d.porcentagem = 7.5;
  CHECK(comissaoDeltaDe(d) == doctest::Approx(150.0));
}

TEST_CASE("listDeltaSindicos: só entram serviços pagos de condomínio marcado deltaSindica") {
  Cenario c;
  // cond2, com síndico cadastrado, marcado como atendido pela Delta —
  // cond1 (do cenário) não é.
  Condominio cond2;
  cond2.id = "cond2";
  cond2.nome = "Residencial Alfa";
  cond2.sindico = "Maria Souza";
  cond2.deltaSindica = true;
  cond2.createdAt = kNow;
  createCondominio(c.db, cond2);

  setConfig(c.db, sos_config::kDeltaSindica, "10");

  // Serviço pago em cond1 (NÃO é Delta): não aparece na planilha.
  createServico(c.db, c.servico("fora", 1000, 10));

  // Serviço pago em cond2 (Delta): aparece, com o "numero" do próprio
  // serviço de origem (não existe id/numero próprio).
  auto s1 = c.servico("s1", 2000, 5);
  s1.condominioId = "cond2";
  auto criado1 = createServico(c.db, s1);

  // Serviço NÃO pago em cond2: não aparece — mesmo critério de
  // montarDashboard ("só pago conta").
  auto naoPago = c.servico("np", 500, 5);
  naoPago.condominioId = "cond2";
  naoPago.pago = false;
  naoPago.dataPagamento = "";
  createServico(c.db, naoPago);

  auto lista = listDeltaSindicos(c.db);
  REQUIRE(lista.size() == 1);
  CHECK(lista[0].numero == criado1.numero);
  CHECK(lista[0].condominioNome == "Residencial Alfa");
  CHECK(lista[0].sindico == "Maria Souza");              // resolvido do cadastro do condomínio
  CHECK(lista[0].venda == doctest::Approx(2000.0));
  CHECK(lista[0].porcentagem == doctest::Approx(10.0));  // sempre sos_config::kDeltaSindica
  CHECK(comissaoDeltaDe(lista[0]) == doctest::Approx(200.0));
}

TEST_CASE("listDeltaSindicos: síndico e a marcação são lidos AGORA, nunca congelados") {
  Cenario c;
  Condominio cond2;
  cond2.id = "cond2";
  cond2.nome = "Residencial Alfa";
  cond2.sindico = "Maria Souza";
  cond2.deltaSindica = true;
  cond2.createdAt = kNow;
  createCondominio(c.db, cond2);

  auto s = c.servico("s1", 1000, 10);
  s.condominioId = "cond2";
  createServico(c.db, s);

  REQUIRE(listDeltaSindicos(c.db).size() == 1);
  CHECK(listDeltaSindicos(c.db)[0].sindico == "Maria Souza");

  // Trocar o síndico do cadastro muda a planilha na hora — não há
  // "histórico congelado" (não existe fechamento próprio aqui).
  cond2.sindico = "Novo Síndico";
  updateCondominio(c.db, cond2);
  CHECK(listDeltaSindicos(c.db)[0].sindico == "Novo Síndico");

  // Desmarcar deltaSindica tira o condomínio da planilha na hora, mesmo com
  // o serviço pago continuando lançado normalmente em Serviços.
  cond2.deltaSindica = false;
  updateCondominio(c.db, cond2);
  CHECK(listDeltaSindicos(c.db).empty());
}

// ------------------------------------------- Dashboard de fechamento

TEST_CASE("dashboard: reproduz as fórmulas SOMASES da planilha real") {
  Cenario c;

  // Um segundo condomínio, só pra ter DOIS condomínios distintos e provar
  // que Recebido segue a CARTEIRA (condomínio), não o gerenteId gravado no
  // serviço — os dois divergem de propósito neste cenário.
  Condominio cond2;
  cond2.id = "cond2";
  cond2.nome = "Edifício Norte";
  cond2.createdAt = kNow;
  createCondominio(c.db, cond2);

  // Um terceiro, marcado como atendido pela Delta — o serviço pago aqui
  // também vira uma linha em Delta Síndicos, cuja comissão é o DESCONTO do
  // gerente (era o SOMASES da planilha).
  Condominio cond3;
  cond3.id = "cond3";
  cond3.nome = "Edifício Delta";
  cond3.deltaSindica = true;
  cond3.createdAt = kNow;
  createCondominio(c.db, cond3);
  setConfig(c.db, sos_config::kDeltaSindica, "50");  // PAGAR = 50% do VALOR, como na planilha

  // Dois gerentes com carteira, para conferir carteira/recebido/descontos.
  Gerente thamiris;
  thamiris.id = "gThamiris";
  thamiris.nome = "THAMIRIS";
  thamiris.createdAt = kNow;
  thamiris.condominioIds = {"cond2", "cond3"};
  createGerente(c.db, thamiris);

  Gerente ricardo;
  ricardo.id = "gRicardo";
  ricardo.nome = "RICARDO";
  ricardo.createdAt = kNow;
  ricardo.condominioIds = {"cond1"};
  createGerente(c.db, ricardo);

  // Serviços do mês: RICARDO recebe 2.362,20 do parceiro (é o "arrecadado").
  // gerenteId aqui é só quem LANÇOU/executou — Recebido ignora este campo
  // e olha o condomínio do serviço contra a carteira de cada gerente.
  auto s1 = c.servico("s1", 2362.20, 10);
  s1.gerenteId = "gRicardo";
  s1.parceiroId = "par1";
  createServico(c.db, s1);

  auto s2 = c.servico("s2", 3939.89, 10, "2026-08");
  s2.condominioId = "cond2";
  s2.gerenteId = "gThamiris";
  s2.parceiroId = "par1";
  createServico(c.db, s2);

  // Serviço de THAMIRIS no condomínio Delta (cond3, também da carteira
  // dela): conta como produção normal dela E gera a linha de Delta
  // Síndicos (desconto).
  auto s3 = c.servico("s3", 251.82, 10, "2026-08");
  s3.condominioId = "cond3";
  s3.gerenteId = "gThamiris";
  createServico(c.db, s3);

  // O rateio de Gerentes (a fatia "liberada para comissão") vem sempre de
  // Configurações — como na planilha real, era 45% nesse mês.
  setConfig(c.db, sos_config::kRateioGerentes, "45");

  DashboardEntrada entrada;
  entrada.mesReferencia = "2026-08";
  entrada.gerentes = {
      {"gThamiris", 30, 100},
      {"gRicardo", 30, 100},
  };

  auto dash = montarDashboard(c.db, entrada);

  // arrecadado = Σ comissão (venda × porcentagem) dos serviços pagos do mês
  // — não a venda bruta. Aqui todo serviço tem porcentagem 10%.
  CHECK(dash.arrecadado == doctest::Approx((2362.20 + 3939.89 + 251.82) * 0.10));
  // liberado = arrecadado × percentual de comissão
  CHECK(dash.liberadoParaComissao == doctest::Approx((2362.20 + 3939.89 + 251.82) * 0.10 * 0.45));

  // Todos os gerentes cadastrados entram — inclusive o "Fulano" do cenário,
  // que não teve venda no mês (é o caso do ULISSES na planilha real).
  auto porNome = [&](const std::string& nome) {
    auto it = std::find_if(dash.gerentes.begin(), dash.gerentes.end(),
                           [&](const DashboardGerenteLinha& l) { return l.gerenteNome == nome; });
    REQUIRE(it != dash.gerentes.end());
    return *it;
  };
  CHECK(dash.gerentes.size() == 3);
  const auto ric = porNome("RICARDO");
  const auto tha = porNome("THAMIRIS");
  // Gerente sem venda no mês aparece zerado, não sumido.
  CHECK(porNome("Fulano").recebido == doctest::Approx(0.0));
  CHECK(porNome("Fulano").comissao == doctest::Approx(0.0));

  // recebido = Σ comissão (venda × porcentagem do serviço) dos condomínios
  // NA CARTEIRA do gerente — a fatia do arrecadado que veio dele, não a
  // venda bruta.
  CHECK(ric.recebido == doctest::Approx(2362.20 * 0.10));
  CHECK(tha.recebido == doctest::Approx((3939.89 + 251.82) * 0.10));
  // carteira = nº de condomínios na carteira
  CHECK(tha.carteira == 2);
  CHECK(ric.carteira == 1);
  // descontos = Σ comissão do Delta Síndicos daquele gerente
  CHECK(tha.descontos == doctest::Approx(125.91));
  CHECK(ric.descontos == doctest::Approx(0.0));
  // comissão = recebido × % do gerente × eficácia − descontos
  CHECK(ric.comissao == doctest::Approx(2362.20 * 0.10 * 0.30));
  // Aqui o desconto (125.91, calculado sobre a venda bruta do Delta
  // Síndicos) é maior que a fatia da THAMIRIS (419.171 × 30% = 125.7513) —
  // comissão nunca vira dívida, fica zerada (mesmo critério de "eficácia
  // reduz a comissão e desconto nunca vira dívida" abaixo).
  CHECK(tha.comissao == doctest::Approx(0.0));
  // gerência líquido = Σ comissão dos gerentes
  CHECK(dash.gerenciaLiquido == doctest::Approx(ric.comissao + tha.comissao));

  // empresas: a parceira aparece com a COMISSÃO (não a venda bruta) dos
  // serviços dela no mês (s3 não tem parceiroId, então não entra aqui)
  REQUIRE(dash.empresas.size() == 1);
  CHECK(dash.empresas[0].empresaNome == "BioPrag");
  CHECK(dash.empresas[0].recebidos == doctest::Approx((2362.20 + 3939.89) * 0.10));

  // o painel do Delta Síndicos traz o lançamento do mês
  CHECK(dash.deltaSindicos.size() == 1);
}

TEST_CASE("dashboard: eficácia reduz a comissão e desconto nunca vira dívida") {
  Cenario c;
  Gerente g;
  g.id = "g1";
  g.nome = "ALENCAR";
  g.createdAt = kNow;
  g.condominioIds = {"cond1"};  // Recebidos segue a carteira, não gerenteId
  createGerente(c.db, g);

  auto s = c.servico("s1", 1000, 10);
  s.gerenteId = "g1";
  createServico(c.db, s);

  DashboardEntrada entrada;
  entrada.mesReferencia = "2026-08";
  entrada.gerentes = {{"g1", 30, 55}};  // eficácia 55%, como ALENCAR
  auto alencarEm = [](const DashboardFechamento& d) {
    auto it = std::find_if(d.gerentes.begin(), d.gerentes.end(),
                           [](const DashboardGerenteLinha& l) { return l.gerenteNome == "ALENCAR"; });
    REQUIRE(it != d.gerentes.end());
    return *it;
  };
  auto dash = montarDashboard(c.db, entrada);
  // recebido = 1000 × 10% (comissão do serviço) = 100; comissão = recebido × 30% × 55%.
  CHECK(alencarEm(dash).comissao == doctest::Approx(1000 * 0.10 * 0.30 * 0.55));

  // Um desconto maior que o bruto zera a comissão (não fica negativa). O
  // desconto vem de um serviço pago em condomínio Delta com o gerenteId de
  // ALENCAR — NÃO precisa estar na carteira dele (o desconto casa por
  // gerenteId do serviço de origem, não por carteira).
  Condominio condDelta;
  condDelta.id = "condDelta";
  condDelta.nome = "Edifício Delta";
  condDelta.deltaSindica = true;
  condDelta.createdAt = kNow;
  createCondominio(c.db, condDelta);
  // kDeltaSindica no padrão de fábrica (15%): 10000 × 15% = 1500, bem maior
  // que o bruto de ALENCAR (165).
  auto sDelta = c.servico("sDelta", 10000, 10);
  sDelta.condominioId = "condDelta";
  sDelta.gerenteId = "g1";
  createServico(c.db, sDelta);

  auto dash2 = montarDashboard(c.db, entrada);
  CHECK(alencarEm(dash2).comissao == doctest::Approx(0.0));
}

TEST_CASE("dashboard: retrato salvo nunca some do histórico") {
  Cenario c;
  DashboardSalvo snap;
  snap.id = "dash1";
  snap.mesReferencia = "2026-08";
  snap.dadosJson = R"({"arrecadado":17117.36})";
  snap.geradoEm = kNow;
  snap.createdAt = kNow;
  auto salvo = salvarDashboard(c.db, snap);
  CHECK(salvo.mesReferencia == "2026-08");

  // Gerar de novo o MESMO mês acrescenta uma entrada — não substitui.
  snap.id = "dash2";
  snap.dadosJson = R"({"arrecadado":17200.00})";
  salvarDashboard(c.db, snap);
  CHECK(listDashboards(c.db).size() == 2);
  CHECK(findDashboard(c.db, "dash1")->dadosJson == R"({"arrecadado":17117.36})");

  DashboardSalvo mesRuim = snap;
  mesRuim.id = "x";
  mesRuim.mesReferencia = "2026-13";
  CHECK_THROWS_AS(salvarDashboard(c.db, mesRuim), std::invalid_argument);
}

TEST_CASE("eficaciaDe: fórmula da meta por condomínio") {
  // Sem carteira ou sem meta configurada não há como cobrar meta de
  // ninguém — caso do PAULO/ULISSES na planilha, que ficam em 100%.
  CHECK(eficaciaDe(1000, 0, 120) == doctest::Approx(100.0));
  CHECK(eficaciaDe(1000, 10, 0) == doctest::Approx(100.0));
  // Produção por condomínio exatamente na meta, ou acima dela, é 100% —
  // nunca passa disso (=SE(I4/J4>=120;100;...) da planilha real).
  CHECK(eficaciaDe(1200, 10, 120) == doctest::Approx(100.0));
  CHECK(eficaciaDe(2000, 10, 120) == doctest::Approx(100.0));
  // Metade da meta por condomínio -> metade da eficácia.
  CHECK(eficaciaDe(600, 10, 120) == doctest::Approx(50.0));
}

TEST_CASE("dashboard: eficácia é medida pelo recebido (comissão), não pela venda bruta") {
  // Um serviço com porcentagem baixa pode ter uma venda enorme e ainda assim
  // gerar pouca comissão — usar a venda bruta contra a meta (que é um alvo em
  // R$ de COMISSÃO por condomínio) inflava a "produção" e nunca deixava a
  // eficácia cair abaixo de 100%, mesmo quando o gerente está bem abaixo da
  // meta de verdade.
  Cenario c;
  Gerente g;
  g.id = "g1";
  g.nome = "BAIXA PORCENTAGEM";
  g.createdAt = kNow;
  g.condominioIds = {"cond1"};
  createGerente(c.db, g);

  // venda 6.000, porcentagem 1% -> recebido (comissão) = 60. Meta por
  // condomínio padrão = 120, carteira = 1 condomínio -> meta = 120.
  auto s = c.servico("s1", 6000, 1);
  createServico(c.db, s);

  DashboardEntrada entrada;
  entrada.mesReferencia = "2026-08";
  auto dash = montarDashboard(c.db, entrada);

  auto it = std::find_if(dash.gerentes.begin(), dash.gerentes.end(),
                         [](const DashboardGerenteLinha& l) { return l.gerenteNome == "BAIXA PORCENTAGEM"; });
  REQUIRE(it != dash.gerentes.end());
  CHECK(it->recebido == doctest::Approx(60.0));
  // Se a eficácia fosse calculada contra a venda bruta (6.000 >> 120), daria
  // 100%. Contra o recebido (60, metade da meta de 120), é 50%.
  CHECK(it->eficacia == doctest::Approx(50.0));
}

TEST_CASE("dashboard: gerente sem override usa porcentagem/eficácia de Configurações") {
  Cenario c;
  Gerente g;
  g.id = "g1";
  g.nome = "SEM OVERRIDE";
  g.createdAt = kNow;
  g.condominioIds = {"cond1"};
  createGerente(c.db, g);

  auto s = c.servico("s1", 1200, 10);  // condominioId default = cond1
  createServico(c.db, s);

  setConfig(c.db, sos_config::kRateioGerentes, "40");
  setConfig(c.db, sos_config::kMetaPorCondominio, "120");

  DashboardEntrada entrada;
  entrada.mesReferencia = "2026-08";
  // entrada.gerentes fica vazio de propósito — é o mês recém-gerado, antes
  // de qualquer edição manual na tela de Fechamento.
  auto dash = montarDashboard(c.db, entrada);

  auto it = std::find_if(dash.gerentes.begin(), dash.gerentes.end(),
                         [](const DashboardGerenteLinha& l) { return l.gerenteNome == "SEM OVERRIDE"; });
  REQUIRE(it != dash.gerentes.end());
  CHECK(it->porcentagem == doctest::Approx(40.0));
  // recebido 120 (comissão: 1200 × 10%) ÷ 1 condomínio na carteira == meta exata -> 100%.
  CHECK(it->eficacia == doctest::Approx(100.0));
  // recebido = 1200 × 10% (comissão do serviço) = 120; comissão = recebido × 40%.
  CHECK(it->comissao == doctest::Approx(1200 * 0.10 * 0.40));
  CHECK(it->retido == doctest::Approx(0.0));
}

TEST_CASE("dashboard: retido é a fatia do bruto perdida por eficácia abaixo de 100%") {
  Cenario c;
  Gerente g;
  g.id = "g1";
  g.nome = "ALENCAR";
  g.createdAt = kNow;
  g.condominioIds = {"cond1"};
  createGerente(c.db, g);

  auto s = c.servico("s1", 1000, 10);
  createServico(c.db, s);

  DashboardEntrada entrada;
  entrada.mesReferencia = "2026-08";
  entrada.gerentes = {{"g1", 30, 55}};  // eficácia sobrescrita em 55%
  auto dash = montarDashboard(c.db, entrada);

  auto it = std::find_if(dash.gerentes.begin(), dash.gerentes.end(),
                         [](const DashboardGerenteLinha& l) { return l.gerenteNome == "ALENCAR"; });
  REQUIRE(it != dash.gerentes.end());
  // recebido = 1000 × 10% (comissão do serviço) = 100; bruto = 100 × 30% =
  // 30; retido = 30 × (1 − 55%) = 13.5.
  CHECK(it->retido == doctest::Approx(13.5));
  CHECK(dash.retido == doctest::Approx(13.5));
}
