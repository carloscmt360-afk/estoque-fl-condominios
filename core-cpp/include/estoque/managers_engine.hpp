#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/db.hpp"

// Gestão SOS: Gerentes e Carteiras.
//
//   Gerente ──< carteira (N condomínios) >── Condominio (dates_engine.hpp)
//
// A carteira é a lista de condomínios que um gerente atende — uma ligação N:N
// simples, no mesmo espírito de Empresa/Especialidade (companies_engine.hpp):
// um gerente pode ter quantos condomínios precisar, e um condomínio pode
// (em tese) aparecer na carteira de mais de um gerente. A tela de Carteiras
// mostra TODOS os condomínios cadastrados e marca/desmarca por clique — a
// escrita é sempre um "regrava a lista inteira" (ver writeCondominios), não um
// toggle unitário.
namespace estoque {

struct Gerente {
  std::string id;
  int numero = 0;  // "ID" que a tela mostra — nunca reciclado, mesmo critério do SKU de produtos
  std::string nome;
  std::string telefone;
  std::string email;
  std::string chavePix;
  std::string observacoes;
  std::string createdAt;
  std::vector<std::string> condominioIds;  // a carteira
};

// Próximo "ID" disponível para um gerente — usa e avança o contador
// persistido em app_settings ('gerentes_numero_seq'), nunca reciclado.
// Exposta (não só uso interno de createGerente) porque restoreFromJson
// (api.cpp) também precisa dela: um backup ANTERIOR a este campo não traz
// "numero" nenhum, e cada gerente sem número precisa ganhar um ao restaurar.
int nextGerenteNumero(Database& db);

std::vector<Gerente> listGerentes(Database& db);
std::optional<Gerente> findGerente(Database& db, const std::string& id);
Gerente createGerente(Database& db, const Gerente& input);
Gerente updateGerente(Database& db, const Gerente& input);
// A carteira dele some junto (ON DELETE CASCADE) — sem marcação órfã.
void deleteGerente(Database& db, const std::string& id);

}  // namespace estoque
