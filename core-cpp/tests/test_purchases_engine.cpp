#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/purchases_engine.hpp"

#include <algorithm>

#include "estoque/companies_engine.hpp"
#include "estoque/dates_engine.hpp"

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

    Empresa fornecedor;
    fornecedor.id = "forn1";
    fornecedor.nome = "Material Forte Ltda";
    fornecedor.createdAt = kNow;
    createEmpresa(db, fornecedor);

    Empresa forn2;
    forn2.id = "forn2";
    forn2.nome = "Barata & Cia";
    forn2.emails = "contato@baratacia.com.br";
    forn2.createdAt = kNow;
    createEmpresa(db, forn2);

    Empresa forn3;
    forn3.id = "forn3";
    forn3.nome = "Premium Serviços";
    forn3.emails = "orcamento@premium.com.br";
    forn3.createdAt = kNow;
    createEmpresa(db, forn3);
  }

  Aquisicao aquisicao(const std::string& id, double valor) {
    Aquisicao a;
    a.id = id;
    a.fornecedorId = "forn1";
    a.descricao = "Compra de materiais";
    a.valor = valor;
    a.dataCompra = "2026-08-10";
    a.createdAt = kNow;
    return a;
  }

  OrdemOrcamento ordem(const std::string& id, const std::string& descricao = "Reforma do hall") {
    OrdemOrcamento o;
    o.id = id;
    o.condominioId = "cond1";
    o.descricao = descricao;
    o.createdAt = kNow;
    return o;
  }

  Pagamento pagamento(const std::string& id, const std::string& aquisicaoId) {
    Pagamento p;
    p.id = id;
    p.aquisicaoId = aquisicaoId;
    p.notaFiscal = "NF-001";
    p.valorTotal = 300;
    p.dataEmissao = "2026-08-12";
    p.createdAt = kNow;
    Parcela p1;
    p1.valor = 150;
    p1.vencimento = "2026-09-10";
    Parcela p2;
    p2.valor = 150;
    p2.vencimento = "2026-10-10";
    p.parcelas = {p1, p2};
    return p;
  }
};

}  // namespace

TEST_CASE("createAquisicao: resolve o nome do fornecedor e recusa fornecedor inexistente") {
  Cenario c;
  auto a = createAquisicao(c.db, c.aquisicao("a1", 500));
  CHECK(a.fornecedorNome == "Material Forte Ltda");

  auto ruim = c.aquisicao("a2", 100);
  ruim.fornecedorId = "fantasma";
  CHECK_THROWS_AS(createAquisicao(c.db, ruim), NotFoundError);

  auto semDescricao = c.aquisicao("a3", 100);
  semDescricao.descricao = "";
  CHECK_THROWS_AS(createAquisicao(c.db, semDescricao), std::invalid_argument);
}

TEST_CASE("updateAquisicao tolera fornecedor excluído quando o id não muda") {
  Cenario c;
  auto a = createAquisicao(c.db, c.aquisicao("a1", 500));
  deleteEmpresa(c.db, "forn1");

  auto edit = a;
  edit.valor = 700;
  auto atualizado = updateAquisicao(c.db, edit);
  CHECK(atualizado.valor == doctest::Approx(700));
  CHECK(atualizado.fornecedorNome == "Material Forte Ltda");  // preservado
}

TEST_CASE("deleteAquisicao recusa quando há pagamentos lançados") {
  Cenario c;
  auto a = createAquisicao(c.db, c.aquisicao("a1", 500));
  createPagamento(c.db, c.pagamento("p1", "a1"));

  CHECK_THROWS_AS(deleteAquisicao(c.db, "a1"), std::invalid_argument);

  deletePagamento(c.db, "p1");
  deleteAquisicao(c.db, "a1");  // agora permitido
  CHECK_FALSE(findAquisicao(c.db, "a1").has_value());
}

TEST_CASE("createOrdemOrcamento: resolve o condomínio, nasce pendente e sem propostas") {
  Cenario c;
  auto o = createOrdemOrcamento(c.db, c.ordem("ord1"));
  CHECK(o.condominioNome == "Edifício Central");
  CHECK(o.status == ordem_orcamento_status::kPendente);
  CHECK(o.numero > 0);  // "ID" sequencial (AUTOINCREMENT)
  CHECK(o.propostas.empty());

  auto ruim = c.ordem("ord2");
  ruim.condominioId = "fantasma";
  CHECK_THROWS_AS(createOrdemOrcamento(c.db, ruim), NotFoundError);

  auto semDescricao = c.ordem("ord3", "");
  CHECK_THROWS_AS(createOrdemOrcamento(c.db, semDescricao), std::invalid_argument);
}

TEST_CASE("updateOrdemOrcamentoInfo só muda descrição/observações") {
  Cenario c;
  createOrdemOrcamento(c.db, c.ordem("ord1"));
  auto atualizado = updateOrdemOrcamentoInfo(c.db, "ord1", "Reforma da fachada", "Urgente");
  CHECK(atualizado.descricao == "Reforma da fachada");
  CHECK(atualizado.observacoes == "Urgente");
  CHECK(atualizado.status == ordem_orcamento_status::kPendente);  // ação livre não mexe no status
}

TEST_CASE("solicitarOrcamentoParaEmpresas: cria uma proposta por empresa, avança status, não duplica") {
  Cenario c;
  createOrdemOrcamento(c.db, c.ordem("ord1"));

  auto depois = solicitarOrcamentoParaEmpresas(
      c.db, "ord1", {{"forn2", "Barata & Cia"}, {"forn3", "Premium Serviços"}}, kNow);
  CHECK(depois.status == ordem_orcamento_status::kSolicitado);
  CHECK(depois.dataSolicitacao == kNow);
  REQUIRE(depois.propostas.size() == 2);
  CHECK(depois.propostas[0].valor < 0);  // sem resposta ainda

  // Chamar de novo com uma empresa repetida + uma nova: não duplica a
  // repetida (continua 3 propostas, não 4), mas REENVIA pra ela — atualiza
  // emailEnviadoEm em vez de ignorar, porque selecionar de novo é sempre um
  // pedido explícito de reenvio — e dataSolicitacao (a janela dos 25 dias)
  // não muda.
  constexpr const char* kDepois = "2026-08-20T12:00:00.000Z";
  auto maisTarde = solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"forn2", "Barata & Cia"}, {"forn1", "Material Forte Ltda"}}, kDepois);
  CHECK(maisTarde.propostas.size() == 3);
  CHECK(maisTarde.dataSolicitacao == kNow);  // não reiniciou
  auto repetida = std::find_if(maisTarde.propostas.begin(), maisTarde.propostas.end(),
                               [](const PropostaOrcamento& p) { return p.empresaId == "forn2"; });
  REQUIRE(repetida != maisTarde.propostas.end());
  CHECK(repetida->emailEnviadoEm == kDepois);  // reenviada de verdade, não ignorada

  CHECK_THROWS_AS(solicitarOrcamentoParaEmpresas(c.db, "ord1", {}, kNow), std::invalid_argument);
  CHECK_THROWS_AS(solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"fantasma", "X"}}, kNow), NotFoundError);
}

TEST_CASE("propostas vêm ordenadas do mais caro pro mais barato, sem resposta por último") {
  Cenario c;
  createOrdemOrcamento(c.db, c.ordem("ord1"));
  auto depois = solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"forn2", "Barata & Cia"}, {"forn3", "Premium Serviços"}}, kNow);
  auto idBarata = depois.propostas[0].empresaId == "forn2" ? depois.propostas[0].id : depois.propostas[1].id;
  auto idPremium = depois.propostas[0].empresaId == "forn3" ? depois.propostas[0].id : depois.propostas[1].id;

  setPropostaValor(c.db, idBarata, 500);
  auto ordem2 = *findOrdemOrcamento(c.db, "ord1");
  CHECK(ordem2.propostas[0].id == idBarata);   // única com resposta, vem primeiro
  CHECK(ordem2.propostas[1].valor < 0);

  setPropostaValor(c.db, idPremium, 900);
  auto ordem3 = *findOrdemOrcamento(c.db, "ord1");
  CHECK(ordem3.propostas[0].id == idPremium);  // 900 > 500: mais caro primeiro
  CHECK(ordem3.propostas[1].id == idBarata);

  CHECK_THROWS_AS(setPropostaValor(c.db, idBarata, -5), std::invalid_argument);
}

TEST_CASE("marcarPropostaRecomendada só deixa uma marcada por ordem") {
  Cenario c;
  createOrdemOrcamento(c.db, c.ordem("ord1"));
  auto depois = solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"forn2", "Barata & Cia"}, {"forn3", "Premium Serviços"}}, kNow);
  auto p1 = depois.propostas[0].id, p2 = depois.propostas[1].id;

  auto marcada1 = marcarPropostaRecomendada(c.db, "ord1", p1);
  CHECK(marcada1.propostaRecomendadaId == p1);
  auto ordem1 = *findOrdemOrcamento(c.db, "ord1");
  int marcadas = 0;
  for (const auto& p : ordem1.propostas) if (p.recomendada) ++marcadas;
  CHECK(marcadas == 1);

  auto marcada2 = marcarPropostaRecomendada(c.db, "ord1", p2);
  CHECK(marcada2.propostaRecomendadaId == p2);
  auto ordem2 = *findOrdemOrcamento(c.db, "ord1");
  for (const auto& p : ordem2.propostas) CHECK(p.recomendada == (p.id == p2));  // só p2
}

TEST_CASE("enviarOrcamentoParaCliente avança status e é reenviável (acumula propostas)") {
  Cenario c;
  createOrdemOrcamento(c.db, c.ordem("ord1"));
  CHECK_THROWS_AS(enviarOrcamentoParaCliente(c.db, "ord1", kNow), std::invalid_argument);  // sem proposta nenhuma

  solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"forn2", "Barata & Cia"}}, kNow);
  auto enviado1 = enviarOrcamentoParaCliente(c.db, "ord1", kNow);
  CHECK(enviado1.status == ordem_orcamento_status::kEnviadoCliente);
  CHECK(enviado1.dataEnvioCliente == kNow);

  constexpr const char* kReenvio = "2026-08-18T09:00:00.000Z";
  solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"forn3", "Premium Serviços"}}, kReenvio);
  auto enviado2 = enviarOrcamentoParaCliente(c.db, "ord1", kReenvio);
  CHECK(enviado2.dataEnvioCliente == kReenvio);  // reenvio atualiza a data
  CHECK(enviado2.propostas.size() == 2);         // acumulou, não substituiu
}

TEST_CASE("aprovarPropostaOrcamento marca a vencedora e vira estado final") {
  Cenario c;
  createOrdemOrcamento(c.db, c.ordem("ord1"));
  auto depois = solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"forn2", "Barata & Cia"}}, kNow);
  auto propostaId = depois.propostas[0].id;

  auto aprovado = aprovarPropostaOrcamento(c.db, "ord1", propostaId, kNow);
  CHECK(aprovado.status == ordem_orcamento_status::kAprovado);
  CHECK(aprovado.propostaAprovadaId == propostaId);
  CHECK(aprovado.dataAprovacao == kNow);

  CHECK_THROWS_AS(aprovarPropostaOrcamento(c.db, "ord1", "fantasma", kNow), NotFoundError);
}

TEST_CASE("ordemOrcamentoStatusEfetivo: declina sozinha depois de 25 dias sem resposta, reativar reinicia") {
  Cenario c;
  createOrdemOrcamento(c.db, c.ordem("ord1"));
  auto depois = solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"forn2", "Barata & Cia"}}, "2026-01-01T00:00:00.000Z");

  CHECK(ordemOrcamentoStatusEfetivo(depois, "2026-01-20T00:00:00.000Z") == ordem_orcamento_status::kSolicitado);
  CHECK(ordemOrcamentoStatusEfetivo(depois, "2026-01-26T00:00:00.000Z") == ordem_orcamento_status::kDeclinado);
  // O status GRAVADO nunca muda — é sempre calculado na leitura.
  CHECK(depois.status == ordem_orcamento_status::kSolicitado);

  auto reativado = reativarOrdemOrcamento(c.db, "ord1", "2026-01-26T00:00:00.000Z");
  CHECK(ordemOrcamentoStatusEfetivo(reativado, "2026-01-27T00:00:00.000Z") == ordem_orcamento_status::kSolicitado);
  CHECK(ordemOrcamentoStatusEfetivo(reativado, "2026-02-20T00:00:00.000Z") == ordem_orcamento_status::kDeclinado);

  // Uma vez aprovada, nunca declina, mesmo muito tempo depois.
  auto aprovado = aprovarPropostaOrcamento(c.db, "ord1", depois.propostas[0].id, "2026-01-10T00:00:00.000Z");
  CHECK(ordemOrcamentoStatusEfetivo(aprovado, "2026-06-01T00:00:00.000Z") == ordem_orcamento_status::kAprovado);
}

TEST_CASE("setPropostaAnexo/clearPropostaAnexo gravam e removem a referência") {
  Cenario c;
  createOrdemOrcamento(c.db, c.ordem("ord1"));
  auto depois = solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"forn2", "Barata & Cia"}}, kNow);
  auto propostaId = depois.propostas[0].id;

  auto comAnexo = setPropostaAnexo(c.db, propostaId, "ord1/prop1/proposta.pdf", "pdf");
  CHECK(comAnexo.anexoPath == "ord1/prop1/proposta.pdf");
  CHECK(comAnexo.anexoTipo == "pdf");

  auto semAnexo = clearPropostaAnexo(c.db, propostaId);
  CHECK(semAnexo.anexoPath.empty());
  CHECK(semAnexo.anexoTipo.empty());
}

TEST_CASE("deleteOrdemOrcamento leva as propostas junto (cascade)") {
  Cenario c;
  createOrdemOrcamento(c.db, c.ordem("ord1"));
  solicitarOrcamentoParaEmpresas(c.db, "ord1", {{"forn2", "Barata & Cia"}}, kNow);
  deleteOrdemOrcamento(c.db, "ord1");
  CHECK_FALSE(findOrdemOrcamento(c.db, "ord1").has_value());
}

TEST_CASE("createPagamento exige aquisição existente e grava as parcelas em aberto") {
  Cenario c;
  createAquisicao(c.db, c.aquisicao("a1", 500));
  auto p = createPagamento(c.db, c.pagamento("p1", "a1"));
  REQUIRE(p.parcelas.size() == 2);
  CHECK(p.parcelas[0].numero == 1);
  CHECK(p.parcelas[1].numero == 2);
  CHECK_FALSE(p.parcelas[0].pago);
  CHECK(p.parcelas[0].dataPagamento.empty());

  auto semAquisicao = c.pagamento("p2", "fantasma");
  CHECK_THROWS_AS(createPagamento(c.db, semAquisicao), NotFoundError);

  auto semParcelas = c.pagamento("p3", "a1");
  semParcelas.parcelas.clear();
  CHECK_THROWS_AS(createPagamento(c.db, semParcelas), std::invalid_argument);
}

TEST_CASE("marcarParcela marca e desmarca, exigindo data ao marcar como paga") {
  Cenario c;
  createAquisicao(c.db, c.aquisicao("a1", 500));
  auto p = createPagamento(c.db, c.pagamento("p1", "a1"));
  auto parcelaId = p.parcelas[0].id;

  CHECK_THROWS_AS(marcarParcela(c.db, "p1", parcelaId, true, ""), std::invalid_argument);

  auto pago = marcarParcela(c.db, "p1", parcelaId, true, "2026-09-05");
  CHECK(pago.parcelas[0].pago);
  CHECK(pago.parcelas[0].dataPagamento == "2026-09-05");
  CHECK_FALSE(pago.parcelas[1].pago);  // a outra parcela não é afetada

  auto reaberta = marcarParcela(c.db, "p1", parcelaId, false, "");
  CHECK_FALSE(reaberta.parcelas[0].pago);
  CHECK(reaberta.parcelas[0].dataPagamento.empty());
}

TEST_CASE("updatePagamento preserva parcelas já pagas e zera as novas") {
  Cenario c;
  createAquisicao(c.db, c.aquisicao("a1", 500));
  auto p = createPagamento(c.db, c.pagamento("p1", "a1"));
  marcarParcela(c.db, "p1", p.parcelas[0].id, true, "2026-09-05");

  auto atual = *findPagamento(c.db, "p1");
  Parcela nova;
  nova.valor = 200;
  nova.vencimento = "2026-11-10";
  atual.parcelas.push_back(nova);  // acrescenta uma 3ª parcela
  atual.notaFiscal = "NF-002";

  auto atualizado = updatePagamento(c.db, atual);
  CHECK(atualizado.notaFiscal == "NF-002");
  REQUIRE(atualizado.parcelas.size() == 3);
  CHECK(atualizado.parcelas[0].pago);                     // preservada
  CHECK(atualizado.parcelas[0].dataPagamento == "2026-09-05");
  CHECK_FALSE(atualizado.parcelas[1].pago);                // já existia, em aberto
  CHECK_FALSE(atualizado.parcelas[2].pago);                // nova, nasce em aberto
}

TEST_CASE("deletePagamento leva as parcelas junto (cascade)") {
  Cenario c;
  createAquisicao(c.db, c.aquisicao("a1", 500));
  auto p = createPagamento(c.db, c.pagamento("p1", "a1"));
  deletePagamento(c.db, "p1");
  CHECK_FALSE(findPagamento(c.db, "p1").has_value());
}
