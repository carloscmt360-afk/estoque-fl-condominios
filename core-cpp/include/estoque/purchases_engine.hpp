#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/db.hpp"

// Compras: Aquisições FL, Orçamentos e Acompanhamento de pagamentos.
//
//   Aquisicao (compra geral, ligada a um fornecedor) ──< Pagamento (uma NF) ──< Parcela
//   Orcamento (proposta de fornecedor para um condomínio)
//
// Aquisição e Orçamento são registros independentes um do outro — o pedido
// foi "aquisições e orçamentos linkados aos fornecedores cadastrados", não
// que um vire o outro automaticamente. Orçamento também se liga a um
// CONDOMÍNIO (é uma proposta PARA ele).
//
// `fornecedorId/Nome` e `condominioId/Nome` seguem o critério de
// sos_servicos (commissions_engine.hpp): sem FK, nome denormalizado,
// tolerante a exclusão do cadastro de origem quando o id não muda numa
// edição — é histórico financeiro, não pode quebrar anos depois porque um
// fornecedor saiu do cadastro.
//
// Pagamento é diferente: a ligação com Aquisição é NOVA a cada lançamento
// (não histórico antigo sobrevivendo), então aqui a regra é a oposta —
// aquisicaoId sempre precisa existir, e excluir uma Aquisição com pagamentos
// lançados é recusado (o usuário exclui os pagamentos primeiro). Por isso
// Pagamento NÃO denormaliza dado nenhum da Aquisição: ele é sempre lido
// junto dela (ver pagamentoToJson em api.cpp).
namespace estoque {

// ---- Aquisições ----
struct Aquisicao {
  std::string id;
  std::string fornecedorId;
  std::string fornecedorNome;
  std::string descricao;
  std::string notaFiscal;
  double valor = 0;
  std::string dataCompra;  // "YYYY-MM-DD"
  std::string observacoes;
  std::string createdAt;
  // Anexo da NF: caminho relativo já processado pela camada Rust (foto
  // redimensionada para A4 ou PDF gravado como veio) — mesmo critério de
  // Product::imagePath/thumbnailPath (inventory_engine): o C++ nunca gera
  // nem decodifica bytes de imagem/PDF, só guarda o caminho. anexoTipo é
  // "imagem" ou "pdf" (vazio == sem anexo).
  std::string anexoPath;
  std::string anexoTipo;
};

std::vector<Aquisicao> listAquisicoes(Database& db);
std::optional<Aquisicao> findAquisicao(Database& db, const std::string& id);
Aquisicao createAquisicao(Database& db, const Aquisicao& input);
Aquisicao updateAquisicao(Database& db, const Aquisicao& input);
// Recusa excluir uma aquisição com pagamentos lançados.
void deleteAquisicao(Database& db, const std::string& id);
// anexoPath/anexoTipo já vêm prontos (arquivo já salvo em disco pela ponte
// Rust) — mesmo critério de setProductImage/clearProductImage.
Aquisicao setAquisicaoAnexo(Database& db, const std::string& id, const std::string& anexoPath,
                            const std::string& anexoTipo);
Aquisicao clearAquisicaoAnexo(Database& db, const std::string& id);

// ---- Orçamentos: fluxo de cotação ----
//
// Uma ORDEM (a "ordem de serviço" pedida pelo usuário, presa a um
// condomínio) tem N PROPOSTAS — uma por empresa solicitada. O status da
// ordem só guarda os estados que são resultado de uma AÇÃO explícita:
// pendente (criada, ninguém solicitado ainda) → solicitado (e-mail disparado
// para pelo menos uma empresa) → enviado_cliente (propostas encaminhadas ao
// condomínio) → aprovado (uma proposta escolhida). "Declinado" (25 dias sem
// resposta) nunca é gravado — é sempre CALCULADO na leitura por
// ordemOrcamentoStatusEfetivo, a partir de dataSolicitacao/reabertoEm contra
// a data de hoje (mesmo critério de calcularStatus em dates_engine.cpp).
// Isso deixa "reativar" trivial: só grava reabertoEm, sem reverter um status
// que a aplicação tivesse escrito.
namespace ordem_orcamento_status {
constexpr const char* kPendente = "pendente";
constexpr const char* kSolicitado = "solicitado";
constexpr const char* kEnviadoCliente = "enviado_cliente";
constexpr const char* kAprovado = "aprovado";
// Efetivo apenas (nunca gravado em ordem.status — ver comentário acima).
constexpr const char* kDeclinado = "declinado";
}  // namespace ordem_orcamento_status

// Janela sem resposta antes do declínio automático (dias corridos).
constexpr int kDiasDeclinioAutomatico = 25;

struct PropostaOrcamento {
  std::string id;
  std::string ordemId;
  std::string empresaId;
  std::string empresaNome;
  // Sem resposta ainda == valor < 0 (0 seria ambíguo com "orçamento
  // gratuito"). anexoPath/anexoTipo seguem o critério de
  // Aquisicao::anexoPath ("imagem"|"pdf", vazio == sem anexo) — o C++ nunca
  // decodifica bytes, só guarda o caminho já processado pela Rust.
  double valor = -1;
  std::string anexoPath;
  std::string anexoTipo;
  std::string emailEnviadoEm;  // quando a solicitação foi enviada a esta empresa
  bool recomendada = false;
  std::string createdAt;
  // Detalhes da proposta em si (ver setPropostaDetalhes) — pedidos junto do
  // anexo, porque cada cotação pode ter escopo/forma de pagamento/validade
  // diferentes; NUNCA "resolvidos na hora" de um cadastro fixo como
  // sindico/gerente em outras telas. CNPJ do fornecedor não mora aqui: já
  // existe em Empresa::cnpj (companies_engine.hpp) e é lido de lá — evita
  // duas fontes divergentes para o mesmo CNPJ.
  std::string escopo;
  std::string formaPagamento;
  std::string validade;
};

struct OrdemOrcamento {
  std::string id;
  int numero = 0;  // "ID" sequencial que a tela mostra (AUTOINCREMENT, nunca reciclado)
  std::string condominioId;
  std::string condominioNome;
  std::string descricao;
  std::string observacoes;  // informações adicionais, editável a qualquer momento
  std::string status;       // ordem_orcamento_status::* (nunca kDeclinado — ver acima)
  std::string propostaRecomendadaId;
  std::string propostaAprovadaId;
  std::string dataSolicitacao;
  std::string dataEnvioCliente;
  std::string dataAprovacao;
  std::string reabertoEm;
  std::string createdAt;
  std::vector<PropostaOrcamento> propostas;
};

// Status que a TELA deve mostrar — sobrepõe ordem.status com "declinado"
// quando pendente resposta (ainda solicitado/enviado_cliente, sem proposta
// aprovada) há kDiasDeclinioAutomatico dias ou mais desde
// MAX(dataSolicitacao, reabertoEm). Pura função de leitura, nunca grava nada.
std::string ordemOrcamentoStatusEfetivo(const OrdemOrcamento& ordem, const std::string& hojeIso);

std::vector<OrdemOrcamento> listOrdensOrcamento(Database& db);
std::optional<OrdemOrcamento> findOrdemOrcamento(Database& db, const std::string& id);
// Nasce em pendente, sem proposta nenhuma. input.id é gerado por quem chama
// (mesmo critério de Aquisicao/Pagamento/DeltaSindico) — é ele que liga as
// propostas à ordem; input.condominioNome é ignorado, sempre resolvido do
// cadastro (mesmo critério de resolverNomeCondominio); input.status/numero/
// propostas são ignorados (a ordem sempre nasce pendente, sem número
// atribuído até o INSERT, sem propostas).
OrdemOrcamento createOrdemOrcamento(Database& db, const OrdemOrcamento& input);
// Só descrição/observações (as "informações adicionais") — o resto dos
// campos muda por ação própria (solicitar/enviar/aprovar), não por edição
// livre.
OrdemOrcamento updateOrdemOrcamentoInfo(Database& db, const std::string& id, const std::string& descricao,
                                        const std::string& observacoes);
void deleteOrdemOrcamento(Database& db, const std::string& id);

struct EmpresaSolicitada {
  std::string empresaId;
  std::string empresaNome;
};
// "Solicitar para empresas": cria uma proposta por empresa nova; quem já
// está na lista não duplica a linha, mas é REENVIADA (email_enviado_em
// atualizado, e a Api manda o e-mail de novo) — chamar de novo com uma
// empresa repetida é sempre um pedido explícito de reenvio, quantas vezes o
// usuário achar necessário, mesmo pra quem já respondeu ou já recebeu.
// Avança ordem.status para 'solicitado' (nunca volta um status mais avançado
// para trás). dataSolicitacao só é gravada da PRIMEIRA vez — chamadas
// posteriores (adicionar mais empresas ou reenviar) não reiniciam a janela
// dos 25 dias.
OrdemOrcamento solicitarOrcamentoParaEmpresas(Database& db, const std::string& ordemId,
                                              const std::vector<EmpresaSolicitada>& empresas,
                                              const std::string& nowIso);

// Preenche o retorno de uma proposta (valor + opcionalmente o anexo, que já
// vem processado pela Rust — mesmo critério de setAquisicaoAnexo).
PropostaOrcamento setPropostaValor(Database& db, const std::string& propostaId, double valor);
PropostaOrcamento setPropostaAnexo(Database& db, const std::string& propostaId, const std::string& anexoPath,
                                   const std::string& anexoTipo);
PropostaOrcamento clearPropostaAnexo(Database& db, const std::string& propostaId);

// Escopo/forma de pagamento/validade — grava os três juntos (mesma tela,
// pedidos no momento de anexar a proposta). String vazia é um valor válido
// (limpa o campo), não "não mexer" — a tela sempre manda os três de uma vez.
PropostaOrcamento setPropostaDetalhes(Database& db, const std::string& propostaId,
                                      const std::string& escopo, const std::string& formaPagamento,
                                      const std::string& validade);

// Reenvio da solicitação a uma empresa que JÁ estava na lista — regrava
// emailEnviadoEm=nowIso, sem duplicar proposta nem mexer em status/valor.
// Diferente de solicitarOrcamentoParaEmpresas, que só aceita empresa nova
// (ver comentário lá em cima): este é exatamente o caso de "perdeu o
// e-mail" ou "não respondeu, manda de novo".
PropostaOrcamento reenviarSolicitacaoProposta(Database& db, const std::string& propostaId,
                                              const std::string& nowIso);

// Só uma proposta recomendada por ordem — desmarca as outras. Chamar de novo
// com uma proposta diferente TROCA a recomendação (é assim que se muda de
// ideia); esta função aqui embaixo é para tirar a recomendação por completo.
OrdemOrcamento marcarPropostaRecomendada(Database& db, const std::string& ordemId, const std::string& propostaId);

// Remove a recomendação da ordem sem marcar outra no lugar — para quando quem
// decide muda de ideia e quer voltar ao estado "nenhuma recomendada".
OrdemOrcamento desmarcarPropostaRecomendada(Database& db, const std::string& ordemId);

// "Enviar para o cliente": avança para 'enviado_cliente' e (re)grava
// dataEnvioCliente=nowIso a cada chamada — reenviar (acumulando novas
// propostas) é só chamar de novo.
OrdemOrcamento enviarOrcamentoParaCliente(Database& db, const std::string& ordemId, const std::string& nowIso);

// Marca a proposta vencedora, grava dataAprovacao=nowIso e avança para
// 'aprovado' — estado final, só reversível reabrindo manualmente (não há
// ação de "desaprovar" pedida).
OrdemOrcamento aprovarPropostaOrcamento(Database& db, const std::string& ordemId, const std::string& propostaId,
                                        const std::string& nowIso);

// Reativa uma ordem que o cálculo classificou como declinada — grava
// reabertoEm=nowIso, reiniciando a contagem dos 25 dias a partir de agora.
OrdemOrcamento reativarOrdemOrcamento(Database& db, const std::string& ordemId, const std::string& nowIso);

// ---- Pagamentos (NF + parcelas) ----
struct Parcela {
  std::string id;
  int numero = 0;
  double valor = 0;
  std::string vencimento;  // "YYYY-MM-DD"
  bool pago = false;
  std::string dataPagamento;  // vazio enquanto não pago
  std::string createdAt;
};

struct Pagamento {
  std::string id;
  std::string aquisicaoId;
  std::string notaFiscal;
  double valorTotal = 0;
  std::string dataEmissao;  // "YYYY-MM-DD"
  std::string observacoes;
  std::string createdAt;
  std::vector<Parcela> parcelas;
};

std::vector<Pagamento> listPagamentos(Database& db);
std::optional<Pagamento> findPagamento(Database& db, const std::string& id);
// aquisicaoId precisa existir. input.parcelas é a composição INICIAL (numero
// 1..N se vier 0) — todas nascem em aberto (pago=false).
Pagamento createPagamento(Database& db, const Pagamento& input);
// Atualiza o cabeçalho (NF, data, observações) E regrava a lista de parcelas
// inteira a partir de input.parcelas — mesmo critério de writeCondominios
// (managers_engine): quem edita já carregou o pagamento antes, então reenvia
// o estado atual com os ajustes. Use marcarParcela para o dia a dia (marcar
// uma parcela paga) sem precisar reenviar tudo.
Pagamento updatePagamento(Database& db, const Pagamento& input);
void deletePagamento(Database& db, const std::string& id);
// Marca (ou desmarca) uma parcela como paga. dataPagamento é ignorada quando
// pago=false (a parcela volta a "em aberto", sem data de baixa).
Pagamento marcarParcela(Database& db, const std::string& pagamentoId, const std::string& parcelaId,
                        bool pago, const std::string& dataPagamento);

}  // namespace estoque
