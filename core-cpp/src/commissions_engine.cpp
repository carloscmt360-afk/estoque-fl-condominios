#include "estoque/commissions_engine.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

#include "estoque/companies_engine.hpp"
#include "estoque/dates_engine.hpp"
#include "estoque/managers_engine.hpp"

namespace estoque {

namespace {

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string textOrEmpty(Statement& st, int idx) { return st.columnIsNull(idx) ? "" : st.columnText(idx); }

// "YYYY-MM": exatamente 7 caracteres, dígitos nas posições certas, mês 01-12.
// Sem <regex> — mesmo critério do resto do core-cpp (canonCnpj, upperAscii).
bool isValidMesReferencia(const std::string& s) {
  if (s.size() != 7 || s[4] != '-') return false;
  for (int i : {0, 1, 2, 3, 5, 6}) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  int mes = (s[5] - '0') * 10 + (s[6] - '0');
  return mes >= 1 && mes <= 12;
}

constexpr const char* kServicoCols =
    "numero, id, codigo, condominio_id, condominio_nome, gerente_id, gerente_nome, "
    "parceiro_id, parceiro_nome, venda, porcentagem, data_referencia, fechamento_id, "
    "observacoes, created_at, pago, data_pagamento";

Servico rowToServico(Statement& st) {
  Servico s;
  s.numero = static_cast<int>(st.columnDouble(0));
  s.id = st.columnText(1);
  s.codigo = textOrEmpty(st, 2);
  s.condominioId = textOrEmpty(st, 3);
  s.condominioNome = st.columnText(4);
  s.gerenteId = textOrEmpty(st, 5);
  s.gerenteNome = textOrEmpty(st, 6);
  s.parceiroId = textOrEmpty(st, 7);
  s.parceiroNome = textOrEmpty(st, 8);
  s.venda = st.columnDouble(9);
  s.porcentagem = st.columnDouble(10);
  s.dataReferencia = st.columnText(11);
  s.fechamentoId = textOrEmpty(st, 12);
  s.observacoes = textOrEmpty(st, 13);
  s.createdAt = st.columnText(14);
  s.pago = st.columnDouble(15) != 0;
  s.dataPagamento = textOrEmpty(st, 16);
  return s;
}

constexpr const char* kFechamentoCols =
    "id, mes_referencia, quantidade_servicos, total_venda, total_comissao, observacoes, "
    "fechado_em, reaberto_em, created_at";

Fechamento rowToFechamento(Statement& st) {
  Fechamento f;
  f.id = st.columnText(0);
  f.mesReferencia = st.columnText(1);
  f.quantidadeServicos = static_cast<int>(st.columnDouble(2));
  f.totalVenda = st.columnDouble(3);
  f.totalComissao = st.columnDouble(4);
  f.observacoes = textOrEmpty(st, 5);
  f.fechadoEm = st.columnText(6);
  f.reabertoEm = textOrEmpty(st, 7);
  f.createdAt = st.columnText(8);
  return f;
}

// Preenche condominioNome/gerenteNome/parceiroNome a partir dos ids (o nome
// digitado pelo usuário nunca é usado — é sempre resolvido do cadastro, para
// não divergir do nome real na hora de gravar).
//
// `anterior` (não nulo só em updateServico) é o que já está gravado. Quando
// um id NÃO mudou em relação a ele, o cadastro de origem pode ter sido
// excluído nesse meio-tempo (condomínio saiu da administradora, por
// exemplo) sem que isso trave a edição de outro campo do serviço (venda,
// porcentagem...): o nome já gravado é preservado em vez de exigir que o
// cadastro ainda exista. Um id NOVO (create, ou troca explícita numa edição)
// sempre precisa existir — aí sim é essa a intenção do usuário.
void resolverNomes(Database& db, Servico& s, const Servico* anterior) {
  bool condominioMudou = !anterior || anterior->condominioId != s.condominioId;
  auto cond = findCondominio(db, s.condominioId);
  if (condominioMudou) {
    if (!cond) throw NotFoundError("condomínio não encontrado: " + s.condominioId);
    s.condominioNome = cond->nome;
  } else {
    s.condominioNome = cond ? cond->nome : anterior->condominioNome;
  }

  bool gerenteMudou = !anterior || anterior->gerenteId != s.gerenteId;
  if (s.gerenteId.empty()) {
    s.gerenteNome = "";
  } else {
    auto ger = findGerente(db, s.gerenteId);
    if (gerenteMudou) {
      if (!ger) throw NotFoundError("gerente não encontrado: " + s.gerenteId);
      s.gerenteNome = ger->nome;
    } else {
      s.gerenteNome = ger ? ger->nome : anterior->gerenteNome;
    }
  }

  bool parceiroMudou = !anterior || anterior->parceiroId != s.parceiroId;
  if (s.parceiroId.empty()) {
    s.parceiroNome = "";
  } else {
    auto par = findEmpresa(db, s.parceiroId);
    if (parceiroMudou) {
      if (!par) throw NotFoundError("parceiro não encontrado: " + s.parceiroId);
      if (!par->parceira) {
        throw std::invalid_argument("\"" + par->nome + "\" não está marcada como parceira");
      }
      s.parceiroNome = par->nome;
    } else {
      s.parceiroNome = par ? par->nome : anterior->parceiroNome;
    }
  }
}

void validateServico(const Servico& input) {
  if (trim(input.condominioId).empty()) throw std::invalid_argument("selecione o condomínio");
  if (input.venda < 0) throw std::invalid_argument("a venda não pode ser negativa");
  if (input.porcentagem < 0 || input.porcentagem > 100) {
    throw std::invalid_argument("a porcentagem precisa estar entre 0 e 100");
  }
  if (!isValidMesReferencia(input.dataReferencia)) {
    throw std::invalid_argument("data de referência inválida (use mês/ano)");
  }
  if (input.pago && trim(input.dataPagamento).empty()) {
    throw std::invalid_argument("informe a data de pagamento");
  }
}

}  // namespace

double comissaoDe(const Servico& s) { return s.venda * s.porcentagem / 100.0; }

std::vector<Servico> listServicos(Database& db) {
  std::vector<Servico> out;
  auto st = db.prepare(std::string("SELECT ") + kServicoCols + " FROM sos_servicos ORDER BY numero DESC");
  while (st.step()) out.push_back(rowToServico(st));
  return out;
}

std::optional<Servico> findServico(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kServicoCols + " FROM sos_servicos WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToServico(st);
}

Servico createServico(Database& db, const Servico& input) {
  Servico s = input;
  if (!s.pago) s.dataPagamento = "";  // data só faz sentido junto de pago=true
  validateServico(s);
  resolverNomes(db, s, nullptr);

  auto st = db.prepare(
      "INSERT INTO sos_servicos (id, codigo, condominio_id, condominio_nome, gerente_id, "
      "gerente_nome, parceiro_id, parceiro_nome, venda, porcentagem, data_referencia, "
      "observacoes, created_at, pago, data_pagamento) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, s.id).bind(2, trim(s.codigo)).bind(3, s.condominioId).bind(4, s.condominioNome);
  s.gerenteId.empty() ? st.bindNull(5) : st.bind(5, s.gerenteId);
  s.gerenteNome.empty() ? st.bindNull(6) : st.bind(6, s.gerenteNome);
  s.parceiroId.empty() ? st.bindNull(7) : st.bind(7, s.parceiroId);
  s.parceiroNome.empty() ? st.bindNull(8) : st.bind(8, s.parceiroNome);
  st.bind(9, s.venda).bind(10, s.porcentagem).bind(11, s.dataReferencia);
  st.bind(12, s.observacoes).bind(13, s.createdAt).bind(14, s.pago ? 1 : 0);
  s.dataPagamento.empty() ? st.bindNull(15) : st.bind(15, s.dataPagamento);
  st.step();

  return *findServico(db, s.id);
}

Servico updateServico(Database& db, const Servico& input) {
  auto existing = findServico(db, input.id);
  if (!existing) throw NotFoundError("serviço não encontrado: " + input.id);
  if (!existing->fechamentoId.empty()) {
    throw std::invalid_argument("este serviço já está num mês fechado — reabra o fechamento para editar");
  }

  Servico s = input;
  if (!s.pago) s.dataPagamento = "";  // data só faz sentido junto de pago=true
  validateServico(s);
  resolverNomes(db, s, &*existing);

  auto st = db.prepare(
      "UPDATE sos_servicos SET codigo=?, condominio_id=?, condominio_nome=?, gerente_id=?, "
      "gerente_nome=?, parceiro_id=?, parceiro_nome=?, venda=?, porcentagem=?, data_referencia=?, "
      "observacoes=?, pago=?, data_pagamento=? WHERE id=?");
  st.bind(1, trim(s.codigo)).bind(2, s.condominioId).bind(3, s.condominioNome);
  s.gerenteId.empty() ? st.bindNull(4) : st.bind(4, s.gerenteId);
  s.gerenteNome.empty() ? st.bindNull(5) : st.bind(5, s.gerenteNome);
  s.parceiroId.empty() ? st.bindNull(6) : st.bind(6, s.parceiroId);
  s.parceiroNome.empty() ? st.bindNull(7) : st.bind(7, s.parceiroNome);
  st.bind(8, s.venda).bind(9, s.porcentagem).bind(10, s.dataReferencia);
  st.bind(11, s.observacoes).bind(12, s.pago ? 1 : 0);
  s.dataPagamento.empty() ? st.bindNull(13) : st.bind(13, s.dataPagamento);
  st.bind(14, s.id);
  st.step();

  return *findServico(db, s.id);
}

void deleteServico(Database& db, const std::string& id) {
  auto existing = findServico(db, id);
  if (!existing) throw NotFoundError("serviço não encontrado: " + id);
  if (!existing->fechamentoId.empty()) {
    throw std::invalid_argument("este serviço já está num mês fechado — reabra o fechamento para excluir");
  }
  db.prepare("DELETE FROM sos_servicos WHERE id=?").bind(1, id).step();
}

std::vector<Fechamento> listFechamentos(Database& db) {
  std::vector<Fechamento> out;
  auto st = db.prepare(std::string("SELECT ") + kFechamentoCols +
                       " FROM sos_fechamentos ORDER BY mes_referencia DESC");
  while (st.step()) out.push_back(rowToFechamento(st));
  return out;
}

Fechamento fecharMes(Database& db, const std::string& id, const std::string& mesReferencia,
                     const std::string& observacoes, const std::string& fechadoEm,
                     const std::string& createdAt) {
  if (!isValidMesReferencia(mesReferencia)) {
    throw std::invalid_argument("data de referência inválida (use mês/ano)");
  }

  Transaction tx(db);

  double totalVenda = 0;
  double totalComissao = 0;
  int quantidade = 0;
  {
    auto st = db.prepare(
        "SELECT venda, porcentagem FROM sos_servicos "
        "WHERE data_referencia=? AND fechamento_id IS NULL AND pago=1");
    st.bind(1, mesReferencia);
    while (st.step()) {
      double venda = st.columnDouble(0);
      double porcentagem = st.columnDouble(1);
      totalVenda += venda;
      totalComissao += venda * porcentagem / 100.0;
      ++quantidade;
    }
  }
  if (quantidade == 0) {
    throw std::invalid_argument("nenhum serviço aberto E pago em " + mesReferencia + " para fechar");
  }

  auto ins = db.prepare(
      "INSERT INTO sos_fechamentos (id, mes_referencia, quantidade_servicos, total_venda, "
      "total_comissao, observacoes, fechado_em, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
  ins.bind(1, id).bind(2, mesReferencia).bind(3, static_cast<double>(quantidade));
  ins.bind(4, totalVenda).bind(5, totalComissao).bind(6, observacoes);
  ins.bind(7, fechadoEm).bind(8, createdAt);
  ins.step();

  auto upd = db.prepare(
      "UPDATE sos_servicos SET fechamento_id=? "
      "WHERE data_referencia=? AND fechamento_id IS NULL AND pago=1");
  upd.bind(1, id).bind(2, mesReferencia);
  upd.step();

  tx.commit();

  auto st = db.prepare(std::string("SELECT ") + kFechamentoCols + " FROM sos_fechamentos WHERE id=?");
  st.bind(1, id);
  st.step();
  return rowToFechamento(st);
}

void reabrirFechamento(Database& db, const std::string& id, const std::string& reabertoEm) {
  {
    auto st = db.prepare("SELECT 1 FROM sos_fechamentos WHERE id=?");
    st.bind(1, id);
    if (!st.step()) throw NotFoundError("fechamento não encontrado: " + id);
  }
  Transaction tx(db);
  db.prepare("UPDATE sos_servicos SET fechamento_id=NULL WHERE fechamento_id=?").bind(1, id).step();
  // O REGISTRO fica — vira uma entrada permanente do histórico, só marcada
  // como reaberta. `fecharMes` desse mesmo mês, mais adiante, cria um novo
  // Fechamento (o mês pode ser fechado mais de uma vez ao longo do tempo).
  auto upd = db.prepare("UPDATE sos_fechamentos SET reaberto_em=? WHERE id=?");
  upd.bind(1, reabertoEm).bind(2, id);
  upd.step();
  tx.commit();
}

// --------------------------------------------------------- Delta Síndicos
//
// listDeltaSindicos de verdade mora mais abaixo (depois de configPct, que
// ela usa) — aqui só o que não depende disso.

double comissaoDeltaDe(const DeltaSindico& d) { return d.venda * d.porcentagem / 100.0; }

// -------------------------------------------- Dashboard de fechamento

namespace {
// Lê uma porcentagem de sos_config; string vazia ou não numérica cai no
// padrão de fábrica (sos_config_padrao) — mesmo critério de getConfig, só
// que já convertido pra double, porque toda porcentagem do módulo é
// numérica.
double configPct(Database& db, const char* key, double padrao) {
  std::string v = getConfig(db, key, "");
  if (v.empty()) return padrao;
  try {
    return std::stod(v);
  } catch (...) {
    return padrao;
  }
}
}  // namespace

// Um item por serviço PAGO cujo condomínio tem deltaSindica=true — nunca lê
// nem grava em sos_delta_sindicos (tabela antiga do lançamento manual,
// morta). `sindico` e `porcentagem` são resolvidos NA HORA (cadastro do
// condomínio e sos_config, respectivamente), nunca denormalizados: não há
// "histórico congelado" aqui porque não há fechamento próprio — a planilha
// é sempre o retrato de agora.
std::vector<DeltaSindico> listDeltaSindicos(Database& db) {
  double pctDelta = configPct(db, sos_config::kDeltaSindica, sos_config_padrao::kDeltaSindica);

  std::map<std::string, std::string> sindicoDoCondominioDelta;
  for (const auto& c : listCondominios(db)) {
    if (c.deltaSindica) sindicoDoCondominioDelta[c.id] = c.sindico;
  }

  std::vector<DeltaSindico> out;
  if (sindicoDoCondominioDelta.empty()) return out;

  for (const auto& s : listServicos(db)) {
    if (!s.pago) continue;
    auto it = sindicoDoCondominioDelta.find(s.condominioId);
    if (it == sindicoDoCondominioDelta.end()) continue;

    DeltaSindico d;
    d.id = s.id;
    d.numero = s.numero;
    d.condominioId = s.condominioId;
    d.condominioNome = s.condominioNome;
    d.gerenteId = s.gerenteId;
    d.gerenteNome = s.gerenteNome;
    d.sindico = it->second;
    d.venda = s.venda;
    d.porcentagem = pctDelta;
    d.dataReferencia = s.dataReferencia;
    d.observacoes = s.observacoes;
    d.createdAt = s.createdAt;
    out.push_back(std::move(d));
  }
  return out;  // listServicos(db) já vem ordenado por numero DESC
}

DashboardFechamento montarDashboard(Database& db, const DashboardEntrada& entrada) {
  if (!isValidMesReferencia(entrada.mesReferencia)) {
    throw std::invalid_argument("data de referência inválida (use mês/ano)");
  }

  // "Sempre o controle de porcentagem deve estar no Botão Configurações":
  // todo percentual usado aqui (salvo o override manual de eficácia por
  // gerente, que é por natureza mensal) vem de sos_config, nunca de um
  // número fixo no código nem de um campo do formulário de fechamento.
  double rateioFl = configPct(db, sos_config::kRateioFl, sos_config_padrao::kRateioFl);
  double rateioGerentesPadrao = configPct(db, sos_config::kRateioGerentes, sos_config_padrao::kRateioGerentes);
  double rateioSuprimentos = configPct(db, sos_config::kRateioSuprimentos, sos_config_padrao::kRateioSuprimentos);
  double suprimentosEncarregado =
      configPct(db, sos_config::kSuprimentosEncarregado, sos_config_padrao::kSuprimentosEncarregado);
  double suprimentosAssistente =
      configPct(db, sos_config::kSuprimentosAssistente, sos_config_padrao::kSuprimentosAssistente);
  double metaPorCondominio = configPct(db, sos_config::kMetaPorCondominio, sos_config_padrao::kMetaPorCondominio);

  DashboardFechamento out;
  out.mesReferencia = entrada.mesReferencia;
  out.percentualComissao = rateioGerentesPadrao;
  out.percentualDistribuido = entrada.percentualDistribuido;
  out.retido = entrada.retido;
  out.distribuicaoCompras = entrada.distribuicaoCompras;
  out.distribuicaoDelta = entrada.distribuicaoDelta;
  out.observacoes = entrada.observacoes;

  // ---- serviços do mês: alimentam arrecadado, gerentes e empresas ----
  // Só entra quem já foi PAGO — venda lançada sem pagamento confirmado não
  // gera comissão pra ninguém (nem FL, nem gerente, nem parceiro).
  std::vector<Servico> doMes;
  for (const auto& s : listServicos(db)) {
    if (s.dataReferencia == entrada.mesReferencia && s.pago) doMes.push_back(s);
  }

  // Arrecadado é a COMISSÃO que a FL recebe sobre a venda (venda ×
  // porcentagem do serviço) — não o valor bruto vendido. É esse total que se
  // reparte entre FL/Gerentes/Suprimentos logo abaixo, nunca a venda em si.
  for (const auto& s : doMes) out.arrecadado += comissaoDe(s);
  // FL e o "liberado para comissão" (a fatia dos Gerentes) são sempre o
  // rateio de Configurações sobre o arrecadado do mês — nunca um campo que
  // o formulário de fechamento preenche à mão (mesmo critério da divisão de
  // Suprimentos abaixo).
  out.flLucro = out.arrecadado * rateioFl / 100.0;
  out.liberadoParaComissao = out.arrecadado * rateioGerentesPadrao / 100.0;

  // Divisão da fatia de Suprimentos (Encarregado/Assistente) — só some o
  // padrão de Configurações quando a tela de Fechamento não mandou nada
  // (mesmo critério de "ausente na entrada usa o padrão" dos gerentes).
  if (out.distribuicaoCompras.empty()) {
    double poolSuprimentos = out.arrecadado * rateioSuprimentos / 100.0;
    out.distribuicaoCompras = {
        {"Encarregado", poolSuprimentos * suprimentosEncarregado / 100.0},
        {"Assistente", poolSuprimentos * suprimentosAssistente / 100.0},
    };
  }

  // ---- Delta Síndicos do mês: alimenta o painel e os DESCONTOS ----
  for (const auto& d : listDeltaSindicos(db)) {
    if (d.dataReferencia == entrada.mesReferencia) out.deltaSindicos.push_back(d);
  }

  // Condomínio que já foi cliente mas saiu (ativo=false) pode continuar
  // ligado à carteira do gerente — histórico de serviços/comissão não pode
  // ficar sem dono só porque o cliente saiu —, mas não conta como número de
  // carteira nem entra na meta dele (ver Condominio::ativo em
  // dates_engine.hpp). Id que não existe mais no cadastro (excluído de
  // verdade) conta como se fosse ativo — não há como saber, e é o critério
  // mais conservador (não reduz a meta de quem não mexeu em nada).
  std::map<std::string, bool> condominioAtivo;
  for (const auto& c : listCondominios(db)) condominioAtivo[c.id] = c.ativo;

  // ---- painel de gerentes ----
  // TODOS os gerentes cadastrados entram, mesmo sem venda no mês: a planilha
  // lista a equipe inteira (ULISSES aparece com recebidos zerado), e uma
  // linha faltando seria lida como "esqueceram de lançar".
  for (const auto& g : listGerentes(db)) {
    DashboardGerenteLinha linha;
    linha.gerenteId = g.id;
    linha.gerenteNome = g.nome;

    // Produção bruta (venda) dos serviços dos condomínios NA CARTEIRA do
    // gerente (portfólio), não dos serviços que trazem o gerenteId dele no
    // registro — os dois divergem sempre que quem lançou o serviço marcou
    // outro gerente responsável pela execução, mas o condomínio pertence à
    // carteira deste. Só entram serviços com porcentagem > 0. Não é mais
    // exibida como coluna própria (sumiu da tela) — sobrevive só como base
    // da fórmula de eficácia (produção contra a meta), que continua em
    // venda bruta, não em comissão.
    //
    // Recebido é a fatia do ARRECADADO (a comissão que a FL já cobrou em
    // cada serviço, não a venda bruta) que veio da carteira deste gerente —
    // é o que faz a soma de "Recebido" de todos os gerentes bater com o
    // "Arrecadado" do topo da tela (esclarecido pelo usuário: produzido em
    // venda bruta não tinha nenhuma relação com o valor realmente
    // arrecadado no mês).
    double producaoBruta = 0;
    for (const auto& s : doMes) {
      bool naCarteira = std::find(g.condominioIds.begin(), g.condominioIds.end(), s.condominioId) !=
                        g.condominioIds.end();
      if (naCarteira && s.porcentagem > 0) {
        producaoBruta += s.venda;
        linha.recebido += comissaoDe(s);
      }
    }
    for (const auto& d : out.deltaSindicos) {
      if (d.gerenteId == g.id) linha.descontos += comissaoDeltaDe(d);
    }

    // porcentagem/eficácia vêm do que a tela de Fechamento mandar (o
    // usuário já viu o valor calculado e decidiu manter ou sobrescrever —
    // caso do diretor que não bate meta mas fica em 100% por decisão da
    // empresa). Gerente ausente da entrada (mês sendo gerado do zero, antes
    // de qualquer edição manual) recebe os PADRÕES: porcentagem de
    // Configurações, eficácia calculada pela fórmula da meta.
    auto it = std::find_if(entrada.gerentes.begin(), entrada.gerentes.end(),
                           [&](const DashboardGerenteEntrada& e) { return e.gerenteId == g.id; });
    // Carteira (para a fórmula de eficácia) é digitada à mão todo mês — não
    // necessariamente igual ao tamanho da carteira REAL cadastrada (o
    // usuário pode ter condomínios entrando/saindo no meio do mês que ainda
    // não bateram no cadastro). Sem valor informado, sugere o tamanho real
    // como ponto de partida — só contando os condomínios ATIVOS (ex-cliente
    // ligado à carteira por histórico não conta número nem meta).
    int carteiraAtiva = 0;
    for (const auto& condId : g.condominioIds) {
      auto itAtivo = condominioAtivo.find(condId);
      if (itAtivo == condominioAtivo.end() || itAtivo->second) carteiraAtiva++;
    }
    linha.carteira = (it != entrada.gerentes.end() && it->carteira > 0)
                          ? it->carteira
                          : carteiraAtiva;
    // Meta = meta por condomínio (Configurações) × carteira.
    linha.meta = metaPorCondominio * linha.carteira;
    if (it != entrada.gerentes.end()) {
      linha.porcentagem = it->porcentagem > 0 ? it->porcentagem : rateioGerentesPadrao;
      linha.eficacia = it->eficacia;
    } else {
      linha.porcentagem = rateioGerentesPadrao;
      linha.eficacia = eficaciaDe(producaoBruta, linha.carteira, metaPorCondominio);
    }

    // Recebido × % do gerente — o bruto antes de aplicar a eficácia.
    // Abaixo de 100% de eficácia, só a fração proporcional é PAGA
    // (comissão); o resto fica RETIDO para a FL (nunca é uma dívida do
    // gerente, e nunca é pago a mais ninguém).
    double recebidoComPct = linha.recebido * linha.porcentagem / 100.0;
    linha.retido = recebidoComPct * (1.0 - linha.eficacia / 100.0);

    // − descontos (Delta Síndicos). Nunca negativa: um desconto maior que a
    // comissão do mês vira zero, não uma dívida do gerente — é assim que a
    // planilha se comporta (ULISSES, PAULO).
    linha.comissao = recebidoComPct * linha.eficacia / 100.0 - linha.descontos;
    if (linha.comissao < 0) linha.comissao = 0;

    out.gerenciaLiquido += linha.comissao;
    out.retido += linha.retido;
    out.gerentes.push_back(linha);
  }
  std::sort(out.gerentes.begin(), out.gerentes.end(),
            [](const DashboardGerenteLinha& a, const DashboardGerenteLinha& b) {
              return a.gerenteNome < b.gerenteNome;
            });

  // ---- painel de empresas (parceiras) ----
  // Mesma regra do painel de gerentes: a lista inteira de parceiras, com
  // zero para quem não teve venda no mês.
  for (const auto& e : listParceiros(db)) {
    DashboardEmpresaLinha linha;
    linha.empresaId = e.id;
    linha.empresaNome = e.nome;
    for (const auto& s : doMes) {
      if (s.parceiroId == e.id) linha.recebidos += s.venda;
    }
    out.empresas.push_back(linha);
  }

  return out;
}

namespace {

constexpr const char* kDashboardCols =
    "id, mes_referencia, dados_json, observacoes, gerado_em, created_at";

DashboardSalvo rowToDashboard(Statement& st) {
  DashboardSalvo d;
  d.id = st.columnText(0);
  d.mesReferencia = st.columnText(1);
  d.dadosJson = st.columnText(2);
  d.observacoes = textOrEmpty(st, 3);
  d.geradoEm = st.columnText(4);
  d.createdAt = st.columnText(5);
  return d;
}

}  // namespace

std::vector<DashboardSalvo> listDashboards(Database& db) {
  std::vector<DashboardSalvo> out;
  auto st = db.prepare(std::string("SELECT ") + kDashboardCols +
                       " FROM sos_dashboards_fechamento ORDER BY mes_referencia DESC, gerado_em DESC");
  while (st.step()) out.push_back(rowToDashboard(st));
  return out;
}

std::optional<DashboardSalvo> findDashboard(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kDashboardCols +
                       " FROM sos_dashboards_fechamento WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToDashboard(st);
}

DashboardSalvo salvarDashboard(Database& db, const DashboardSalvo& input) {
  if (!isValidMesReferencia(input.mesReferencia)) {
    throw std::invalid_argument("data de referência inválida (use mês/ano)");
  }
  if (trim(input.dadosJson).empty()) throw std::invalid_argument("dashboard vazio");

  auto st = db.prepare(
      "INSERT INTO sos_dashboards_fechamento (id, mes_referencia, dados_json, observacoes, "
      "gerado_em, created_at) VALUES (?, ?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, input.mesReferencia).bind(3, input.dadosJson);
  st.bind(4, input.observacoes).bind(5, input.geradoEm).bind(6, input.createdAt);
  st.step();

  return *findDashboard(db, input.id);
}

namespace {

constexpr const char* kPagamentoCols =
    "id, mes_referencia, dados_json, observacoes, fechado, gerado_em, fechado_em, created_at";

PagamentoSalvo rowToPagamento(Statement& st) {
  PagamentoSalvo p;
  p.id = st.columnText(0);
  p.mesReferencia = st.columnText(1);
  p.dadosJson = st.columnText(2);
  p.observacoes = textOrEmpty(st, 3);
  p.fechado = st.columnDouble(4) != 0;
  p.geradoEm = st.columnText(5);
  p.fechadoEm = textOrEmpty(st, 6);
  p.createdAt = st.columnText(7);
  return p;
}

}  // namespace

std::vector<PagamentoSalvo> listPagamentosSos(Database& db) {
  std::vector<PagamentoSalvo> out;
  auto st = db.prepare(std::string("SELECT ") + kPagamentoCols +
                       " FROM sos_pagamentos ORDER BY mes_referencia DESC, gerado_em DESC");
  while (st.step()) out.push_back(rowToPagamento(st));
  return out;
}

std::optional<PagamentoSalvo> findPagamentoSos(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kPagamentoCols + " FROM sos_pagamentos WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToPagamento(st);
}

std::optional<PagamentoSalvo> findPagamentoSosPorMes(Database& db, const std::string& mesReferencia) {
  auto st = db.prepare(std::string("SELECT ") + kPagamentoCols +
                       " FROM sos_pagamentos WHERE mes_referencia=?");
  st.bind(1, mesReferencia);
  if (!st.step()) return std::nullopt;
  return rowToPagamento(st);
}

PagamentoSalvo salvarPagamentoSos(Database& db, const PagamentoSalvo& input) {
  if (!isValidMesReferencia(input.mesReferencia)) {
    throw std::invalid_argument("data de referência inválida (use mês/ano)");
  }
  if (trim(input.dadosJson).empty()) throw std::invalid_argument("pagamento vazio");

  bool existe = findPagamentoSos(db, input.id).has_value();
  if (existe) {
    auto st = db.prepare(
        "UPDATE sos_pagamentos SET mes_referencia=?, dados_json=?, observacoes=?, fechado=?, "
        "gerado_em=?, fechado_em=? WHERE id=?");
    st.bind(1, input.mesReferencia).bind(2, input.dadosJson).bind(3, input.observacoes);
    st.bind(4, input.fechado ? 1 : 0).bind(5, input.geradoEm).bind(6, input.fechadoEm);
    st.bind(7, input.id);
    st.step();
  } else {
    auto st = db.prepare(
        "INSERT INTO sos_pagamentos (id, mes_referencia, dados_json, observacoes, fechado, "
        "gerado_em, fechado_em, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    st.bind(1, input.id).bind(2, input.mesReferencia).bind(3, input.dadosJson);
    st.bind(4, input.observacoes).bind(5, input.fechado ? 1 : 0).bind(6, input.geradoEm);
    st.bind(7, input.fechadoEm).bind(8, input.createdAt);
    st.step();
  }

  return *findPagamentoSos(db, input.id);
}

double eficaciaDe(double producao, int condominiosNaCarteira, double metaPorCondominio) {
  if (condominiosNaCarteira <= 0 || metaPorCondominio <= 0) return 100.0;
  double producaoPorCondominio = producao / condominiosNaCarteira;
  if (producaoPorCondominio >= metaPorCondominio) return 100.0;
  return (producaoPorCondominio / metaPorCondominio) * 100.0;
}

std::string getConfig(Database& db, const std::string& key, const std::string& def) {
  auto st = db.prepare("SELECT value FROM sos_config WHERE key=?");
  st.bind(1, key);
  if (!st.step()) return def;
  return st.columnText(0);
}

void setConfig(Database& db, const std::string& key, const std::string& value) {
  auto st = db.prepare("INSERT INTO sos_config (key, value) VALUES (?, ?) "
                       "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  st.bind(1, key).bind(2, value);
  st.step();
}

}  // namespace estoque
