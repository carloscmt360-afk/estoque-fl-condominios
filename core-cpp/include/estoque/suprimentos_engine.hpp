#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/db.hpp"

// Gestão SOS: Suprimentos — a equipe de campo, por categoria de função.
// Diferente de Gerente (que carrega uma carteira de condomínios), aqui não
// há vínculo nenhum: é só o cadastro de contato + Chave PIX de cada pessoa.
namespace estoque {

namespace suprimento_categoria {
constexpr const char* kGestor = "gestor";
constexpr const char* kAssistente = "assistente";
constexpr const char* kAuxiliar = "auxiliar";
constexpr const char* kVistoriadorPredial = "vistoriador_predial";
}  // namespace suprimento_categoria

struct CategoriaSuprimentoInfo {
  std::string key;
  std::string label;
};
// Na ordem em que a tela mostra as opções.
const std::vector<CategoriaSuprimentoInfo>& categoriaSuprimentoCatalog();
bool isKnownCategoriaSuprimento(const std::string& key);
std::string categoriaSuprimentoLabel(const std::string& key);

struct Suprimento {
  std::string id;
  int numero = 0;  // "ID" que a tela mostra — AUTOINCREMENT, nunca reciclado (mesmo critério de Servico)
  std::string nome;
  std::string categoria;  // suprimento_categoria::*
  std::string telefone;
  std::string email;
  std::string chavePix;
  std::string observacoes;
  std::string createdAt;
};

std::vector<Suprimento> listSuprimentos(Database& db);
std::optional<Suprimento> findSuprimento(Database& db, const std::string& id);
Suprimento createSuprimento(Database& db, const Suprimento& input);
Suprimento updateSuprimento(Database& db, const Suprimento& input);
void deleteSuprimento(Database& db, const std::string& id);

}  // namespace estoque
