#include "estoque/companies_engine.hpp"

#include <algorithm>
#include <stdexcept>

namespace estoque {

namespace {

// As quatro divisões do negócio, na ordem em que a tela as mostra.
const std::vector<SetorInfo> kSetorCatalog = {
    {setores::kVendas, "Vendas"},
    {setores::kContratosManutencoes, "Contratos / Manutenções"},
    {setores::kTerceirizadas, "Terceirizadas"},
    {setores::kEngenharia, "Engenharia"},
};

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string upperAscii(const std::string& s) {
  std::string out = s;
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

// Forma "dobrada" de um nome, para comparar nichos: minúsculas, sem acento e
// com os espaços internos colapsados.
//
// Por que ignorar acento: o nicho é digitado à mão e é ele que agrupa o
// Catálogo. Se "Impermeabilização" e "Impermeabilizacao" pudessem coexistir, a
// mesma especialidade apareceria em duas linhas e as empresas ficariam
// espalhadas entre elas — exatamente o que o Catálogo existe para evitar.
//
// A tabela cobre os acentos do português (Latin-1 Supplement em UTF-8, sempre
// 0xC3 seguido do segundo byte). Não é normalização Unicode completa, e não
// precisa ser: nome de especialidade não tem cirílico nem forma decomposta.
std::string folded(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xC3 && i + 1 < s.size()) {
      unsigned char d = static_cast<unsigned char>(s[i + 1]);
      char base = 0;
      if ((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) base = 'a';
      else if (d == 0x87 || d == 0xA7) base = 'c';
      else if ((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) base = 'e';
      else if ((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) base = 'i';
      else if (d == 0x91 || d == 0xB1) base = 'n';
      else if ((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) base = 'o';
      else if ((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) base = 'u';
      if (base != 0) {
        out += base;
        ++i;  // consome o segundo byte da sequência
        continue;
      }
    }
    if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
    else if (c == '\t' || c == '\r' || c == '\n') out += ' ';
    else out += static_cast<char>(c);
  }
  // colapsa espaços repetidos: "Limpeza  de caixa" == "Limpeza de caixa"
  std::string compacto;
  bool espacoAnterior = false;
  for (char c : out) {
    if (c == ' ') {
      if (!espacoAnterior && !compacto.empty()) compacto += c;
      espacoAnterior = true;
    } else {
      compacto += c;
      espacoAnterior = false;
    }
  }
  while (!compacto.empty() && compacto.back() == ' ') compacto.pop_back();
  return compacto;
}

std::string textOrEmpty(Statement& st, int idx) { return st.columnIsNull(idx) ? "" : st.columnText(idx); }

constexpr const char* kEspecialidadeCols = "id, setor, nome, created_at";

Especialidade rowToEspecialidade(Statement& st) {
  Especialidade e;
  e.id = st.columnText(0);
  e.setor = st.columnText(1);
  e.nome = st.columnText(2);
  e.createdAt = st.columnText(3);
  return e;
}

constexpr const char* kEmpresaCols =
    "id, nome, nome_fantasia, cnpj, cnpj_key, endereco, numero, complemento, bairro, cep, cidade, "
    "estado, telefone, emails, observacoes, parceira, created_at";

Empresa rowToEmpresa(Statement& st) {
  Empresa e;
  e.id = st.columnText(0);
  e.nome = st.columnText(1);
  e.nomeFantasia = textOrEmpty(st, 2);
  e.cnpj = textOrEmpty(st, 3);
  // coluna 4 (cnpj_key) é interna — só carrega o UNIQUE, não vai para o modelo
  e.endereco = textOrEmpty(st, 5);
  e.numero = textOrEmpty(st, 6);
  e.complemento = textOrEmpty(st, 7);
  e.bairro = textOrEmpty(st, 8);
  e.cep = textOrEmpty(st, 9);
  e.cidade = textOrEmpty(st, 10);
  e.estado = textOrEmpty(st, 11);
  e.telefone = textOrEmpty(st, 12);
  e.emails = textOrEmpty(st, 13);
  e.observacoes = textOrEmpty(st, 14);
  e.parceira = st.columnDouble(15) != 0;
  e.createdAt = st.columnText(16);
  return e;
}

void loadEspecialidades(Database& db, Empresa& e) {
  auto st = db.prepare(
      "SELECT es.id FROM empresa_especialidades ee JOIN especialidades es ON es.id = ee.especialidade_id "
      "WHERE ee.empresa_id=? ORDER BY es.setor, es.nome");
  st.bind(1, e.id);
  while (st.step()) e.especialidadeIds.push_back(st.columnText(0));
}

// Regrava as marcações da empresa. Recusa especialidade inexistente: uma
// marcação órfã sumiria da tela sem avisar, e a empresa apareceria com menos
// especialidades do que o usuário escolheu.
void writeEspecialidades(Database& db, const std::string& empresaId,
                         const std::vector<std::string>& especialidadeIds) {
  db.prepare("DELETE FROM empresa_especialidades WHERE empresa_id=?").bind(1, empresaId).step();

  std::vector<std::string> jaGravadas;
  for (const auto& id : especialidadeIds) {
    if (id.empty()) continue;
    // O mesmo id repetido na seleção não é erro do usuário (dois cliques), mas
    // não pode virar duas linhas.
    if (std::find(jaGravadas.begin(), jaGravadas.end(), id) != jaGravadas.end()) continue;
    if (!findEspecialidade(db, id)) {
      throw NotFoundError("especialidade não encontrada: " + id);
    }
    auto st = db.prepare(
        "INSERT INTO empresa_especialidades (empresa_id, especialidade_id) VALUES (?, ?)");
    st.bind(1, empresaId).bind(2, id);
    st.step();
    jaGravadas.push_back(id);
  }
}

void validateEspecialidade(Database& db, const Especialidade& input, const std::string& exceptId) {
  if (!isKnownSetor(input.setor)) {
    throw std::invalid_argument("setor inválido: '" + input.setor + "'");
  }
  std::string nome = trim(input.nome);
  if (nome.empty()) throw std::invalid_argument("informe o nome da especialidade");

  // Repetido só importa dentro do MESMO setor — ver o comentário no header.
  // A comparação é feita em C++ (e não no SQL) porque ela ignora acento, e
  // nenhum COLLATE do SQLite sem ICU faz isso — ver folded().
  auto st = db.prepare("SELECT nome FROM especialidades WHERE setor=? AND id<>?");
  st.bind(1, input.setor).bind(2, exceptId);
  std::string alvo = folded(nome);
  while (st.step()) {
    std::string existente = st.columnText(0);
    if (folded(existente) == alvo) {
      throw std::invalid_argument("já existe \"" + existente + "\" em " + setorLabel(input.setor));
    }
  }
}

void validateEmpresa(Database& db, const Empresa& input, const std::string& exceptId) {
  if (trim(input.nome).empty()) throw std::invalid_argument("informe o nome da empresa");

  std::string key = canonCnpj(input.cnpj);
  if (key.empty()) return;  // CNPJ é opcional
  auto st = db.prepare("SELECT nome FROM empresas WHERE cnpj_key=? AND id<>?");
  st.bind(1, key).bind(2, exceptId);
  if (st.step()) {
    throw std::invalid_argument("o CNPJ informado já está cadastrado em \"" + st.columnText(0) + "\"");
  }
}

}  // namespace

// ------------------------------------------------------------------ setores

const std::vector<SetorInfo>& setorCatalog() { return kSetorCatalog; }

bool isKnownSetor(const std::string& key) {
  return std::any_of(kSetorCatalog.begin(), kSetorCatalog.end(),
                     [&](const SetorInfo& s) { return s.key == key; });
}

std::string setorLabel(const std::string& key) {
  for (const auto& s : kSetorCatalog) {
    if (s.key == key) return s.label;
  }
  return key;
}

// ------------------------------------------------------------ especialidades

std::vector<Especialidade> listEspecialidades(Database& db) {
  std::vector<Especialidade> out;
  auto st = db.prepare(std::string("SELECT ") + kEspecialidadeCols +
                       " FROM especialidades ORDER BY setor, nome");
  while (st.step()) out.push_back(rowToEspecialidade(st));
  return out;
}

std::optional<Especialidade> findEspecialidade(Database& db, const std::string& id) {
  auto st =
      db.prepare(std::string("SELECT ") + kEspecialidadeCols + " FROM especialidades WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToEspecialidade(st);
}

Especialidade createEspecialidade(Database& db, const Especialidade& input) {
  validateEspecialidade(db, input, "");
  auto st = db.prepare(
      "INSERT INTO especialidades (id, setor, nome, created_at) VALUES (?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, input.setor).bind(3, trim(input.nome)).bind(4, input.createdAt);
  st.step();
  return *findEspecialidade(db, input.id);
}

Especialidade updateEspecialidade(Database& db, const Especialidade& input) {
  if (!findEspecialidade(db, input.id)) {
    throw NotFoundError("especialidade não encontrada: " + input.id);
  }
  validateEspecialidade(db, input, input.id);
  auto st = db.prepare("UPDATE especialidades SET setor=?, nome=? WHERE id=?");
  st.bind(1, input.setor).bind(2, trim(input.nome)).bind(3, input.id);
  st.step();
  return *findEspecialidade(db, input.id);
}

int empresasComEspecialidade(Database& db, const std::string& especialidadeId) {
  auto st = db.prepare("SELECT COUNT(*) FROM empresa_especialidades WHERE especialidade_id=?");
  st.bind(1, especialidadeId);
  st.step();
  return static_cast<int>(st.columnDouble(0));
}

void deleteEspecialidade(Database& db, const std::string& id) {
  auto existing = findEspecialidade(db, id);
  if (!existing) throw NotFoundError("especialidade não encontrada: " + id);

  int emUso = empresasComEspecialidade(db, id);
  if (emUso > 0) {
    throw std::invalid_argument("\"" + existing->nome + "\" está marcada em " +
                                std::to_string(emUso) +
                                " empresa(s) — desmarque-a nelas antes de excluir");
  }
  db.prepare("DELETE FROM especialidades WHERE id=?").bind(1, id).step();
}

// ----------------------------------------------------------------- empresas

std::string canonCnpj(const std::string& cnpj) {
  std::string out;
  for (char c : cnpj) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) out += c;
    else if (c >= 'a' && c <= 'z') out += static_cast<char>(c - 'a' + 'A');
  }
  return out;
}

std::vector<Empresa> listEmpresas(Database& db) {
  std::vector<Empresa> out;
  {
    auto st = db.prepare(std::string("SELECT ") + kEmpresaCols + " FROM empresas ORDER BY nome");
    while (st.step()) out.push_back(rowToEmpresa(st));
  }
  for (auto& e : out) loadEspecialidades(db, e);
  return out;
}

std::vector<Empresa> listParceiros(Database& db) {
  std::vector<Empresa> out;
  {
    auto st = db.prepare(std::string("SELECT ") + kEmpresaCols +
                         " FROM empresas WHERE parceira<>0 ORDER BY nome");
    while (st.step()) out.push_back(rowToEmpresa(st));
  }
  for (auto& e : out) loadEspecialidades(db, e);
  return out;
}

std::optional<Empresa> findEmpresa(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kEmpresaCols + " FROM empresas WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  Empresa e = rowToEmpresa(st);
  loadEspecialidades(db, e);
  return e;
}

Empresa createEmpresa(Database& db, const Empresa& input) {
  validateEmpresa(db, input, "");

  Transaction tx(db);
  auto st = db.prepare(
      "INSERT INTO empresas (id, nome, nome_fantasia, cnpj, cnpj_key, endereco, numero, complemento, "
      "bairro, cep, cidade, estado, telefone, emails, observacoes, parceira, created_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, trim(input.nome));
  input.nomeFantasia.empty() ? st.bindNull(3) : st.bind(3, input.nomeFantasia);
  trim(input.cnpj).empty() ? st.bindNull(4) : st.bind(4, trim(input.cnpj));
  canonCnpj(input.cnpj).empty() ? st.bindNull(5) : st.bind(5, canonCnpj(input.cnpj));
  st.bind(6, input.endereco).bind(7, input.numero).bind(8, input.complemento).bind(9, input.bairro);
  st.bind(10, input.cep).bind(11, input.cidade);
  st.bind(12, upperAscii(trim(input.estado))).bind(13, input.telefone).bind(14, input.emails);
  st.bind(15, input.observacoes).bind(16, input.parceira ? 1.0 : 0.0).bind(17, input.createdAt);
  st.step();
  writeEspecialidades(db, input.id, input.especialidadeIds);
  tx.commit();

  return *findEmpresa(db, input.id);
}

Empresa updateEmpresa(Database& db, const Empresa& input) {
  if (!findEmpresa(db, input.id)) throw NotFoundError("empresa não encontrada: " + input.id);
  validateEmpresa(db, input, input.id);

  Transaction tx(db);
  auto st = db.prepare(
      "UPDATE empresas SET nome=?, nome_fantasia=?, cnpj=?, cnpj_key=?, endereco=?, numero=?, "
      "complemento=?, bairro=?, cep=?, cidade=?, estado=?, telefone=?, emails=?, observacoes=?, "
      "parceira=? WHERE id=?");
  st.bind(1, trim(input.nome));
  input.nomeFantasia.empty() ? st.bindNull(2) : st.bind(2, input.nomeFantasia);
  trim(input.cnpj).empty() ? st.bindNull(3) : st.bind(3, trim(input.cnpj));
  canonCnpj(input.cnpj).empty() ? st.bindNull(4) : st.bind(4, canonCnpj(input.cnpj));
  st.bind(5, input.endereco).bind(6, input.numero).bind(7, input.complemento).bind(8, input.bairro);
  st.bind(9, input.cep).bind(10, input.cidade);
  st.bind(11, upperAscii(trim(input.estado))).bind(12, input.telefone).bind(13, input.emails);
  st.bind(14, input.observacoes).bind(15, input.parceira ? 1.0 : 0.0).bind(16, input.id);
  st.step();
  writeEspecialidades(db, input.id, input.especialidadeIds);
  tx.commit();

  return *findEmpresa(db, input.id);
}

void deleteEmpresa(Database& db, const std::string& id) {
  if (!findEmpresa(db, id)) throw NotFoundError("empresa não encontrada: " + id);
  // As marcações vão junto por ON DELETE CASCADE (ver schema v9).
  db.prepare("DELETE FROM empresas WHERE id=?").bind(1, id).step();
}

}  // namespace estoque
