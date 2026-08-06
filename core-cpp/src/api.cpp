#include "estoque/api.hpp"

#include <nlohmann/json.hpp>

#include "estoque/inventory_engine.hpp"
#include "estoque/report_engine.hpp"
#include "estoque/retrospect_engine.hpp"

namespace estoque {

using json = nlohmann::json;

namespace {

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
  j["monthlyLimit"] = d.monthlyLimit;
  j["createdAt"] = d.createdAt;
  return j;
}

// `key` já resolvida (evita repetir count.contains(...) ? ... em cada chamada)
std::string jstr(const json& j, const char* key, const std::string& def = "") {
  return (j.contains(key) && !j[key].is_null()) ? j[key].get<std::string>() : def;
}
double jnum(const json& j, const char* key, double def = 0.0) {
  return (j.contains(key) && !j[key].is_null()) ? j[key].get<double>() : def;
}

}  // namespace

Api::Api(const std::string& dbPath) : db_(dbPath) {}

std::vector<Product> Api::listProducts() { return estoque::listProducts(db_); }
Product Api::createProduct(const Product& input) { return estoque::createProduct(db_, input); }
Product Api::updateProduct(const Product& input) { return estoque::updateProduct(db_, input); }
void Api::deleteProduct(const std::string& id) { estoque::deleteProduct(db_, id); }

std::vector<Department> Api::listDepartments() { return estoque::listDepartments(db_); }
Department Api::createDepartment(const Department& input) { return estoque::createDepartment(db_, input); }
Department Api::updateDepartment(const Department& input) { return estoque::updateDepartment(db_, input); }
void Api::deleteDepartment(const std::string& id) { estoque::deleteDepartment(db_, id); }

Movement Api::applyEntrada(const std::string& movementId, const std::string& productId, double qty,
                           double unitPrice, const std::string& supplier, const std::string& nf,
                           const std::string& date, const std::string& obs, const std::string& createdAt) {
  return estoque::applyEntrada(db_, movementId, productId, qty, unitPrice, supplier, nf, date, obs, createdAt);
}

Movement Api::applySaida(const std::string& movementId, const std::string& productId, double qty,
                         const std::string& departmentId, const std::string& date, const std::string& obs,
                         const std::string& requester, const std::string& createdAt) {
  return estoque::applySaida(db_, movementId, productId, qty, departmentId, date, obs, requester, createdAt);
}

Movement Api::applyCorrecao(const std::string& movementId, const std::string& productId, double qtyReal,
                            const std::string& motivo, const std::string& date, const std::string& createdAt) {
  return estoque::applyCorrecao(db_, movementId, productId, qtyReal, motivo, date, createdAt);
}

std::vector<Movement> Api::listMovements() { return estoque::listMovements(db_); }

Movement Api::updateMovement(const MovementPatch& patch) { return estoque::updateMovement(db_, patch); }

void Api::deleteMovement(const std::string& id) { estoque::deleteMovement(db_, id); }

std::string Api::computeReportJson(int year, int month0, const std::string& deptFilter, int windowMonths,
                                   const std::string& nowIso) {
  ReportParams params{year, month0, deptFilter, windowMonths, nowIso};
  return estoque::computeReportJson(db_, params);
}

std::string Api::computeRetrospectJson(int year, const std::string& source, const std::string& nowIso) {
  RetrospectParams params{year, source, nowIso};
  return estoque::computeRetrospectJson(db_, params);
}

void Api::saveBudgetParams(double metaReducao, double ipca, double pisoMensal) {
  estoque::saveBudgetParams(db_, BudgetParams{metaReducao, ipca, pisoMensal});
}

int Api::importDeptCostHistory(const std::string& payload) {
  return estoque::importDeptCostHistoryJson(db_, payload);
}

std::string Api::backupJson() {
  json products = json::array();
  for (auto& p : listProducts()) products.push_back(productToJson(p));

  json departments = json::array();
  for (auto& d : listDepartments()) departments.push_back(departmentToJson(d));

  json movements = json::array();
  auto st = db_.prepare(
      "SELECT id, type, product_id, qty, unit_price, supplier, nf, department_id, recipient, "
      "encarregado, requester, obs, date, resulting_qty, resulting_avg_cost, created_at "
      "FROM movements ORDER BY date, created_at");
  while (st.step()) {
    json m;
    m["id"] = st.columnText(0);
    m["type"] = st.columnText(1);
    m["productId"] = st.columnText(2);
    m["qty"] = st.columnDouble(3);
    m["unitPrice"] = st.columnIsNull(4) ? json(nullptr) : json(st.columnDouble(4));
    m["supplier"] = st.columnIsNull(5) ? "" : st.columnText(5);
    m["nf"] = st.columnIsNull(6) ? "" : st.columnText(6);
    m["departmentId"] = st.columnIsNull(7) ? "" : st.columnText(7);
    m["recipient"] = st.columnIsNull(8) ? "" : st.columnText(8);
    m["encarregado"] = st.columnIsNull(9) ? "" : st.columnText(9);
    m["requester"] = st.columnIsNull(10) ? "" : st.columnText(10);
    m["obs"] = st.columnIsNull(11) ? "" : st.columnText(11);
    m["date"] = st.columnText(12);
    m["resultingQty"] = st.columnDouble(13);
    m["resultingAvgCost"] = st.columnDouble(14);
    m["createdAt"] = st.columnText(15);
    movements.push_back(m);
  }

  json settings = json::object();
  {
    auto sq = db_.prepare("SELECT key, value FROM app_settings");
    while (sq.step()) settings[sq.columnText(0)] = sq.columnText(1);
  }

  json root;
  root["products"] = products;
  root["movements"] = movements;
  root["departments"] = departments;
  root["deptCostHistory"] = json::parse(estoque::exportDeptCostHistoryJson(db_));
  root["settings"] = settings;
  return root.dump();
}

void Api::restoreFromJson(const std::string& payload) {
  json root = json::parse(payload);
  if (!root.contains("products") || !root.contains("movements")) {
    throw std::runtime_error("backup inválido: faltam as chaves 'products'/'movements'");
  }

  Transaction tx(db_);
  db_.execute("DELETE FROM movements;");
  db_.execute("DELETE FROM products;");
  db_.execute("DELETE FROM departments;");
  // Histórico e preferências só são zerados se o backup os trouxer: importar
  // um backup antigo (anterior ao retrospecto) não pode apagar em silêncio o
  // histórico da planilha nem o teto já configurado.
  if (root.contains("deptCostHistory")) db_.execute("DELETE FROM dept_cost_history;");
  if (root.contains("settings")) db_.execute("DELETE FROM app_settings;");

  for (auto& p : root["products"]) {
    auto st = db_.prepare(
        "INSERT INTO products (id, name, unit, min_stock, category, qty, avg_cost, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    st.bind(1, jstr(p, "id")).bind(2, jstr(p, "name")).bind(3, jstr(p, "unit"));
    st.bind(4, jnum(p, "minStock")).bind(5, jstr(p, "category"));
    st.bind(6, jnum(p, "qty")).bind(7, jnum(p, "avgCost")).bind(8, jstr(p, "createdAt"));
    st.step();
  }

  if (root.contains("departments")) {
    for (auto& d : root["departments"]) {
      auto st = db_.prepare(
          "INSERT INTO departments (id, name, encarregado, monthly_limit, created_at) VALUES (?, ?, ?, ?, ?)");
      st.bind(1, jstr(d, "id")).bind(2, jstr(d, "name")).bind(3, jstr(d, "encarregado"));
      st.bind(4, jnum(d, "monthlyLimit")).bind(5, jstr(d, "createdAt"));
      st.step();
    }
  }

  if (root.contains("deptCostHistory")) {
    for (auto& h : root["deptCostHistory"]) {
      std::string key = canonDeptKey(jstr(h, "dept"));
      if (key.empty()) continue;
      auto st = db_.prepare(
          "INSERT INTO dept_cost_history (year, month0, dept_key, dept_name, amount) VALUES (?, ?, ?, ?, ?) "
          "ON CONFLICT(year, month0, dept_key) DO UPDATE SET amount = amount + excluded.amount");
      st.bind(1, jnum(h, "year")).bind(2, jnum(h, "month0"));
      st.bind(3, key).bind(4, jstr(h, "dept")).bind(5, jnum(h, "amount"));
      st.step();
    }
  }

  if (root.contains("settings")) {
    for (auto& [k, v] : root["settings"].items()) {
      auto st = db_.prepare("INSERT INTO app_settings (key, value) VALUES (?, ?)");
      st.bind(1, k).bind(2, v.is_string() ? v.get<std::string>() : v.dump());
      st.step();
    }
  }

  for (auto& m : root["movements"]) {
    auto st = db_.prepare(
        "INSERT INTO movements (id, type, product_id, qty, unit_price, supplier, nf, department_id, "
        "recipient, encarregado, requester, obs, date, resulting_qty, resulting_avg_cost, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    st.bind(1, jstr(m, "id")).bind(2, jstr(m, "type")).bind(3, jstr(m, "productId"));
    st.bind(4, jnum(m, "qty")).bind(5, jnum(m, "unitPrice"));
    st.bind(6, jstr(m, "supplier")).bind(7, jstr(m, "nf")).bind(8, jstr(m, "departmentId"));
    st.bind(9, jstr(m, "recipient")).bind(10, jstr(m, "encarregado")).bind(11, jstr(m, "requester"));
    st.bind(12, jstr(m, "obs")).bind(13, jstr(m, "date"));
    st.bind(14, jnum(m, "resultingQty")).bind(15, jnum(m, "resultingAvgCost")).bind(16, jstr(m, "createdAt"));
    st.step();
  }

  tx.commit();
}

}  // namespace estoque
