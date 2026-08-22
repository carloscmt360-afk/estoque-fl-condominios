#include "estoque/suprimentos_engine.hpp"

#include <algorithm>
#include <stdexcept>

namespace estoque {

namespace {

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string textOrEmpty(Statement& st, int idx) { return st.columnIsNull(idx) ? "" : st.columnText(idx); }

const std::vector<CategoriaSuprimentoInfo> kCategoriaCatalog = {
    {suprimento_categoria::kGestor, "Gestor"},
    {suprimento_categoria::kAssistente, "Assistente"},
    {suprimento_categoria::kAuxiliar, "Auxiliar"},
    {suprimento_categoria::kVistoriadorPredial, "Vistoriador predial"},
};

constexpr const char* kSuprimentoCols =
    "numero, id, nome, categoria, telefone, email, chave_pix, observacoes, created_at";

Suprimento rowToSuprimento(Statement& st) {
  Suprimento s;
  s.numero = static_cast<int>(st.columnDouble(0));
  s.id = st.columnText(1);
  s.nome = st.columnText(2);
  s.categoria = st.columnText(3);
  s.telefone = textOrEmpty(st, 4);
  s.email = textOrEmpty(st, 5);
  s.chavePix = textOrEmpty(st, 6);
  s.observacoes = textOrEmpty(st, 7);
  s.createdAt = st.columnText(8);
  return s;
}

void validateSuprimento(const Suprimento& input) {
  if (trim(input.nome).empty()) throw std::invalid_argument("informe o nome");
  if (!isKnownCategoriaSuprimento(input.categoria)) {
    throw std::invalid_argument("categoria inválida — selecione Gestor, Assistente, Auxiliar ou Vistoriador predial");
  }
}

}  // namespace

const std::vector<CategoriaSuprimentoInfo>& categoriaSuprimentoCatalog() { return kCategoriaCatalog; }

bool isKnownCategoriaSuprimento(const std::string& key) {
  return std::any_of(kCategoriaCatalog.begin(), kCategoriaCatalog.end(),
                     [&](const CategoriaSuprimentoInfo& c) { return c.key == key; });
}

std::string categoriaSuprimentoLabel(const std::string& key) {
  auto it = std::find_if(kCategoriaCatalog.begin(), kCategoriaCatalog.end(),
                         [&](const CategoriaSuprimentoInfo& c) { return c.key == key; });
  return it != kCategoriaCatalog.end() ? it->label : key;
}

std::vector<Suprimento> listSuprimentos(Database& db) {
  std::vector<Suprimento> out;
  auto st = db.prepare(std::string("SELECT ") + kSuprimentoCols + " FROM sos_suprimentos ORDER BY nome");
  while (st.step()) out.push_back(rowToSuprimento(st));
  return out;
}

std::optional<Suprimento> findSuprimento(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kSuprimentoCols + " FROM sos_suprimentos WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToSuprimento(st);
}

Suprimento createSuprimento(Database& db, const Suprimento& input) {
  validateSuprimento(input);

  auto st = db.prepare(
      "INSERT INTO sos_suprimentos (id, nome, categoria, telefone, email, chave_pix, observacoes, "
      "created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, trim(input.nome)).bind(3, input.categoria).bind(4, input.telefone);
  st.bind(5, input.email).bind(6, input.chavePix).bind(7, input.observacoes).bind(8, input.createdAt);
  st.step();

  return *findSuprimento(db, input.id);
}

Suprimento updateSuprimento(Database& db, const Suprimento& input) {
  if (!findSuprimento(db, input.id)) throw NotFoundError("suprimento não encontrado: " + input.id);
  validateSuprimento(input);

  auto st = db.prepare(
      "UPDATE sos_suprimentos SET nome=?, categoria=?, telefone=?, email=?, chave_pix=?, "
      "observacoes=? WHERE id=?");
  st.bind(1, trim(input.nome)).bind(2, input.categoria).bind(3, input.telefone).bind(4, input.email);
  st.bind(5, input.chavePix).bind(6, input.observacoes).bind(7, input.id);
  st.step();

  return *findSuprimento(db, input.id);
}

void deleteSuprimento(Database& db, const std::string& id) {
  if (!findSuprimento(db, id)) throw NotFoundError("suprimento não encontrado: " + id);
  db.prepare("DELETE FROM sos_suprimentos WHERE id=?").bind(1, id).step();
}

}  // namespace estoque
