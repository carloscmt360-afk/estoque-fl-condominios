#pragma once
#include <string>
#include <vector>

#include "estoque/db.hpp"
#include "estoque/inventory_engine.hpp"  // MovementPatch
#include "estoque/models.hpp"

// Fachada única do domínio: junta banco + inventory_engine + report_engine
// + backup/restore num objeto com estado (a conexão SQLite). É o que a
// ponte cxx embrulha (ver bridge/cpp/shim.hpp) — mas esta classe em si não
// sabe que `cxx` existe, então continua 100% testável sozinha (doctest),
// com os mesmos tipos simples usados no resto do core-cpp.
namespace estoque {

class Api {
 public:
  explicit Api(const std::string& dbPath);

  std::vector<Product> listProducts();
  Product createProduct(const Product& input);
  Product updateProduct(const Product& input);
  void deleteProduct(const std::string& id);

  std::vector<Department> listDepartments();
  Department createDepartment(const Department& input);
  Department updateDepartment(const Department& input);
  void deleteDepartment(const std::string& id);

  Movement applyEntrada(const std::string& movementId, const std::string& productId, double qty,
                        double unitPrice, const std::string& supplier, const std::string& nf,
                        const std::string& date, const std::string& obs, const std::string& createdAt);
  Movement applySaida(const std::string& movementId, const std::string& productId, double qty,
                      const std::string& departmentId, const std::string& date, const std::string& obs,
                      const std::string& requester, const std::string& createdAt);
  Movement applyCorrecao(const std::string& movementId, const std::string& productId, double qtyReal,
                        const std::string& motivo, const std::string& date, const std::string& createdAt);

  std::vector<Movement> listMovements();
  Movement updateMovement(const MovementPatch& patch);
  void deleteMovement(const std::string& id);

  std::string computeReportJson(int year, int month0, const std::string& deptFilter, int windowMonths,
                                const std::string& nowIso);

  // Retrospecto anual (matriz departamento × mês, totais do ano, comparativo
  // e teto de gastos) — ver retrospect_engine.hpp.
  std::string computeRetrospectJson(int year, const std::string& source, const std::string& nowIso);
  void saveBudgetParams(double metaReducao, double ipca, double pisoMensal);
  int importDeptCostHistory(const std::string& payload);

  // JSON no MESMO formato do antigo localStorage (products/movements/departments
  // com as mesmas chaves camelCase) — permite importar os backups já existentes
  // (ex.: carga_inicial_ref_jun26.json) sem nenhuma conversão. As chaves
  // `deptCostHistory` e `settings` são acréscimos posteriores: backups antigos
  // que não as tenham continuam válidos (ver restoreFromJson).
  std::string backupJson();

  // Substitui TODOS os dados atuais pelo conteúdo do payload — mesma
  // semântica do antigo "Importar Backup" (state = data; sem reprocessar
  // pelas regras de negócio, preserva os valores exatamente como estavam
  // gravados: qty/avgCost/resultingQty/resultingAvgCost não são recalculados).
  void restoreFromJson(const std::string& payload);

 private:
  Database db_;
};

}  // namespace estoque
