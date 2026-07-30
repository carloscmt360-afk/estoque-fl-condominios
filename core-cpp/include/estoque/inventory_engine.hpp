#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/db.hpp"
#include "estoque/models.hpp"

// Motor de CRUD + custo médio ponderado móvel. Nenhuma função aqui toca
// relógio de parede ou gera aleatoriedade: id e timestamp de criação são
// sempre passados pelo chamador (a ponte cxx os gera do lado Rust) — isso
// mantém o núcleo determinístico e barato de testar.
namespace estoque {

// ---- Produtos ----
// `input` já deve trazer id/createdAt preenchidos pelo chamador; qty/avgCost
// são sempre forçados a 0 na criação (entram por uma entrada explícita).
Product createProduct(Database& db, const Product& input);
// Atualiza name/unit/minStock/category; nunca mexe em qty/avgCost (esses só
// mudam via applyEntrada/applySaida/applyCorrecao).
Product updateProduct(Database& db, const Product& input);
void deleteProduct(Database& db, const std::string& id);  // remove também as movimentações do produto
std::vector<Product> listProducts(Database& db);
std::optional<Product> findProduct(Database& db, const std::string& id);

// ---- Departamentos ----
Department createDepartment(Database& db, const Department& input);
Department updateDepartment(Database& db, const Department& input);
void deleteDepartment(Database& db, const std::string& id);
std::vector<Department> listDepartments(Database& db);
std::optional<Department> findDepartment(Database& db, const std::string& id);

// ---- Lançamentos ----
// Cada uma persiste o produto atualizado + insere a movimentação numa única
// transação (commit só no fim; qualquer exceção reverte as duas). Lança
// NotFoundError se productId/departmentId não existir.

Movement applyEntrada(Database& db, const std::string& movementId, const std::string& productId,
                       double qty, double unitPrice, const std::string& supplier, const std::string& nf,
                       const std::string& date, const std::string& obs, const std::string& createdAt);

Movement applySaida(Database& db, const std::string& movementId, const std::string& productId, double qty,
                     const std::string& departmentId, const std::string& date, const std::string& obs,
                     const std::string& requester, const std::string& createdAt);

Movement applyCorrecao(Database& db, const std::string& movementId, const std::string& productId,
                        double qtyReal, const std::string& motivo, const std::string& date,
                        const std::string& createdAt);

}  // namespace estoque
