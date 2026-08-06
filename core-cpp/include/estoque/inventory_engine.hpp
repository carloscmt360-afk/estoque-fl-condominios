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
//
// Todas reprocessam o produto ao final (ver recomputeProduct): a posição
// resultante nunca depende da ORDEM DE DIGITAÇÃO, só da ordem cronológica —
// um lançamento retroativo entra no lugar certo da linha do tempo e os
// lançamentos posteriores são revalorizados.

Movement applyEntrada(Database& db, const std::string& movementId, const std::string& productId,
                       double qty, double unitPrice, const std::string& supplier, const std::string& nf,
                       const std::string& date, const std::string& obs, const std::string& createdAt);

Movement applySaida(Database& db, const std::string& movementId, const std::string& productId, double qty,
                     const std::string& departmentId, const std::string& date, const std::string& obs,
                     const std::string& requester, const std::string& createdAt);

// `qtyReal` é a quantidade CONTADA fisicamente; o que se grava em
// movements.qty é o delta contra o saldo vigente NA DATA do ajuste (não
// contra o saldo de hoje) — é isso que faz um ajuste retroativo continuar
// coerente depois que outro lançamento é editado antes dele.
Movement applyCorrecao(Database& db, const std::string& movementId, const std::string& productId,
                        double qtyReal, const std::string& motivo, const std::string& date,
                        const std::string& createdAt);

// ---- Consulta de lançamentos ----
std::vector<Movement> listMovements(Database& db);  // ordem cronológica (date, created_at)
std::optional<Movement> findMovement(Database& db, const std::string& id);

// ---- Edição e exclusão de lançamentos ----

// Campos editáveis de um lançamento já gravado. O TIPO e o PRODUTO são
// imutáveis de propósito: entrada/saída/ajuste carregam campos e semântica
// diferentes (uma entrada tem fornecedor e NF e forma o custo médio; uma
// saída tem departamento e o consome), e trocar o produto exigiria
// reprocessar dois produtos para desfazer/refazer efeitos financeiros em
// cadeia. Para esses casos: exclua o lançamento e registre outro.
struct MovementPatch {
  std::string id;
  double qty = 0;        // entrada/saída: quantidade movimentada (> 0)
  double qtyReal = 0;    // ajuste: quantidade CONTADA (o delta é derivado dela)
  double unitPrice = 0;  // entrada: preço de compra · saída: preço de valoração gravado
  std::string supplier;  // entrada
  std::string nf;        // entrada
  std::string departmentId;  // saída (redenormaliza recipient/encarregado)
  std::string requester;     // saída
  std::string obs;           // todos — no ajuste, é o motivo
  std::string date;
};

// Lança NotFoundError (lançamento/departamento inexistente) ou
// std::invalid_argument (quantidade não positiva em entrada/saída).
Movement updateMovement(Database& db, const MovementPatch& patch);

void deleteMovement(Database& db, const std::string& id);

// Reprocessa a linha do tempo INTEIRA de um produto em ordem cronológica,
// regravando resulting_qty/resulting_avg_cost de cada lançamento e a posição
// do produto. Mesma aritmética do report_engine, para que os dois nunca
// divirjam sobre os mesmos lançamentos.
//
// `offsetQty` preserva a diferença que JÁ EXISTIA entre o saldo gravado no
// produto e o que o razão reproduz — nos dados reais do usuário 13 de 157
// produtos divergem (a planilha de origem tem saídas sem a entrada
// correspondente), somando R$ 8,2 mil. Sem esse offset, editar a observação
// de um lançamento qualquer de PILHA AA saltaria o estoque dela de 21 para
// 101 unidades. Com ele, uma edição move o saldo exatamente pelo efeito da
// edição — nada mais. Os chamadores capturam o offset ANTES de mexer no
// razão (ver ledgerOffset, no .cpp).
void recomputeProduct(Database& db, const std::string& productId, double offsetQty);

}  // namespace estoque
