#include "shim.hpp"

#include <nlohmann/json.hpp>

// Aqui (uma unidade de tradução separada de lib.rs.h, não incluída por ele)
// não há circularidade: podemos incluir o header gerado normalmente para
// obter a definição completa de ProductDto/DepartmentDto.
#include "bridge/src/lib.rs.h"
#include "estoque/portable_paths.hpp"

namespace estoque::shim {

using json = nlohmann::json;

namespace {

Product toProduct(const ProductDto& d) {
  Product p;
  p.id = std::string(d.id);
  p.name = std::string(d.name);
  p.unit = std::string(d.unit);
  p.minStock = d.min_stock;
  p.category = std::string(d.category);
  p.qty = d.qty;
  p.avgCost = d.avg_cost;
  p.createdAt = std::string(d.created_at);
  return p;
}

Department toDepartment(const DepartmentDto& d) {
  Department dep;
  dep.id = std::string(d.id);
  dep.name = std::string(d.name);
  dep.encarregado = std::string(d.encarregado);
  dep.createdAt = std::string(d.created_at);
  return dep;
}

json productToJson(const Product& p) {
  json j;
  j["id"] = p.id;
  j["name"] = p.name;
  j["unit"] = p.unit;
  j["minStock"] = p.minStock;
  j["category"] = p.category;
  j["qty"] = p.qty;
  j["avgCost"] = p.avgCost;
  j["createdAt"] = p.createdAt;
  return j;
}

json departmentToJson(const Department& d) {
  json j;
  j["id"] = d.id;
  j["name"] = d.name;
  j["encarregado"] = d.encarregado;
  j["createdAt"] = d.createdAt;
  return j;
}

json movementToJson(const Movement& m) {
  json j;
  j["id"] = m.id;
  j["type"] = movementTypeToString(m.type);
  j["productId"] = m.productId;
  j["qty"] = m.qty;
  j["unitPrice"] = m.unitPrice;
  j["supplier"] = m.supplier;
  j["nf"] = m.nf;
  j["departmentId"] = m.departmentId;
  j["recipient"] = m.recipient;
  j["encarregado"] = m.encarregado;
  j["requester"] = m.requester;
  j["obs"] = m.obs;
  j["date"] = m.date;
  j["resultingQty"] = m.resultingQty;
  j["resultingAvgCost"] = m.resultingAvgCost;
  j["createdAt"] = m.createdAt;
  return j;
}

}  // namespace

Session::Session(const std::string& dbPath) : api_(dbPath) {}

rust::String Session::list_products_json() {
  json arr = json::array();
  for (auto& p : api_.listProducts()) arr.push_back(productToJson(p));
  return rust::String(arr.dump());
}

rust::String Session::create_product(ProductDto p) {
  return rust::String(productToJson(api_.createProduct(toProduct(p))).dump());
}

rust::String Session::update_product(ProductDto p) {
  return rust::String(productToJson(api_.updateProduct(toProduct(p))).dump());
}

void Session::delete_product(rust::Str id) { api_.deleteProduct(std::string(id)); }

rust::String Session::list_departments_json() {
  json arr = json::array();
  for (auto& d : api_.listDepartments()) arr.push_back(departmentToJson(d));
  return rust::String(arr.dump());
}

rust::String Session::create_department(DepartmentDto d) {
  return rust::String(departmentToJson(api_.createDepartment(toDepartment(d))).dump());
}

rust::String Session::update_department(DepartmentDto d) {
  return rust::String(departmentToJson(api_.updateDepartment(toDepartment(d))).dump());
}

void Session::delete_department(rust::Str id) { api_.deleteDepartment(std::string(id)); }

rust::String Session::apply_entrada(rust::Str movement_id, rust::Str product_id, double qty,
                                     double unit_price, rust::Str supplier, rust::Str nf, rust::Str date,
                                     rust::Str obs, rust::Str created_at) {
  auto m = api_.applyEntrada(std::string(movement_id), std::string(product_id), qty, unit_price,
                             std::string(supplier), std::string(nf), std::string(date), std::string(obs),
                             std::string(created_at));
  return rust::String(movementToJson(m).dump());
}

rust::String Session::apply_saida(rust::Str movement_id, rust::Str product_id, double qty,
                                   rust::Str department_id, rust::Str date, rust::Str obs,
                                   rust::Str requester, rust::Str created_at) {
  auto m = api_.applySaida(std::string(movement_id), std::string(product_id), qty,
                           std::string(department_id), std::string(date), std::string(obs),
                           std::string(requester), std::string(created_at));
  return rust::String(movementToJson(m).dump());
}

rust::String Session::apply_correcao(rust::Str movement_id, rust::Str product_id, double qty_real,
                                      rust::Str motivo, rust::Str date, rust::Str created_at) {
  auto m = api_.applyCorrecao(std::string(movement_id), std::string(product_id), qty_real,
                              std::string(motivo), std::string(date), std::string(created_at));
  return rust::String(movementToJson(m).dump());
}

rust::String Session::compute_report_json(int year, int month0, rust::Str dept_filter, int window_months,
                                           rust::Str now_iso) {
  return rust::String(api_.computeReportJson(year, month0, std::string(dept_filter), window_months,
                                              std::string(now_iso)));
}

rust::String Session::backup_json() { return rust::String(api_.backupJson()); }

void Session::restore_from_json(rust::Str payload) { api_.restoreFromJson(std::string(payload)); }

std::unique_ptr<Session> open_session(rust::Str db_path) {
  return std::make_unique<Session>(std::string(db_path));
}

rust::String resolve_data_dir_default() {
  auto result = estoque::portable_paths::resolveDataDir();
  if (!result.ok) throw std::runtime_error(result.error);
  return rust::String(result.dataDir.string());
}

}  // namespace estoque::shim
