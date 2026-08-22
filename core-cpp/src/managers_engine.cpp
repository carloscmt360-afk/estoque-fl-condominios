#include "estoque/managers_engine.hpp"

#include <algorithm>
#include <stdexcept>

#include "estoque/dates_engine.hpp"

namespace estoque {

namespace {

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string textOrEmpty(Statement& st, int idx) { return st.columnIsNull(idx) ? "" : st.columnText(idx); }

constexpr const char* kGerenteCols =
    "id, numero, nome, telefone, email, chave_pix, observacoes, created_at";

Gerente rowToGerente(Statement& st) {
  Gerente g;
  g.id = st.columnText(0);
  g.numero = static_cast<int>(st.columnDouble(1));
  g.nome = st.columnText(2);
  g.telefone = textOrEmpty(st, 3);
  g.email = textOrEmpty(st, 4);
  g.chavePix = textOrEmpty(st, 5);
  g.observacoes = textOrEmpty(st, 6);
  g.createdAt = st.columnText(7);
  return g;
}


void loadCondominios(Database& db, Gerente& g) {
  auto st = db.prepare(
      "SELECT c.id FROM gerente_condominios gc JOIN condominios c ON c.id = gc.condominio_id "
      "WHERE gc.gerente_id=? ORDER BY c.nome");
  st.bind(1, g.id);
  while (st.step()) g.condominioIds.push_back(st.columnText(0));
}

// Regrava a carteira inteira. Recusa condomínio inexistente: uma marcação
// órfã sumiria da tela sem avisar, e o gerente apareceria com menos
// condomínios do que o usuário selecionou.
void writeCondominios(Database& db, const std::string& gerenteId,
                     const std::vector<std::string>& condominioIds) {
  db.prepare("DELETE FROM gerente_condominios WHERE gerente_id=?").bind(1, gerenteId).step();

  std::vector<std::string> jaGravados;
  for (const auto& id : condominioIds) {
    if (id.empty()) continue;
    if (std::find(jaGravados.begin(), jaGravados.end(), id) != jaGravados.end()) continue;
    if (!findCondominio(db, id)) {
      throw NotFoundError("condomínio não encontrado: " + id);
    }
    auto st = db.prepare(
        "INSERT INTO gerente_condominios (gerente_id, condominio_id) VALUES (?, ?)");
    st.bind(1, gerenteId).bind(2, id);
    st.step();
    jaGravados.push_back(id);
  }
}

void validateGerente(const Gerente& input) {
  if (trim(input.nome).empty()) throw std::invalid_argument("informe o nome do gerente");
}

}  // namespace

// Próximo número disponível — usa e AVANÇA o contador persistido em
// app_settings (chave 'gerentes_numero_seq'), nunca a contagem de linhas da
// tabela (mesmo critério de nextSku em inventory_engine.cpp: um gerente
// excluído não pode fazer um número ser reciclado).
int nextGerenteNumero(Database& db) {
  long long seq = 0;
  {
    auto st = db.prepare("SELECT value FROM app_settings WHERE key='gerentes_numero_seq'");
    if (st.step()) seq = std::stoll(st.columnText(0));
  }
  ++seq;
  db.prepare("INSERT OR REPLACE INTO app_settings (key, value) VALUES ('gerentes_numero_seq', ?)")
      .bind(1, std::to_string(seq))
      .step();
  return static_cast<int>(seq);
}

std::vector<Gerente> listGerentes(Database& db) {
  std::vector<Gerente> out;
  {
    auto st = db.prepare(std::string("SELECT ") + kGerenteCols + " FROM gerentes ORDER BY nome");
    while (st.step()) out.push_back(rowToGerente(st));
  }
  for (auto& g : out) loadCondominios(db, g);
  return out;
}

std::optional<Gerente> findGerente(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kGerenteCols + " FROM gerentes WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  Gerente g = rowToGerente(st);
  loadCondominios(db, g);
  return g;
}

Gerente createGerente(Database& db, const Gerente& input) {
  validateGerente(input);

  Transaction tx(db);
  int numero = nextGerenteNumero(db);
  auto st = db.prepare(
      "INSERT INTO gerentes (id, numero, nome, telefone, email, chave_pix, observacoes, created_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, static_cast<double>(numero)).bind(3, trim(input.nome));
  st.bind(4, input.telefone).bind(5, input.email).bind(6, input.chavePix);
  st.bind(7, input.observacoes).bind(8, input.createdAt);
  st.step();
  writeCondominios(db, input.id, input.condominioIds);
  tx.commit();

  return *findGerente(db, input.id);
}

Gerente updateGerente(Database& db, const Gerente& input) {
  if (!findGerente(db, input.id)) throw NotFoundError("gerente não encontrado: " + input.id);
  validateGerente(input);

  Transaction tx(db);
  auto st = db.prepare(
      "UPDATE gerentes SET nome=?, telefone=?, email=?, chave_pix=?, observacoes=? WHERE id=?");
  st.bind(1, trim(input.nome)).bind(2, input.telefone).bind(3, input.email);
  st.bind(4, input.chavePix).bind(5, input.observacoes).bind(6, input.id);
  st.step();
  writeCondominios(db, input.id, input.condominioIds);
  tx.commit();

  return *findGerente(db, input.id);
}

void deleteGerente(Database& db, const std::string& id) {
  if (!findGerente(db, id)) throw NotFoundError("gerente não encontrado: " + id);
  // A carteira vai junto por ON DELETE CASCADE (ver schema v11).
  db.prepare("DELETE FROM gerentes WHERE id=?").bind(1, id).step();
}

}  // namespace estoque
