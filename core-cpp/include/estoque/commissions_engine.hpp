#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/db.hpp"

// Gestão SOS: Serviços (planilha de vendas/comissões) e Fechamentos.
//
//   Servico (venda, porcentagem, mês de referência) ──> comissaoDe() [calculada]
//        │
//        └──< fechamentoId >── Fechamento (totais denormalizados no momento do fechar)
//
// Um Servico referencia condomínio, gerente e parceiro pelo id E pelo nome
// (denormalizado, sem FK — mesmo critério de requests.department_id): é um
// lançamento financeiro, e excluir o cadastro de origem anos depois não pode
// apagar nem quebrar a comissão já lançada.
//
// `comissao` nunca é gravada: é sempre venda × porcentagem ÷ 100, calculada na
// leitura (mesmo espírito do vencimento em dates_engine) — nunca diverge.
//
// Fechar um mês trava todos os serviços em aberto daquele mês (fechamentoId
// deixa de ser vazio) e grava os totais no Fechamento. Um serviço fechado não
// pode ser editado nem excluído — só reabrirFechamento desfaz isso, e mesmo
// assim o REGISTRO do fechamento nunca é excluído (ver Fechamento abaixo):
// todo fechamento permanece no histórico para sempre, então o mesmo mês pode
// ser fechado mais de uma vez ao longo do tempo (fecha → reabre para corrigir
// um lançamento → fecha de novo), cada fechamento sendo uma entrada
// permanente e imutável com os totais QUE FORAM fechados naquele momento.
namespace estoque {

struct Servico {
  std::string id;
  int numero = 0;  // "ID" da planilha — AUTOINCREMENT, nunca se repete
  std::string codigo;
  std::string condominioId;
  std::string condominioNome;
  std::string gerenteId;
  std::string gerenteNome;
  std::string parceiroId;
  std::string parceiroNome;
  double venda = 0;
  double porcentagem = 0;
  std::string dataReferencia;  // "YYYY-MM"
  std::string fechamentoId;    // vazio = em aberto
  std::string observacoes;
  std::string createdAt;
  // Venda lançada não é comissão garantida: só entra no fechamento e na
  // distribuição do Dashboard de Fechamento depois que o cliente PAGOU (ver
  // fecharMes e montarDashboard abaixo). `dataPagamento` é obrigatória
  // quando `pago` é true, e ignorada (limpa) quando não é.
  bool pago = false;
  std::string dataPagamento;  // "YYYY-MM-DD", vazia enquanto não pago
};

// venda * porcentagem / 100 — nunca gravado, sempre calculado.
double comissaoDe(const Servico& s);

std::vector<Servico> listServicos(Database& db);
std::optional<Servico> findServico(Database& db, const std::string& id);
// condominioId precisa existir (é o que o serviço está vinculado); gerenteId
// e parceiroId são opcionais, mas quando informados precisam existir — e
// parceiroId precisa ser uma empresa marcada como parceira.
Servico createServico(Database& db, const Servico& input);
// Recusa editar um serviço já fechado.
Servico updateServico(Database& db, const Servico& input);
// Recusa excluir um serviço já fechado.
void deleteServico(Database& db, const std::string& id);

struct Fechamento {
  std::string id;
  std::string mesReferencia;  // "YYYY-MM"
  int quantidadeServicos = 0;
  double totalVenda = 0;
  double totalComissao = 0;
  std::string observacoes;
  std::string fechadoEm;
  std::string reabertoEm;  // vazio = nunca foi reaberto
  std::string createdAt;
};

std::vector<Fechamento> listFechamentos(Database& db);
// Fecha os serviços em aberto (fechamentoId vazio) E PAGOS com dataReferencia
// == mesReferencia — quem ainda não pagou fica de fora do fechamento (e da
// comissão), continua em aberto, e entra num fechamento futuro assim que for
// marcado como pago. Recusa só quando não há nenhum serviço aberto E pago
// naquele mês — fechar de novo um mês que já teve outro fechamento é
// permitido de propósito (ver o comentário no topo do arquivo).
Fechamento fecharMes(Database& db, const std::string& id, const std::string& mesReferencia,
                     const std::string& observacoes, const std::string& fechadoEm,
                     const std::string& createdAt);
// Solta os serviços daquele fechamento (fechamentoId volta a vazio) para
// poderem ser corrigidos — mas o REGISTRO do fechamento nunca é excluído, só
// marcado com `reabertoEm`: ele continua no histórico como o retrato de
// quando e com que totais aquele fechamento aconteceu.
void reabrirFechamento(Database& db, const std::string& id, const std::string& reabertoEm);

// ---- Delta Síndicos ----
//
// Segunda "planilha" de comissões do módulo, irmã de Servico: mesma ideia
// (venda × porcentagem num mês de referência), mas a comissão é do SÍNDICO
// do condomínio, não de gerente/parceiro.
//
// NÃO é mais lançada à mão: é inteiramente PUXADA de Serviços. Um condomínio
// marcado com `Condominio::deltaSindica` (Gestão SOS > Delta Síndicos tem a
// tela pra cadastrar quais) faz TODO serviço pago lançado nele virar uma
// linha aqui automaticamente — `id`/`numero` são os do próprio Servico de
// origem (não existe id próprio, porque não existe INSERT próprio), `sindico`
// é lido do cadastro do condomínio na hora ("Delta", tipicamente), e
// `porcentagem` é sempre sos_config::kDeltaSindica (a fatia da Delta dentro
// do rateio de gerentes) — nunca um valor digitado linha a linha.
//
// Só entram serviços PAGOS (mesmo critério de montarDashboard/fecharMes):
// venda sem pagamento confirmado não gera comissão pra ninguém, Delta
// incluída. Não há fechamento próprio aqui (nunca houve) — a planilha
// simplesmente reflete o que estiver em Serviços a qualquer momento.
struct DeltaSindico {
  std::string id;
  int numero = 0;  // = Servico::numero de origem
  std::string condominioId;
  std::string condominioNome;
  // O gerente do serviço de origem: é ele que liga esta planilha ao dashboard
  // de fechamento — o DESCONTO de cada gerente lá é a soma do que ele "paga"
  // aqui no mês (ver montarDashboard).
  std::string gerenteId;
  std::string gerenteNome;
  std::string sindico;
  double venda = 0;
  double porcentagem = 0;
  std::string dataReferencia;  // "YYYY-MM"
  std::string observacoes;
  std::string createdAt;
};

// venda * porcentagem / 100 — nunca gravado, sempre calculado (mesmo
// critério de comissaoDe acima).
double comissaoDeltaDe(const DeltaSindico& d);

// Monta a lista inteira a partir de listServicos(db): um item por serviço
// PAGO cujo condomínio tem deltaSindica=true. Mais recente primeiro (mesmo
// critério de ordenação de listServicos).
std::vector<DeltaSindico> listDeltaSindicos(Database& db);

// ---- Dashboard de fechamento ----
//
// O retrato que hoje é montado no Excel a cada mês, com quatro painéis. Tudo
// que dá para DERIVAR dos lançamentos é derivado aqui (eram os SOMASES da
// planilha); o que não sai dos dados vem do usuário em `DashboardEntrada` e
// é gravado junto no retrato.
//
// Fórmulas derivadas (conferidas contra a planilha real de jul/26):
//   arrecadado          = Σ comissão (venda × porcentagem) dos serviços
//                         PAGOS do mês — não a venda bruta (não pago não
//                         entra na distribuição de comissão de ninguém)
//   liberadoParaComissao= arrecadado × percentualComissao
//   gerente.recebido    = Σ comissão dos serviços pagos da CARTEIRA daquele
//                         gerente no mês, só os com porcentagem > 0 — é a
//                         fatia do arrecadado que veio dos clientes dele
//                         (a soma do recebido de todos os gerentes bate com
//                         o arrecadado, salvo condomínio fora de carteira)
//   gerente.carteira    = nº de condomínios na carteira do gerente
//   gerente.meta        = metaPorCondominio × carteira
//   gerente.descontos   = Σ comissão do Delta Síndicos daquele gerente no mês
//   gerente.eficacia    = recebido do gerente (comissão, não venda bruta) ÷
//                         carteira, contra metaPorCondominio — sem isso um
//                         serviço de porcentagem baixa infla a "produção" com
//                         venda bruta e nunca deixa a eficácia cair abaixo de
//                         100%, mesmo quando o gerente está bem abaixo da meta
//   gerente.comissao    = recebido × porcentagem × eficácia − descontos
//   gerenciaLiquido     = Σ comissão dos gerentes
//   empresa.recebidos   = Σ comissão (venda × porcentagem) dos serviços
//                         daquele parceiro no mês — não a venda bruta (é o
//                         quanto da FL veio de cada parceira, não o quanto a
//                         parceira vendeu)
struct DashboardGerenteEntrada {
  std::string gerenteId;
  double porcentagem = 0;  // a coluna "(%)" — 30% na planilha
  double eficacia = 100;   // a coluna "EFICÁCIA" — 0..100
  // A coluna "CARTEIRA" — o usuário digita esse número todo mês (esclarecido
  // por ele: "o número de condomínios de cada carteira eu vou adicionar
  // manual"), não é necessariamente igual ao tamanho real da carteira
  // cadastrada em Gestão SOS › Carteiras. 0 = "ainda não preenchido nesta
  // entrada" -> montarDashboard pré-sugere o tamanho real da carteira.
  int carteira = 0;
};

struct DashboardLinhaValor {
  std::string rotulo;
  double valor = 0;
};

// O que o usuário informa (não sai dos lançamentos).
struct DashboardEntrada {
  std::string mesReferencia;  // "YYYY-MM"
  double flLucro = 0;
  double percentualComissao = 0;   // 0..100 — 45% na planilha
  double percentualDistribuido = 0;  // 0..100 — 42% na planilha
  double retido = 0;
  std::vector<DashboardLinhaValor> distribuicaoCompras;
  std::vector<DashboardLinhaValor> distribuicaoDelta;
  std::vector<DashboardGerenteEntrada> gerentes;
  std::string observacoes;
};

struct DashboardGerenteLinha {
  std::string gerenteId;
  std::string gerenteNome;
  int carteira = 0;
  // metaPorCondominio × carteira (a coluna "META").
  double meta = 0;
  double porcentagem = 0;
  double eficacia = 0;
  double descontos = 0;
  // Σ comissão (venda × porcentagem) dos serviços pagos da CARTEIRA deste
  // gerente no mês, só os com porcentagem > 0 (a coluna "RECEBIDO") — a
  // fatia do `arrecadado` do dashboard que veio dele. Não é mais a venda
  // bruta: era a origem do "Recebido" não bater com o "Arrecadado" do topo
  // da tela.
  double recebido = 0;
  double comissao = 0;
  // Fatia do (recebido × porcentagem) perdida por não atingir 100% de
  // eficácia — fica retida para a FL, nunca é paga a ninguém.
  double retido = 0;
};

struct DashboardEmpresaLinha {
  std::string empresaId;
  std::string empresaNome;
  double recebidos = 0;
};

struct DashboardFechamento {
  std::string mesReferencia;
  double arrecadado = 0;
  double flLucro = 0;
  double liberadoParaComissao = 0;
  double percentualComissao = 0;
  double percentualDistribuido = 0;
  double gerenciaLiquido = 0;
  double retido = 0;
  std::vector<DashboardLinhaValor> distribuicaoCompras;
  std::vector<DashboardLinhaValor> distribuicaoDelta;
  std::vector<DashboardGerenteLinha> gerentes;
  std::vector<DeltaSindico> deltaSindicos;
  std::vector<DashboardEmpresaLinha> empresas;
  std::string observacoes;
};

// Monta o dashboard do mês a partir dos lançamentos + do que o usuário
// informou. Não grava nada — é só o cálculo (mesmo espírito de
// computeReport): quem grava é salvarDashboard.
DashboardFechamento montarDashboard(Database& db, const DashboardEntrada& entrada);

struct DashboardSalvo {
  std::string id;
  std::string mesReferencia;
  std::string dadosJson;  // o retrato inteiro, como foi apresentado
  std::string observacoes;
  std::string geradoEm;
  std::string createdAt;
};

// Retratos em ordem do mais recente para o mais antigo. Nunca são alterados
// nem excluídos: gerar de novo o mesmo mês acrescenta uma entrada nova.
std::vector<DashboardSalvo> listDashboards(Database& db);
std::optional<DashboardSalvo> findDashboard(Database& db, const std::string& id);
DashboardSalvo salvarDashboard(Database& db, const DashboardSalvo& input);

// ---- Pagamentos (Programar pagamento / Histórico de pagamentos) ----
//
// A proposta de pagamento de um mês (quem recebe quanto — Gerentes,
// Suprimentos, Delta — e as Chaves PIX) é montada em Api::montarPagamento a
// partir do Dashboard de Fechamento JÁ SALVO daquele mês (histórico de
// Dashboard de Fechamento) — nunca recalculada aqui: o dashboard salvo é o
// retrato aprovado, e Pagamento só decide QUEM efetivamente recebe daquele
// total. `dadosJson` guarda esse retrato (totais + lista de linhas com o
// valor sugerido e se foi autorizado), do jeito que foi apresentado —
// mesmo formato de DashboardSalvo::dadosJson.
//
// Diferente de Dashboard/Fechamento, aqui um mês tem UM registro só (índice
// único em sos_pagamentos): salvarPagamento faz UPSERT por id — fechar de
// novo ou corrigir depois regrava o mesmo registro, nunca duplica.
struct PagamentoSalvo {
  std::string id;
  std::string mesReferencia;
  std::string dadosJson;
  std::string observacoes;
  bool fechado = false;
  std::string geradoEm;
  std::string fechadoEm;  // vazio enquanto não fechado
  std::string createdAt;
};

// Mais recente primeiro (mesmo critério de listDashboards).
std::vector<PagamentoSalvo> listPagamentosSos(Database& db);
std::optional<PagamentoSalvo> findPagamentoSos(Database& db, const std::string& id);
std::optional<PagamentoSalvo> findPagamentoSosPorMes(Database& db, const std::string& mesReferencia);
// INSERT se o id não existir ainda, UPDATE completo se existir — é o que
// permite fechar e, depois, corrigir o mesmo registro (ver comentário acima).
PagamentoSalvo salvarPagamentoSos(Database& db, const PagamentoSalvo& input);

// ---- configurações (chave/valor simples) ----
//
// TODAS as porcentagens do fechamento moram aqui, e não no código: elas mudam
// conforme a necessidade da empresa, e um número desses cravado no fonte
// viraria uma versão nova do app a cada renegociação.
//
// O rateio de 100% do que entra de vendas no mês:
//   kRateioFl (55) + kRateioGerentes (30) + kRateioSuprimentos (15) = 100
// e, dentro da fatia de Suprimentos:
//   kSuprimentosEncarregado (78) + kSuprimentosAssistente (22) = 100
//
// kMetaPorCondominio (120) é a base da EFICÁCIA de cada gerente:
//   eficácia = min(100%, (produção ÷ condomínios da carteira) ÷ meta)
// Quem não atinge 100% recebe o proporcional, e a diferença fica retida
// para a FL. A eficácia calculada pode ser sobrescrita à mão no fechamento
// (um diretor que não atingiu a meta, por exemplo) — ver DashboardGerenteEntrada.
namespace sos_config {
constexpr const char* kPorcentagemPadrao = "porcentagem_padrao";
constexpr const char* kRateioFl = "rateio_fl";
constexpr const char* kRateioGerentes = "rateio_gerentes";
constexpr const char* kRateioSuprimentos = "rateio_suprimentos";
constexpr const char* kSuprimentosEncarregado = "suprimentos_encarregado";
constexpr const char* kSuprimentosAssistente = "suprimentos_assistente";
constexpr const char* kMetaPorCondominio = "meta_por_condominio";
// Condomínio em que a DELTA é a síndica: os 30% do gerente se dividem entre
// ela e ele. 15 + 15 na regra vigente — por isso o "PAGAR" da planilha de
// Delta Síndicos é metade do "VALOR".
constexpr const char* kDeltaSindica = "delta_sindica";
constexpr const char* kDeltaGerente = "delta_gerente";
// Dados bancários da própria Delta (empresa), pra Programar Pagamento — não
// existe cadastro de síndico com PIX próprio (síndico é só nome de texto em
// DeltaSindico::sindico), então o pagamento da fatia dela vai pra ela mesma,
// como uma entidade só, não um por síndico.
constexpr const char* kDeltaChavePix = "delta_chave_pix";
constexpr const char* kDeltaTitular = "delta_titular";
}  // namespace sos_config

// Os padrões de fábrica (o acordo vigente quando o módulo nasceu). Só valem
// enquanto ninguém salvou outro valor em Configurações.
namespace sos_config_padrao {
constexpr double kRateioFl = 55;
constexpr double kRateioGerentes = 30;
constexpr double kRateioSuprimentos = 15;
constexpr double kSuprimentosEncarregado = 78;
constexpr double kSuprimentosAssistente = 22;
constexpr double kMetaPorCondominio = 120;
constexpr double kDeltaSindica = 15;
constexpr double kDeltaGerente = 15;
}  // namespace sos_config_padrao

// Eficácia de um gerente: produção ÷ condomínios da carteira, medida contra a
// meta por condomínio. Devolve 0..100. Carteira zerada ou meta zerada devolve
// 100 (não há como cobrar meta de quem não tem carteira — é o caso do PAULO
// e do ULISSES na planilha).
double eficaciaDe(double producao, int condominiosNaCarteira, double metaPorCondominio);

std::string getConfig(Database& db, const std::string& key, const std::string& def);
void setConfig(Database& db, const std::string& key, const std::string& value);

}  // namespace estoque
