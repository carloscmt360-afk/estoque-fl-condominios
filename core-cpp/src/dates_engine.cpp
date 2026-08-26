#include "estoque/dates_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

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

// `<input type="date">` manda "YYYY-MM-DD" (10 caracteres); time_utils::isoToEpochMs
// exige o "T...Z" completo. Normaliza para meia-noite UTC nesse caso — datas já
// completas (ex.: vindas de outro cálculo) passam intactas.
std::string normalizeDateOnly(const std::string& iso) {
  if (iso.size() == 10) return iso + "T00:00:00.000Z";
  return iso;
}

constexpr int64_t kMsPerDay = 86400000;

const std::vector<LocalizacaoInfo> kLocalizacaoCatalog = {
    {localizacao_condominio::kCentro, "Centro"},
    {localizacao_condominio::kLeste, "Leste"},
    {localizacao_condominio::kOeste, "Oeste"},
    {localizacao_condominio::kNorte, "Norte"},
    {localizacao_condominio::kNoroeste, "Noroeste"},
    {localizacao_condominio::kSul, "Sul"},
    {localizacao_condominio::kOutraCidade, "Outra cidade"},
};

constexpr const char* kCondominioCols =
    "id, nome, nome_fantasia, cnpj, codigo, endereco, numero, complemento, bairro, cidade, estado, cep, "
    "localizacao, sindico, telefone, email, observacoes, ativo, created_at, delta_sindica, "
    "aviso_previo_ate, aviso_previo_novo_codigo";

Condominio rowToCondominio(Statement& st) {
  Condominio c;
  c.id = st.columnText(0);
  c.nome = st.columnText(1);
  c.nomeFantasia = textOrEmpty(st, 2);
  c.cnpj = textOrEmpty(st, 3);
  c.codigo = textOrEmpty(st, 4);
  c.endereco = textOrEmpty(st, 5);
  c.numero = textOrEmpty(st, 6);
  c.complemento = textOrEmpty(st, 7);
  c.bairro = textOrEmpty(st, 8);
  c.cidade = textOrEmpty(st, 9);
  c.estado = textOrEmpty(st, 10);
  c.cep = textOrEmpty(st, 11);
  c.localizacao = textOrEmpty(st, 12);
  c.sindico = textOrEmpty(st, 13);
  c.telefone = textOrEmpty(st, 14);
  c.email = textOrEmpty(st, 15);
  c.observacoes = textOrEmpty(st, 16);
  c.ativo = st.columnDouble(17) != 0;
  c.createdAt = st.columnText(18);
  c.deltaSindica = st.columnDouble(19) != 0;
  c.avisoPrevioAte = textOrEmpty(st, 20);
  c.avisoPrevioNovoCodigo = textOrEmpty(st, 21);
  return c;
}

void validateLocalizacao(const std::string& localizacao) {
  if (!localizacao.empty() && !isKnownLocalizacao(localizacao)) {
    throw std::invalid_argument("localização inválida: '" + localizacao + "'");
  }
}

constexpr const char* kTipoServicoCols = "id, nome, prazo_dias, cor, created_at";

TipoServico rowToTipoServico(Statement& st) {
  TipoServico t;
  t.id = st.columnText(0);
  t.nome = st.columnText(1);
  t.prazoDias = static_cast<int>(st.columnDouble(2));
  t.cor = st.columnText(3);
  t.createdAt = st.columnText(4);
  return t;
}

constexpr const char* kServicoCondominioCols =
    "id, condominio_id, tipo_servico_id, data_ultima_renovacao, empresa_contratada, observacoes, "
    "created_at";

ServicoCondominio rowToServicoCondominio(Statement& st) {
  ServicoCondominio s;
  s.id = st.columnText(0);
  s.condominioId = st.columnText(1);
  s.tipoServicoId = st.columnText(2);
  s.dataUltimaRenovacao = st.columnText(3);
  s.empresaContratada = textOrEmpty(st, 4);
  s.observacoes = textOrEmpty(st, 5);
  s.createdAt = st.columnText(6);
  return s;
}

constexpr const char* kRenovacaoCols =
    "id, servico_condominio_id, data_renovacao, empresa_contratada, prazo_dias_aplicado, observacoes, "
    "created_at";

Renovacao rowToRenovacao(Statement& st) {
  Renovacao r;
  r.id = st.columnText(0);
  r.servicoCondominioId = st.columnText(1);
  r.dataRenovacao = st.columnText(2);
  r.empresaContratada = textOrEmpty(st, 3);
  r.prazoDiasAplicado = static_cast<int>(st.columnDouble(4));
  r.observacoes = textOrEmpty(st, 5);
  r.createdAt = st.columnText(6);
  return r;
}

}  // namespace

const std::vector<LocalizacaoInfo>& localizacaoCatalog() { return kLocalizacaoCatalog; }

bool isKnownLocalizacao(const std::string& key) {
  return std::any_of(kLocalizacaoCatalog.begin(), kLocalizacaoCatalog.end(),
                     [&](const LocalizacaoInfo& l) { return l.key == key; });
}

std::string localizacaoLabel(const std::string& key) {
  for (const auto& l : kLocalizacaoCatalog) {
    if (l.key == key) return l.label;
  }
  return key;
}

StatusVencimento calcularStatus(const std::string& dataUltimaRenovacao, int prazoDias,
                                const std::string& hojeIso) {
  int64_t venceMs = time_utils::isoToEpochMs(normalizeDateOnly(dataUltimaRenovacao)) +
                    static_cast<int64_t>(prazoDias) * kMsPerDay;
  int64_t hojeMs = time_utils::isoToEpochMs(normalizeDateOnly(hojeIso));

  StatusVencimento out;
  out.dataVencimento = time_utils::epochMsToIso(venceMs);
  // Arredonda para cima: "faltam 0,3 dias" ainda conta como "falta 1 dia", não
  // como vencido — só é vencido quando o prazo já passou de verdade.
  out.diasRestantes = static_cast<int>((venceMs - hojeMs + kMsPerDay - 1) / kMsPerDay);
  if (out.diasRestantes < 0) {
    out.status = vencimento_status::kVencido;
  } else if (out.diasRestantes <= 30) {
    out.status = vencimento_status::kAtencao;
  } else {
    out.status = vencimento_status::kOk;
  }
  return out;
}

// --------------------------------------------------------------- Condomínios

std::vector<Condominio> listCondominios(Database& db) {
  std::vector<Condominio> out;
  auto st = db.prepare(std::string("SELECT ") + kCondominioCols + " FROM condominios ORDER BY nome");
  while (st.step()) out.push_back(rowToCondominio(st));
  return out;
}

std::optional<Condominio> findCondominio(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kCondominioCols + " FROM condominios WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToCondominio(st);
}

Condominio createCondominio(Database& db, const Condominio& input) {
  std::string nome = trim(input.nome);
  if (nome.empty()) throw std::invalid_argument("informe o nome do condomínio");
  validateLocalizacao(input.localizacao);

  auto st = db.prepare(
      "INSERT INTO condominios (id, nome, nome_fantasia, cnpj, codigo, endereco, numero, complemento, "
      "bairro, cidade, estado, cep, localizacao, sindico, telefone, email, observacoes, ativo, created_at, "
      "delta_sindica) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, nome);
  input.nomeFantasia.empty() ? st.bindNull(3) : st.bind(3, input.nomeFantasia);
  input.cnpj.empty() ? st.bindNull(4) : st.bind(4, input.cnpj);
  input.codigo.empty() ? st.bindNull(5) : st.bind(5, input.codigo);
  input.endereco.empty() ? st.bindNull(6) : st.bind(6, input.endereco);
  input.numero.empty() ? st.bindNull(7) : st.bind(7, input.numero);
  input.complemento.empty() ? st.bindNull(8) : st.bind(8, input.complemento);
  input.bairro.empty() ? st.bindNull(9) : st.bind(9, input.bairro);
  input.cidade.empty() ? st.bindNull(10) : st.bind(10, input.cidade);
  input.estado.empty() ? st.bindNull(11) : st.bind(11, input.estado);
  input.cep.empty() ? st.bindNull(12) : st.bind(12, input.cep);
  input.localizacao.empty() ? st.bindNull(13) : st.bind(13, input.localizacao);
  input.sindico.empty() ? st.bindNull(14) : st.bind(14, input.sindico);
  input.telefone.empty() ? st.bindNull(15) : st.bind(15, input.telefone);
  input.email.empty() ? st.bindNull(16) : st.bind(16, input.email);
  input.observacoes.empty() ? st.bindNull(17) : st.bind(17, input.observacoes);
  st.bind(18, input.ativo ? 1.0 : 0.0);
  st.bind(19, input.createdAt);
  st.bind(20, input.deltaSindica ? 1.0 : 0.0);
  st.step();
  return *findCondominio(db, input.id);
}

Condominio updateCondominio(Database& db, const Condominio& input) {
  if (!findCondominio(db, input.id)) throw NotFoundError("condomínio não encontrado: " + input.id);
  std::string nome = trim(input.nome);
  if (nome.empty()) throw std::invalid_argument("informe o nome do condomínio");
  validateLocalizacao(input.localizacao);

  auto st = db.prepare(
      "UPDATE condominios SET nome=?, nome_fantasia=?, cnpj=?, codigo=?, endereco=?, numero=?, "
      "complemento=?, bairro=?, cidade=?, estado=?, cep=?, localizacao=?, sindico=?, telefone=?, email=?, "
      "observacoes=?, ativo=?, delta_sindica=? WHERE id=?");
  st.bind(1, nome);
  input.nomeFantasia.empty() ? st.bindNull(2) : st.bind(2, input.nomeFantasia);
  input.cnpj.empty() ? st.bindNull(3) : st.bind(3, input.cnpj);
  input.codigo.empty() ? st.bindNull(4) : st.bind(4, input.codigo);
  input.endereco.empty() ? st.bindNull(5) : st.bind(5, input.endereco);
  input.numero.empty() ? st.bindNull(6) : st.bind(6, input.numero);
  input.complemento.empty() ? st.bindNull(7) : st.bind(7, input.complemento);
  input.bairro.empty() ? st.bindNull(8) : st.bind(8, input.bairro);
  input.cidade.empty() ? st.bindNull(9) : st.bind(9, input.cidade);
  input.estado.empty() ? st.bindNull(10) : st.bind(10, input.estado);
  input.cep.empty() ? st.bindNull(11) : st.bind(11, input.cep);
  input.localizacao.empty() ? st.bindNull(12) : st.bind(12, input.localizacao);
  input.sindico.empty() ? st.bindNull(13) : st.bind(13, input.sindico);
  input.telefone.empty() ? st.bindNull(14) : st.bind(14, input.telefone);
  input.email.empty() ? st.bindNull(15) : st.bind(15, input.email);
  input.observacoes.empty() ? st.bindNull(16) : st.bind(16, input.observacoes);
  st.bind(17, input.ativo ? 1.0 : 0.0);
  st.bind(18, input.deltaSindica ? 1.0 : 0.0);
  st.bind(19, input.id);
  st.step();
  return *findCondominio(db, input.id);
}

void deleteCondominio(Database& db, const std::string& id) {
  // ON DELETE CASCADE (ver schema v4) já leva junto os vínculos deste
  // condomínio e, em cadeia, o histórico de renovações deles.
  db.prepare("DELETE FROM condominios WHERE id=?").bind(1, id).step();
}

// Agenda a saída: o condomínio segue ativo normalmente até `ate` (inclusive)
// — só o cadastro fica marcado; nada muda de comportamento em nenhuma outra
// tela até aplicarAvisosPrevioVencidos decidir que já passou da data.
Condominio iniciarAvisoPrevio(Database& db, const std::string& condominioId, const std::string& ate,
                              const std::string& novoCodigo) {
  auto c = findCondominio(db, condominioId);
  if (!c) throw NotFoundError("condomínio não encontrado: " + condominioId);
  if (!c->ativo) throw std::invalid_argument("este condomínio já está inativo");
  if (trim(ate).empty()) throw std::invalid_argument("informe até quando o condomínio fica ativo");
  if (trim(novoCodigo).empty()) throw std::invalid_argument("informe o novo código após a saída");

  db.prepare("UPDATE condominios SET aviso_previo_ate=?, aviso_previo_novo_codigo=? WHERE id=?")
      .bind(1, ate)
      .bind(2, trim(novoCodigo))
      .bind(3, condominioId)
      .step();
  return *findCondominio(db, condominioId);
}

// O cliente decidiu ficar — desmarca o aviso sem mexer em mais nada (o
// condomínio nunca chegou a mudar de estado, então não há o que desfazer
// além de limpar a agenda).
Condominio cancelarAvisoPrevio(Database& db, const std::string& condominioId) {
  if (!findCondominio(db, condominioId)) throw NotFoundError("condomínio não encontrado: " + condominioId);
  db.prepare("UPDATE condominios SET aviso_previo_ate=NULL, aviso_previo_novo_codigo=NULL WHERE id=?")
      .bind(1, condominioId)
      .step();
  return *findCondominio(db, condominioId);
}

// Chamada a cada listCondominios (ver Api::listCondominios) — sem scheduler
// em background nesta app desktop, varrer no momento da listagem é o
// equivalente prático de "acontece sozinho, sem precisar editar nada": quem
// abrir a tela de Cadastro de Condomínios depois da data já vê o resultado
// aplicado. Comparação por isoToEpochMs (não string) porque aviso_previo_ate
// vem de <input type="date"> ("YYYY-MM-DD") e hojeIso pode vir com hora —
// comparar como texto funcionaria hoje mas quebraria silenciosamente se um
// dos dois formatos mudasse.
void aplicarAvisosPrevioVencidos(Database& db, const std::string& hojeIso) {
  int64_t hojeMs = time_utils::isoToEpochMs(normalizeDateOnly(hojeIso));
  for (const auto& c : listCondominios(db)) {
    if (c.avisoPrevioAte.empty()) continue;
    if (time_utils::isoToEpochMs(normalizeDateOnly(c.avisoPrevioAte)) > hojeMs) continue;
    db.prepare("UPDATE condominios SET ativo=0, codigo=?, aviso_previo_ate=NULL, "
               "aviso_previo_novo_codigo=NULL WHERE id=?")
        .bind(1, c.avisoPrevioNovoCodigo)
        .bind(2, c.id)
        .step();
  }
}

// -------------------------------------------------------------- Tipos de serviço

std::vector<TipoServico> listTiposServico(Database& db) {
  std::vector<TipoServico> out;
  auto st = db.prepare(std::string("SELECT ") + kTipoServicoCols + " FROM tipos_servico ORDER BY nome");
  while (st.step()) out.push_back(rowToTipoServico(st));
  return out;
}

std::optional<TipoServico> findTipoServico(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kTipoServicoCols + " FROM tipos_servico WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToTipoServico(st);
}

TipoServico createTipoServico(Database& db, const TipoServico& input) {
  std::string nome = trim(input.nome);
  if (nome.empty()) throw std::invalid_argument("informe o nome do serviço");
  if (input.prazoDias <= 0) throw std::invalid_argument("o prazo do serviço precisa ser maior que zero");

  auto st = db.prepare(
      "INSERT INTO tipos_servico (id, nome, prazo_dias, cor, created_at) VALUES (?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, nome).bind(3, static_cast<double>(input.prazoDias));
  st.bind(4, input.cor.empty() ? "#2e6ba6" : input.cor);
  st.bind(5, input.createdAt);
  st.step();
  return *findTipoServico(db, input.id);
}

TipoServico updateTipoServico(Database& db, const TipoServico& input) {
  if (!findTipoServico(db, input.id)) throw NotFoundError("tipo de serviço não encontrado: " + input.id);
  std::string nome = trim(input.nome);
  if (nome.empty()) throw std::invalid_argument("informe o nome do serviço");
  if (input.prazoDias <= 0) throw std::invalid_argument("o prazo do serviço precisa ser maior que zero");

  auto st = db.prepare("UPDATE tipos_servico SET nome=?, prazo_dias=?, cor=? WHERE id=?");
  st.bind(1, nome).bind(2, static_cast<double>(input.prazoDias));
  st.bind(3, input.cor.empty() ? "#2e6ba6" : input.cor);
  st.bind(4, input.id);
  st.step();
  return *findTipoServico(db, input.id);
}

void deleteTipoServico(Database& db, const std::string& id) {
  auto st = db.prepare("SELECT COUNT(*) FROM servicos_condominio WHERE tipo_servico_id=?");
  st.bind(1, id);
  st.step();
  if (st.columnDouble(0) > 0) {
    throw std::invalid_argument(
        "este tipo de serviço está em uso por um ou mais condomínios — remova os vínculos antes de "
        "excluí-lo");
  }
  db.prepare("DELETE FROM tipos_servico WHERE id=?").bind(1, id).step();
}

// --------------------------------------------------------------------- Vínculos

std::vector<ServicoCondominio> listServicosCondominio(Database& db) {
  std::vector<ServicoCondominio> out;
  auto st = db.prepare(std::string("SELECT ") + kServicoCondominioCols +
                       " FROM servicos_condominio ORDER BY data_ultima_renovacao");
  while (st.step()) out.push_back(rowToServicoCondominio(st));
  return out;
}

std::optional<ServicoCondominio> findServicoCondominio(Database& db, const std::string& id) {
  auto st =
      db.prepare(std::string("SELECT ") + kServicoCondominioCols + " FROM servicos_condominio WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToServicoCondominio(st);
}

ServicoCondominio createServicoCondominio(Database& db, const ServicoCondominio& input) {
  if (!findCondominio(db, input.condominioId)) {
    throw NotFoundError("condomínio não encontrado: " + input.condominioId);
  }
  if (!findTipoServico(db, input.tipoServicoId)) {
    throw NotFoundError("tipo de serviço não encontrado: " + input.tipoServicoId);
  }
  if (trim(input.dataUltimaRenovacao).empty()) {
    throw std::invalid_argument("informe a data da última renovação");
  }

  auto st = db.prepare(
      "INSERT INTO servicos_condominio (id, condominio_id, tipo_servico_id, data_ultima_renovacao, "
      "empresa_contratada, observacoes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, input.condominioId).bind(3, input.tipoServicoId);
  st.bind(4, input.dataUltimaRenovacao);
  input.empresaContratada.empty() ? st.bindNull(5) : st.bind(5, input.empresaContratada);
  input.observacoes.empty() ? st.bindNull(6) : st.bind(6, input.observacoes);
  st.bind(7, input.createdAt);
  st.step();
  return *findServicoCondominio(db, input.id);
}

ServicoCondominio updateServicoCondominio(Database& db, const ServicoCondominio& input) {
  if (!findServicoCondominio(db, input.id)) {
    throw NotFoundError("vínculo de serviço não encontrado: " + input.id);
  }
  if (!findCondominio(db, input.condominioId)) {
    throw NotFoundError("condomínio não encontrado: " + input.condominioId);
  }
  if (!findTipoServico(db, input.tipoServicoId)) {
    throw NotFoundError("tipo de serviço não encontrado: " + input.tipoServicoId);
  }
  if (trim(input.dataUltimaRenovacao).empty()) {
    throw std::invalid_argument("informe a data da última renovação");
  }

  auto st = db.prepare(
      "UPDATE servicos_condominio SET condominio_id=?, tipo_servico_id=?, data_ultima_renovacao=?, "
      "empresa_contratada=?, observacoes=? WHERE id=?");
  st.bind(1, input.condominioId).bind(2, input.tipoServicoId).bind(3, input.dataUltimaRenovacao);
  input.empresaContratada.empty() ? st.bindNull(4) : st.bind(4, input.empresaContratada);
  input.observacoes.empty() ? st.bindNull(5) : st.bind(5, input.observacoes);
  st.bind(6, input.id);
  st.step();
  return *findServicoCondominio(db, input.id);
}

void deleteServicoCondominio(Database& db, const std::string& id) {
  // ON DELETE CASCADE (ver schema v4) já leva junto o histórico de renovações.
  db.prepare("DELETE FROM servicos_condominio WHERE id=?").bind(1, id).step();
}

// ------------------------------------------------------------------- Renovações

ServicoCondominio renovarServico(Database& db, const std::string& servicoCondominioId,
                                 const std::string& renovacaoId, const std::string& dataRenovacao,
                                 const std::string& empresaContratada, const std::string& observacoes,
                                 const std::string& createdAt) {
  auto vinculo = findServicoCondominio(db, servicoCondominioId);
  if (!vinculo) throw NotFoundError("vínculo de serviço não encontrado: " + servicoCondominioId);
  if (trim(dataRenovacao).empty()) throw std::invalid_argument("informe a data da renovação");

  auto tipo = findTipoServico(db, vinculo->tipoServicoId);
  if (!tipo) throw NotFoundError("tipo de serviço não encontrado: " + vinculo->tipoServicoId);

  Transaction tx(db);

  auto st = db.prepare(
      "INSERT INTO renovacoes (id, servico_condominio_id, data_renovacao, empresa_contratada, "
      "prazo_dias_aplicado, observacoes, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, renovacaoId).bind(2, servicoCondominioId).bind(3, dataRenovacao);
  empresaContratada.empty() ? st.bindNull(4) : st.bind(4, empresaContratada);
  st.bind(5, static_cast<double>(tipo->prazoDias));
  observacoes.empty() ? st.bindNull(6) : st.bind(6, observacoes);
  st.bind(7, createdAt);
  st.step();

  // A renovação também atualiza a "empresa contratada" do vínculo — é comum
  // trocar de prestador numa renovação, e o vínculo deve refletir quem presta
  // o serviço agora, não quem prestava quando foi cadastrado.
  auto ust = db.prepare(
      "UPDATE servicos_condominio SET data_ultima_renovacao=?, empresa_contratada=? WHERE id=?");
  ust.bind(1, dataRenovacao);
  empresaContratada.empty() ? ust.bindNull(2) : ust.bind(2, empresaContratada);
  ust.bind(3, servicoCondominioId);
  ust.step();

  tx.commit();
  return *findServicoCondominio(db, servicoCondominioId);
}

std::vector<Renovacao> listRenovacoes(Database& db, const std::string& servicoCondominioFilter) {
  std::vector<Renovacao> out;
  std::string sql = std::string("SELECT ") + kRenovacaoCols + " FROM renovacoes";
  if (!servicoCondominioFilter.empty()) sql += " WHERE servico_condominio_id=?";
  sql += " ORDER BY data_renovacao DESC, created_at DESC";
  auto st = db.prepare(sql);
  if (!servicoCondominioFilter.empty()) st.bind(1, servicoCondominioFilter);
  while (st.step()) out.push_back(rowToRenovacao(st));
  return out;
}

}  // namespace estoque
