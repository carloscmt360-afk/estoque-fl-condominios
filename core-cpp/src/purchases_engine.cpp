#include "estoque/purchases_engine.hpp"

#include <algorithm>
#include <stdexcept>

#include "estoque/companies_engine.hpp"
#include "estoque/dates_engine.hpp"
#include "estoque/time_utils.hpp"

namespace estoque {

namespace {

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string textOrEmpty(Statement& st, int idx) { return st.columnIsNull(idx) ? "" : st.columnText(idx); }

// "YYYY-MM-DD": exatamente 10 caracteres, dígitos nos lugares certos, sem
// <regex> — mesmo critério do resto do core-cpp.
bool isValidData(const std::string& s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
  for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

// ---------------------------------------------------------------- Aquisições

constexpr const char* kAquisicaoCols =
    "id, fornecedor_id, fornecedor_nome, descricao, nota_fiscal, valor, data_compra, "
    "observacoes, created_at, anexo_path, anexo_tipo";

Aquisicao rowToAquisicao(Statement& st) {
  Aquisicao a;
  a.id = st.columnText(0);
  a.fornecedorId = st.columnText(1);
  a.fornecedorNome = st.columnText(2);
  a.descricao = st.columnText(3);
  a.notaFiscal = textOrEmpty(st, 4);
  a.valor = st.columnDouble(5);
  a.dataCompra = st.columnText(6);
  a.observacoes = textOrEmpty(st, 7);
  a.createdAt = st.columnText(8);
  a.anexoPath = textOrEmpty(st, 9);
  a.anexoTipo = textOrEmpty(st, 10);
  return a;
}

// Mesmo critério de resolverNomes em commissions_engine.cpp: o nome digitado
// nunca é usado, é sempre resolvido do cadastro. Quando o fornecedorId NÃO
// muda numa edição, o cadastro pode ter sido excluído nesse meio-tempo sem
// travar a edição de outro campo — o nome já gravado é preservado. Um id
// NOVO sempre precisa existir.
std::string resolverNomeFornecedor(Database& db, const std::string& fornecedorId, bool mudou,
                                   const std::string& nomeAnterior) {
  auto emp = findEmpresa(db, fornecedorId);
  if (mudou) {
    if (!emp) throw NotFoundError("fornecedor não encontrado: " + fornecedorId);
    return emp->nome;
  }
  return emp ? emp->nome : nomeAnterior;
}

std::string resolverNomeCondominio(Database& db, const std::string& condominioId, bool mudou,
                                   const std::string& nomeAnterior) {
  auto cond = findCondominio(db, condominioId);
  if (mudou) {
    if (!cond) throw NotFoundError("condomínio não encontrado: " + condominioId);
    return cond->nome;
  }
  return cond ? cond->nome : nomeAnterior;
}

void validateAquisicao(const Aquisicao& input) {
  if (trim(input.fornecedorId).empty()) throw std::invalid_argument("selecione o fornecedor");
  if (trim(input.descricao).empty()) throw std::invalid_argument("informe a descrição da compra");
  if (input.valor < 0) throw std::invalid_argument("o valor não pode ser negativo");
  if (!isValidData(input.dataCompra)) throw std::invalid_argument("data da compra inválida");
}

int contarPagamentosDaAquisicao(Database& db, const std::string& aquisicaoId) {
  auto st = db.prepare("SELECT COUNT(*) FROM compras_pagamentos WHERE aquisicao_id=?");
  st.bind(1, aquisicaoId);
  st.step();
  return static_cast<int>(st.columnDouble(0));
}

// -------------------------------------------------------------- Orçamentos

constexpr int64_t kMsPerDiaOrcamento = 86400000;

constexpr const char* kOrdemOrcamentoCols =
    "id, numero, condominio_id, condominio_nome, descricao, observacoes, status, "
    "proposta_recomendada_id, proposta_aprovada_id, data_solicitacao, data_envio_cliente, "
    "data_aprovacao, reaberto_em, created_at";

OrdemOrcamento rowToOrdemOrcamento(Statement& st) {
  OrdemOrcamento o;
  o.id = st.columnText(0);
  o.numero = static_cast<int>(st.columnDouble(1));
  o.condominioId = textOrEmpty(st, 2);
  o.condominioNome = st.columnText(3);
  o.descricao = st.columnText(4);
  o.observacoes = textOrEmpty(st, 5);
  o.status = st.columnText(6);
  o.propostaRecomendadaId = textOrEmpty(st, 7);
  o.propostaAprovadaId = textOrEmpty(st, 8);
  o.dataSolicitacao = textOrEmpty(st, 9);
  o.dataEnvioCliente = textOrEmpty(st, 10);
  o.dataAprovacao = textOrEmpty(st, 11);
  o.reabertoEm = textOrEmpty(st, 12);
  o.createdAt = st.columnText(13);
  return o;
}

constexpr const char* kPropostaOrcamentoCols =
    "id, ordem_id, empresa_id, empresa_nome, valor, anexo_path, anexo_tipo, email_enviado_em, "
    "recomendada, created_at";

PropostaOrcamento rowToPropostaOrcamento(Statement& st) {
  PropostaOrcamento p;
  p.id = st.columnText(0);
  p.ordemId = st.columnText(1);
  p.empresaId = st.columnText(2);
  p.empresaNome = st.columnText(3);
  p.valor = st.columnIsNull(4) ? -1 : st.columnDouble(4);
  p.anexoPath = textOrEmpty(st, 5);
  p.anexoTipo = textOrEmpty(st, 6);
  p.emailEnviadoEm = textOrEmpty(st, 7);
  p.recomendada = st.columnDouble(8) != 0;
  p.createdAt = st.columnText(9);
  return p;
}

// Sem resposta (valor NULL) por último; entre respondidas, do mais caro pro
// mais barato — exatamente o pedido ("o sistema sempre vai organizar as
// propostas do mais caro para o mais barato").
std::vector<PropostaOrcamento> carregarPropostas(Database& db, const std::string& ordemId) {
  std::vector<PropostaOrcamento> out;
  auto st = db.prepare(std::string("SELECT ") + kPropostaOrcamentoCols +
                       " FROM compras_propostas_orcamento WHERE ordem_id=? "
                       "ORDER BY (valor IS NULL) ASC, valor DESC, created_at ASC");
  st.bind(1, ordemId);
  while (st.step()) out.push_back(rowToPropostaOrcamento(st));
  return out;
}

std::optional<PropostaOrcamento> findPropostaOrcamento(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kPropostaOrcamentoCols +
                       " FROM compras_propostas_orcamento WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToPropostaOrcamento(st);
}

void carregarOrdemCompleta(Database& db, OrdemOrcamento& o) { o.propostas = carregarPropostas(db, o.id); }

// --------------------------------------------------------------- Pagamentos

constexpr const char* kPagamentoCols =
    "id, aquisicao_id, nota_fiscal, valor_total, data_emissao, observacoes, created_at";

Pagamento rowToPagamento(Statement& st) {
  Pagamento p;
  p.id = st.columnText(0);
  p.aquisicaoId = st.columnText(1);
  p.notaFiscal = st.columnText(2);
  p.valorTotal = st.columnDouble(3);
  p.dataEmissao = st.columnText(4);
  p.observacoes = textOrEmpty(st, 5);
  p.createdAt = st.columnText(6);
  return p;
}

constexpr const char* kParcelaCols = "id, numero, valor, vencimento, pago, data_pagamento, created_at";

Parcela rowToParcela(Statement& st) {
  Parcela p;
  p.id = st.columnText(0);
  p.numero = static_cast<int>(st.columnDouble(1));
  p.valor = st.columnDouble(2);
  p.vencimento = st.columnText(3);
  p.pago = st.columnDouble(4) != 0;
  p.dataPagamento = textOrEmpty(st, 5);
  p.createdAt = st.columnText(6);
  return p;
}

std::vector<Parcela> carregarParcelas(Database& db, const std::string& pagamentoId) {
  std::vector<Parcela> out;
  auto st = db.prepare(std::string("SELECT ") + kParcelaCols +
                       " FROM compras_parcelas WHERE pagamento_id=? ORDER BY numero");
  st.bind(1, pagamentoId);
  while (st.step()) out.push_back(rowToParcela(st));
  return out;
}

void gravarParcelas(Database& db, const std::string& pagamentoId, std::vector<Parcela>& parcelas,
                    const std::string& createdAt) {
  db.prepare("DELETE FROM compras_parcelas WHERE pagamento_id=?").bind(1, pagamentoId).step();
  int seq = 0;
  for (auto& p : parcelas) {
    ++seq;
    if (p.numero <= 0) p.numero = seq;
    if (trim(p.id).empty()) p.id = pagamentoId + "-p" + std::to_string(p.numero);
    auto st = db.prepare(
        "INSERT INTO compras_parcelas (id, pagamento_id, numero, valor, vencimento, pago, "
        "data_pagamento, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    st.bind(1, p.id).bind(2, pagamentoId).bind(3, static_cast<double>(p.numero));
    st.bind(4, p.valor).bind(5, p.vencimento).bind(6, p.pago ? 1.0 : 0.0);
    p.dataPagamento.empty() || !p.pago ? st.bindNull(7) : st.bind(7, p.dataPagamento);
    st.bind(8, p.createdAt.empty() ? createdAt : p.createdAt);
    st.step();
  }
}

void validateParcelas(const std::vector<Parcela>& parcelas) {
  if (parcelas.empty()) throw std::invalid_argument("informe ao menos uma parcela");
  for (const auto& p : parcelas) {
    if (p.valor < 0) throw std::invalid_argument("o valor da parcela não pode ser negativo");
    if (!isValidData(p.vencimento)) throw std::invalid_argument("vencimento de parcela inválido");
  }
}

void validatePagamento(const Pagamento& input) {
  if (trim(input.aquisicaoId).empty()) throw std::invalid_argument("selecione a aquisição");
  if (trim(input.notaFiscal).empty()) throw std::invalid_argument("informe o número da nota fiscal");
  if (input.valorTotal < 0) throw std::invalid_argument("o valor total não pode ser negativo");
  if (!isValidData(input.dataEmissao)) throw std::invalid_argument("data de emissão inválida");
  validateParcelas(input.parcelas);
}

}  // namespace

// ---------------------------------------------------------------- Aquisições

std::vector<Aquisicao> listAquisicoes(Database& db) {
  std::vector<Aquisicao> out;
  auto st = db.prepare(std::string("SELECT ") + kAquisicaoCols +
                       " FROM compras_aquisicoes ORDER BY data_compra DESC, created_at DESC");
  while (st.step()) out.push_back(rowToAquisicao(st));
  return out;
}

std::optional<Aquisicao> findAquisicao(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kAquisicaoCols + " FROM compras_aquisicoes WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToAquisicao(st);
}

Aquisicao createAquisicao(Database& db, const Aquisicao& input) {
  validateAquisicao(input);
  Aquisicao a = input;
  a.fornecedorNome = resolverNomeFornecedor(db, a.fornecedorId, true, "");

  auto st = db.prepare(
      "INSERT INTO compras_aquisicoes (id, fornecedor_id, fornecedor_nome, descricao, nota_fiscal, "
      "valor, data_compra, observacoes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, a.id).bind(2, a.fornecedorId).bind(3, a.fornecedorNome).bind(4, trim(a.descricao));
  trim(a.notaFiscal).empty() ? st.bindNull(5) : st.bind(5, trim(a.notaFiscal));
  st.bind(6, a.valor).bind(7, a.dataCompra).bind(8, a.observacoes).bind(9, a.createdAt);
  st.step();

  return *findAquisicao(db, a.id);
}

Aquisicao updateAquisicao(Database& db, const Aquisicao& input) {
  auto existing = findAquisicao(db, input.id);
  if (!existing) throw NotFoundError("aquisição não encontrada: " + input.id);
  validateAquisicao(input);

  Aquisicao a = input;
  a.fornecedorNome = resolverNomeFornecedor(db, a.fornecedorId, existing->fornecedorId != a.fornecedorId,
                                            existing->fornecedorNome);

  auto st = db.prepare(
      "UPDATE compras_aquisicoes SET fornecedor_id=?, fornecedor_nome=?, descricao=?, nota_fiscal=?, "
      "valor=?, data_compra=?, observacoes=? WHERE id=?");
  st.bind(1, a.fornecedorId).bind(2, a.fornecedorNome).bind(3, trim(a.descricao));
  trim(a.notaFiscal).empty() ? st.bindNull(4) : st.bind(4, trim(a.notaFiscal));
  st.bind(5, a.valor).bind(6, a.dataCompra).bind(7, a.observacoes).bind(8, a.id);
  st.step();

  return *findAquisicao(db, a.id);
}

void deleteAquisicao(Database& db, const std::string& id) {
  auto existing = findAquisicao(db, id);
  if (!existing) throw NotFoundError("aquisição não encontrada: " + id);
  if (contarPagamentosDaAquisicao(db, id) > 0) {
    throw std::invalid_argument("existem pagamentos lançados para esta aquisição — exclua-os primeiro");
  }
  db.prepare("DELETE FROM compras_aquisicoes WHERE id=?").bind(1, id).step();
}

// Mesmo critério de setProductImage/clearProductImage (inventory_engine): o
// arquivo já foi processado e gravado em disco pela ponte Rust antes desta
// chamada (ver bridge/src/attachments.rs); aqui só grava a referência.
Aquisicao setAquisicaoAnexo(Database& db, const std::string& id, const std::string& anexoPath,
                            const std::string& anexoTipo) {
  if (!findAquisicao(db, id)) throw NotFoundError("aquisição não encontrada: " + id);
  db.prepare("UPDATE compras_aquisicoes SET anexo_path=?, anexo_tipo=? WHERE id=?")
      .bind(1, anexoPath)
      .bind(2, anexoTipo)
      .bind(3, id)
      .step();
  return *findAquisicao(db, id);
}

// Remove a REFERÊNCIA do anexo — apagar o arquivo físico é responsabilidade
// do chamador (bridge/src/attachments.rs::delete_aquisicao_attachment),
// chamado ANTES disto pelo comando Tauri, mesma ordem de clearProductImage.
Aquisicao clearAquisicaoAnexo(Database& db, const std::string& id) {
  if (!findAquisicao(db, id)) throw NotFoundError("aquisição não encontrada: " + id);
  db.prepare("UPDATE compras_aquisicoes SET anexo_path=NULL, anexo_tipo=NULL WHERE id=?")
      .bind(1, id)
      .step();
  return *findAquisicao(db, id);
}

// -------------------------------------------------------------- Orçamentos

std::string ordemOrcamentoStatusEfetivo(const OrdemOrcamento& ordem, const std::string& hojeIso) {
  bool aguardandoResposta =
      ordem.status == ordem_orcamento_status::kSolicitado || ordem.status == ordem_orcamento_status::kEnviadoCliente;
  if (!aguardandoResposta || ordem.dataSolicitacao.empty()) return ordem.status;

  std::string base = ordem.reabertoEm.empty() ? ordem.dataSolicitacao : ordem.reabertoEm;
  if (ordem.reabertoEm.empty() && ordem.dataSolicitacao.empty()) return ordem.status;
  int64_t baseMs = time_utils::isoToEpochMs(base);
  int64_t hojeMs = time_utils::isoToEpochMs(hojeIso);
  int64_t diasDecorridos = (hojeMs - baseMs) / kMsPerDiaOrcamento;

  return diasDecorridos >= kDiasDeclinioAutomatico ? ordem_orcamento_status::kDeclinado : ordem.status;
}

std::vector<OrdemOrcamento> listOrdensOrcamento(Database& db) {
  std::vector<OrdemOrcamento> out;
  auto st = db.prepare(std::string("SELECT ") + kOrdemOrcamentoCols +
                       " FROM compras_ordens_orcamento ORDER BY numero DESC");
  while (st.step()) out.push_back(rowToOrdemOrcamento(st));
  for (auto& o : out) carregarOrdemCompleta(db, o);
  return out;
}

std::optional<OrdemOrcamento> findOrdemOrcamento(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kOrdemOrcamentoCols + " FROM compras_ordens_orcamento WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  OrdemOrcamento o = rowToOrdemOrcamento(st);
  carregarOrdemCompleta(db, o);
  return o;
}

OrdemOrcamento createOrdemOrcamento(Database& db, const OrdemOrcamento& input) {
  if (trim(input.id).empty()) throw std::invalid_argument("id da ordem ausente");
  if (trim(input.condominioId).empty()) throw std::invalid_argument("selecione o condomínio");
  if (trim(input.descricao).empty()) throw std::invalid_argument("informe a descrição do pedido");
  std::string condominioNome = resolverNomeCondominio(db, input.condominioId, true, "");

  auto st = db.prepare(
      "INSERT INTO compras_ordens_orcamento (id, condominio_id, condominio_nome, descricao, "
      "observacoes, status, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, input.condominioId).bind(3, condominioNome);
  st.bind(4, trim(input.descricao));
  trim(input.observacoes).empty() ? st.bindNull(5) : st.bind(5, trim(input.observacoes));
  st.bind(6, ordem_orcamento_status::kPendente).bind(7, input.createdAt);
  st.step();

  return *findOrdemOrcamento(db, input.id);
}

OrdemOrcamento updateOrdemOrcamentoInfo(Database& db, const std::string& id, const std::string& descricao,
                                        const std::string& observacoes) {
  if (!findOrdemOrcamento(db, id)) throw NotFoundError("ordem de orçamento não encontrada: " + id);
  if (trim(descricao).empty()) throw std::invalid_argument("informe a descrição do pedido");

  auto st = db.prepare("UPDATE compras_ordens_orcamento SET descricao=?, observacoes=? WHERE id=?");
  st.bind(1, trim(descricao));
  trim(observacoes).empty() ? st.bindNull(2) : st.bind(2, trim(observacoes));
  st.bind(3, id);
  st.step();

  return *findOrdemOrcamento(db, id);
}

void deleteOrdemOrcamento(Database& db, const std::string& id) {
  if (!findOrdemOrcamento(db, id)) throw NotFoundError("ordem de orçamento não encontrada: " + id);
  db.prepare("DELETE FROM compras_ordens_orcamento WHERE id=?").bind(1, id).step();
}

OrdemOrcamento solicitarOrcamentoParaEmpresas(Database& db, const std::string& ordemId,
                                              const std::vector<EmpresaSolicitada>& empresas,
                                              const std::string& nowIso) {
  auto ordem = findOrdemOrcamento(db, ordemId);
  if (!ordem) throw NotFoundError("ordem de orçamento não encontrada: " + ordemId);
  if (empresas.empty()) throw std::invalid_argument("selecione ao menos uma empresa");

  Transaction tx(db);
  for (const auto& e : empresas) {
    if (trim(e.empresaId).empty()) continue;
    // Já solicitada nesta ordem? Não duplica a linha da proposta, mas
    // REENVIA (mesmo critério de reenviarSolicitacaoProposta): o usuário
    // decide quantas vezes chamar a mesma empresa de novo, inclusive quem já
    // respondeu ou já recebeu antes — selecionar de novo é sempre um pedido
    // explícito de reenvio, nunca "fica esquecida porque já foi chamada".
    auto existente = std::find_if(ordem->propostas.begin(), ordem->propostas.end(),
                                  [&](const PropostaOrcamento& p) { return p.empresaId == e.empresaId; });
    if (existente != ordem->propostas.end()) {
      db.prepare("UPDATE compras_propostas_orcamento SET email_enviado_em=? WHERE id=?")
          .bind(1, nowIso)
          .bind(2, existente->id)
          .step();
      continue;
    }
    auto empresa = findEmpresa(db, e.empresaId);
    if (!empresa) throw NotFoundError("empresa não encontrada: " + e.empresaId);

    auto st = db.prepare(
        "INSERT INTO compras_propostas_orcamento (id, ordem_id, empresa_id, empresa_nome, "
        "email_enviado_em, created_at) VALUES (?, ?, ?, ?, ?, ?)");
    st.bind(1, ordemId + "-" + e.empresaId).bind(2, ordemId).bind(3, e.empresaId).bind(4, empresa->nome);
    st.bind(5, nowIso).bind(6, nowIso);
    st.step();
  }

  // dataSolicitacao só na primeira vez — chamadas seguintes (mais empresas
  // depois) não reiniciam a janela dos 25 dias.
  auto st = db.prepare(
      "UPDATE compras_ordens_orcamento SET status=?, "
      "data_solicitacao=COALESCE(data_solicitacao, ?) WHERE id=?");
  st.bind(1, ordem_orcamento_status::kSolicitado).bind(2, nowIso).bind(3, ordemId);
  st.step();
  tx.commit();

  return *findOrdemOrcamento(db, ordemId);
}

PropostaOrcamento setPropostaValor(Database& db, const std::string& propostaId, double valor) {
  if (!findPropostaOrcamento(db, propostaId)) throw NotFoundError("proposta não encontrada: " + propostaId);
  if (valor < 0) throw std::invalid_argument("o valor não pode ser negativo");
  db.prepare("UPDATE compras_propostas_orcamento SET valor=? WHERE id=?").bind(1, valor).bind(2, propostaId).step();
  return *findPropostaOrcamento(db, propostaId);
}

PropostaOrcamento setPropostaAnexo(Database& db, const std::string& propostaId, const std::string& anexoPath,
                                   const std::string& anexoTipo) {
  if (!findPropostaOrcamento(db, propostaId)) throw NotFoundError("proposta não encontrada: " + propostaId);
  db.prepare("UPDATE compras_propostas_orcamento SET anexo_path=?, anexo_tipo=? WHERE id=?")
      .bind(1, anexoPath)
      .bind(2, anexoTipo)
      .bind(3, propostaId)
      .step();
  return *findPropostaOrcamento(db, propostaId);
}

PropostaOrcamento clearPropostaAnexo(Database& db, const std::string& propostaId) {
  if (!findPropostaOrcamento(db, propostaId)) throw NotFoundError("proposta não encontrada: " + propostaId);
  db.prepare("UPDATE compras_propostas_orcamento SET anexo_path=NULL, anexo_tipo=NULL WHERE id=?")
      .bind(1, propostaId)
      .step();
  return *findPropostaOrcamento(db, propostaId);
}

PropostaOrcamento reenviarSolicitacaoProposta(Database& db, const std::string& propostaId,
                                              const std::string& nowIso) {
  if (!findPropostaOrcamento(db, propostaId)) throw NotFoundError("proposta não encontrada: " + propostaId);
  db.prepare("UPDATE compras_propostas_orcamento SET email_enviado_em=? WHERE id=?")
      .bind(1, nowIso)
      .bind(2, propostaId)
      .step();
  return *findPropostaOrcamento(db, propostaId);
}

OrdemOrcamento marcarPropostaRecomendada(Database& db, const std::string& ordemId, const std::string& propostaId) {
  auto ordem = findOrdemOrcamento(db, ordemId);
  if (!ordem) throw NotFoundError("ordem de orçamento não encontrada: " + ordemId);
  auto proposta = findPropostaOrcamento(db, propostaId);
  if (!proposta || proposta->ordemId != ordemId) {
    throw NotFoundError("proposta não encontrada nesta ordem: " + propostaId);
  }

  Transaction tx(db);
  db.prepare("UPDATE compras_propostas_orcamento SET recomendada=0 WHERE ordem_id=?").bind(1, ordemId).step();
  db.prepare("UPDATE compras_propostas_orcamento SET recomendada=1 WHERE id=?").bind(1, propostaId).step();
  db.prepare("UPDATE compras_ordens_orcamento SET proposta_recomendada_id=? WHERE id=?")
      .bind(1, propostaId)
      .bind(2, ordemId)
      .step();
  tx.commit();

  return *findOrdemOrcamento(db, ordemId);
}

OrdemOrcamento desmarcarPropostaRecomendada(Database& db, const std::string& ordemId) {
  auto ordem = findOrdemOrcamento(db, ordemId);
  if (!ordem) throw NotFoundError("ordem de orçamento não encontrada: " + ordemId);

  Transaction tx(db);
  db.prepare("UPDATE compras_propostas_orcamento SET recomendada=0 WHERE ordem_id=?").bind(1, ordemId).step();
  db.prepare("UPDATE compras_ordens_orcamento SET proposta_recomendada_id=NULL WHERE id=?")
      .bind(1, ordemId)
      .step();
  tx.commit();

  return *findOrdemOrcamento(db, ordemId);
}

OrdemOrcamento enviarOrcamentoParaCliente(Database& db, const std::string& ordemId, const std::string& nowIso) {
  auto ordem = findOrdemOrcamento(db, ordemId);
  if (!ordem) throw NotFoundError("ordem de orçamento não encontrada: " + ordemId);
  if (ordem->propostas.empty()) throw std::invalid_argument("solicite ao menos uma empresa antes de enviar");

  auto st = db.prepare("UPDATE compras_ordens_orcamento SET status=?, data_envio_cliente=? WHERE id=?");
  st.bind(1, ordem_orcamento_status::kEnviadoCliente).bind(2, nowIso).bind(3, ordemId);
  st.step();

  return *findOrdemOrcamento(db, ordemId);
}

OrdemOrcamento aprovarPropostaOrcamento(Database& db, const std::string& ordemId, const std::string& propostaId,
                                        const std::string& nowIso) {
  auto ordem = findOrdemOrcamento(db, ordemId);
  if (!ordem) throw NotFoundError("ordem de orçamento não encontrada: " + ordemId);
  auto proposta = findPropostaOrcamento(db, propostaId);
  if (!proposta || proposta->ordemId != ordemId) {
    throw NotFoundError("proposta não encontrada nesta ordem: " + propostaId);
  }

  auto st = db.prepare(
      "UPDATE compras_ordens_orcamento SET status=?, proposta_aprovada_id=?, data_aprovacao=? WHERE id=?");
  st.bind(1, ordem_orcamento_status::kAprovado).bind(2, propostaId).bind(3, nowIso).bind(4, ordemId);
  st.step();

  return *findOrdemOrcamento(db, ordemId);
}

OrdemOrcamento reativarOrdemOrcamento(Database& db, const std::string& ordemId, const std::string& nowIso) {
  if (!findOrdemOrcamento(db, ordemId)) throw NotFoundError("ordem de orçamento não encontrada: " + ordemId);
  db.prepare("UPDATE compras_ordens_orcamento SET reaberto_em=? WHERE id=?").bind(1, nowIso).bind(2, ordemId).step();
  return *findOrdemOrcamento(db, ordemId);
}

// --------------------------------------------------------------- Pagamentos

std::vector<Pagamento> listPagamentos(Database& db) {
  std::vector<Pagamento> out;
  auto st = db.prepare(std::string("SELECT ") + kPagamentoCols +
                       " FROM compras_pagamentos ORDER BY data_emissao DESC, created_at DESC");
  while (st.step()) out.push_back(rowToPagamento(st));
  for (auto& p : out) p.parcelas = carregarParcelas(db, p.id);
  return out;
}

std::optional<Pagamento> findPagamento(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kPagamentoCols + " FROM compras_pagamentos WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  Pagamento p = rowToPagamento(st);
  p.parcelas = carregarParcelas(db, p.id);
  return p;
}

Pagamento createPagamento(Database& db, const Pagamento& input) {
  validatePagamento(input);
  if (!findAquisicao(db, input.aquisicaoId)) {
    throw NotFoundError("aquisição não encontrada: " + input.aquisicaoId);
  }

  Transaction tx(db);
  Pagamento p = input;
  auto st = db.prepare(
      "INSERT INTO compras_pagamentos (id, aquisicao_id, nota_fiscal, valor_total, data_emissao, "
      "observacoes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, p.id).bind(2, p.aquisicaoId).bind(3, trim(p.notaFiscal)).bind(4, p.valorTotal);
  st.bind(5, p.dataEmissao).bind(6, p.observacoes).bind(7, p.createdAt);
  st.step();

  // Todas nascem em aberto: um lançamento novo nunca chega já pago —
  // marcarParcela é o único caminho para virar pago, com a data de baixa.
  for (auto& parc : p.parcelas) {
    parc.pago = false;
    parc.dataPagamento = "";
  }
  gravarParcelas(db, p.id, p.parcelas, p.createdAt);
  tx.commit();

  return *findPagamento(db, p.id);
}

Pagamento updatePagamento(Database& db, const Pagamento& input) {
  auto existing = findPagamento(db, input.id);
  if (!existing) throw NotFoundError("pagamento não encontrado: " + input.id);
  validatePagamento(input);

  Transaction tx(db);
  auto st = db.prepare(
      "UPDATE compras_pagamentos SET nota_fiscal=?, valor_total=?, data_emissao=?, observacoes=? "
      "WHERE id=?");
  st.bind(1, trim(input.notaFiscal)).bind(2, input.valorTotal).bind(3, input.dataEmissao);
  st.bind(4, input.observacoes).bind(5, input.id);
  st.step();

  // Preserva pago/dataPagamento de quem já existia com o MESMO id de parcela
  // — regravar não pode reabrir em silêncio uma parcela já baixada. Uma
  // parcela nova (id que ainda não existia) sempre nasce em aberto.
  std::vector<Parcela> parcelas = input.parcelas;
  for (auto& p : parcelas) {
    auto anterior = std::find_if(existing->parcelas.begin(), existing->parcelas.end(),
                                 [&](const Parcela& x) { return !p.id.empty() && x.id == p.id; });
    if (anterior != existing->parcelas.end()) {
      p.pago = anterior->pago;
      p.dataPagamento = anterior->dataPagamento;
      p.createdAt = anterior->createdAt;
    } else {
      p.id.clear();  // força gravarParcelas a gerar um id novo
      p.pago = false;
      p.dataPagamento = "";
    }
  }
  gravarParcelas(db, input.id, parcelas, existing->createdAt);
  tx.commit();

  return *findPagamento(db, input.id);
}

void deletePagamento(Database& db, const std::string& id) {
  auto existing = findPagamento(db, id);
  if (!existing) throw NotFoundError("pagamento não encontrado: " + id);
  db.prepare("DELETE FROM compras_pagamentos WHERE id=?").bind(1, id).step();
}

Pagamento marcarParcela(Database& db, const std::string& pagamentoId, const std::string& parcelaId,
                        bool pago, const std::string& dataPagamento) {
  auto pagamento = findPagamento(db, pagamentoId);
  if (!pagamento) throw NotFoundError("pagamento não encontrado: " + pagamentoId);
  bool existe = std::any_of(pagamento->parcelas.begin(), pagamento->parcelas.end(),
                            [&](const Parcela& p) { return p.id == parcelaId; });
  if (!existe) throw NotFoundError("parcela não encontrada: " + parcelaId);
  if (pago && !isValidData(dataPagamento)) {
    throw std::invalid_argument("informe a data do pagamento");
  }

  auto st = db.prepare("UPDATE compras_parcelas SET pago=?, data_pagamento=? WHERE id=?");
  st.bind(1, pago ? 1.0 : 0.0);
  (pago && !dataPagamento.empty()) ? st.bind(2, dataPagamento) : st.bindNull(2);
  st.bind(3, parcelaId);
  st.step();

  return *findPagamento(db, pagamentoId);
}

}  // namespace estoque
