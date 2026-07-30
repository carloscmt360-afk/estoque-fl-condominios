#include "estoque/inventory_engine.hpp"

#include <algorithm>

namespace estoque {

namespace {

Product rowToProduct(Statement& st) {
  Product p;
  p.id = st.columnText(0);
  p.name = st.columnText(1);
  p.unit = st.columnText(2);
  p.minStock = st.columnDouble(3);
  p.category = st.columnIsNull(4) ? "" : st.columnText(4);
  p.qty = st.columnDouble(5);
  p.avgCost = st.columnDouble(6);
  p.createdAt = st.columnText(7);
  return p;
}

Department rowToDepartment(Statement& st) {
  Department d;
  d.id = st.columnText(0);
  d.name = st.columnText(1);
  d.encarregado = st.columnIsNull(2) ? "" : st.columnText(2);
  d.createdAt = st.columnText(3);
  return d;
}

constexpr const char* kProductCols =
    "id, name, unit, min_stock, category, qty, avg_cost, created_at";
constexpr const char* kDepartmentCols = "id, name, encarregado, created_at";

}  // namespace

// -------------------------------------------------------------- Produtos

Product createProduct(Database& db, const Product& input) {
  auto st = db.prepare(
      "INSERT INTO products (id, name, unit, min_stock, category, qty, avg_cost, created_at) "
      "VALUES (?, ?, ?, ?, ?, 0, 0, ?)");
  st.bind(1, input.id).bind(2, input.name).bind(3, input.unit).bind(4, input.minStock);
  if (input.category.empty())
    st.bindNull(5);
  else
    st.bind(5, input.category);
  st.bind(6, input.createdAt);
  st.step();

  Product created = input;
  created.qty = 0;
  created.avgCost = 0;
  return created;
}

Product updateProduct(Database& db, const Product& input) {
  if (!findProduct(db, input.id)) {
    throw NotFoundError("produto não encontrado: " + input.id);
  }
  auto st = db.prepare("UPDATE products SET name=?, unit=?, min_stock=?, category=? WHERE id=?");
  st.bind(1, input.name).bind(2, input.unit).bind(3, input.minStock);
  if (input.category.empty())
    st.bindNull(4);
  else
    st.bind(4, input.category);
  st.bind(5, input.id);
  st.step();
  return *findProduct(db, input.id);
}

void deleteProduct(Database& db, const std::string& id) {
  Transaction tx(db);
  db.prepare("DELETE FROM movements WHERE product_id=?").bind(1, id).step();
  db.prepare("DELETE FROM products WHERE id=?").bind(1, id).step();
  tx.commit();
}

std::vector<Product> listProducts(Database& db) {
  std::vector<Product> out;
  auto st = db.prepare(std::string("SELECT ") + kProductCols + " FROM products ORDER BY name");
  while (st.step()) out.push_back(rowToProduct(st));
  return out;
}

std::optional<Product> findProduct(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kProductCols + " FROM products WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToProduct(st);
}

// ----------------------------------------------------------- Departamentos

Department createDepartment(Database& db, const Department& input) {
  auto st = db.prepare("INSERT INTO departments (id, name, encarregado, created_at) VALUES (?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, input.name);
  if (input.encarregado.empty())
    st.bindNull(3);
  else
    st.bind(3, input.encarregado);
  st.bind(4, input.createdAt);
  st.step();
  return input;
}

Department updateDepartment(Database& db, const Department& input) {
  if (!findDepartment(db, input.id)) {
    throw NotFoundError("departamento não encontrado: " + input.id);
  }
  auto st = db.prepare("UPDATE departments SET name=?, encarregado=? WHERE id=?");
  st.bind(1, input.name);
  if (input.encarregado.empty())
    st.bindNull(2);
  else
    st.bind(2, input.encarregado);
  st.bind(3, input.id);
  st.step();
  return *findDepartment(db, input.id);
}

void deleteDepartment(Database& db, const std::string& id) {
  // Espelha o app web: exclui o departamento, mas preserva o histórico de
  // saídas já registradas para ele (não apaga movements).
  db.prepare("DELETE FROM departments WHERE id=?").bind(1, id).step();
}

std::vector<Department> listDepartments(Database& db) {
  std::vector<Department> out;
  auto st = db.prepare(std::string("SELECT ") + kDepartmentCols + " FROM departments ORDER BY name");
  while (st.step()) out.push_back(rowToDepartment(st));
  return out;
}

std::optional<Department> findDepartment(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kDepartmentCols + " FROM departments WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToDepartment(st);
}

// -------------------------------------------------------------- Lançamentos

namespace {

void insertMovement(Database& db, const Movement& m) {
  auto st = db.prepare(
      "INSERT INTO movements (id, type, product_id, qty, unit_price, supplier, nf, department_id, "
      "recipient, encarregado, requester, obs, date, resulting_qty, resulting_avg_cost, created_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, m.id).bind(2, movementTypeToString(m.type)).bind(3, m.productId).bind(4, m.qty).bind(5, m.unitPrice);
  m.supplier.empty() ? st.bindNull(6) : st.bind(6, m.supplier);
  m.nf.empty() ? st.bindNull(7) : st.bind(7, m.nf);
  m.departmentId.empty() ? st.bindNull(8) : st.bind(8, m.departmentId);
  m.recipient.empty() ? st.bindNull(9) : st.bind(9, m.recipient);
  m.encarregado.empty() ? st.bindNull(10) : st.bind(10, m.encarregado);
  m.requester.empty() ? st.bindNull(11) : st.bind(11, m.requester);
  m.obs.empty() ? st.bindNull(12) : st.bind(12, m.obs);
  st.bind(13, m.date).bind(14, m.resultingQty).bind(15, m.resultingAvgCost).bind(16, m.createdAt);
  st.step();
}

}  // namespace

Movement applyEntrada(Database& db, const std::string& movementId, const std::string& productId,
                       double qty, double unitPrice, const std::string& supplier, const std::string& nf,
                       const std::string& date, const std::string& obs, const std::string& createdAt) {
  Transaction tx(db);
  auto product = findProduct(db, productId);
  if (!product) throw NotFoundError("produto não encontrado: " + productId);

  // Custo médio ponderado móvel. Saldo negativo (só ocorre em dado importado
  // malformado) NUNCA entra na ponderação — senão o custo médio resultante
  // fica distorcido ou negativo. Mesma proteção já validada na versão web.
  double baseQty = std::max(product->qty, 0.0);
  double newQty = product->qty + qty;
  double newAvg = (baseQty + qty) > 0 ? (baseQty * product->avgCost + qty * unitPrice) / (baseQty + qty) : 0.0;

  db.prepare("UPDATE products SET qty=?, avg_cost=? WHERE id=?").bind(1, newQty).bind(2, newAvg).bind(3, productId).step();

  Movement m;
  m.id = movementId;
  m.type = MovementType::Entrada;
  m.productId = productId;
  m.qty = qty;
  m.unitPrice = unitPrice;
  m.supplier = supplier;
  m.nf = nf;
  m.date = date;
  m.obs = obs;
  m.resultingQty = newQty;
  m.resultingAvgCost = newAvg;
  m.createdAt = createdAt;
  insertMovement(db, m);

  tx.commit();
  return m;
}

Movement applySaida(Database& db, const std::string& movementId, const std::string& productId, double qty,
                     const std::string& departmentId, const std::string& date, const std::string& obs,
                     const std::string& requester, const std::string& createdAt) {
  Transaction tx(db);
  auto product = findProduct(db, productId);
  if (!product) throw NotFoundError("produto não encontrado: " + productId);
  auto department = findDepartment(db, departmentId);
  if (!department) throw NotFoundError("departamento não encontrado: " + departmentId);

  // Saída não recalcula custo médio — apenas debita a quantidade e valoriza
  // ao custo médio vigente no momento (nunca ao preço de uma nota específica).
  double newQty = product->qty - qty;

  db.prepare("UPDATE products SET qty=? WHERE id=?").bind(1, newQty).bind(2, productId).step();

  Movement m;
  m.id = movementId;
  m.type = MovementType::Saida;
  m.productId = productId;
  m.qty = qty;
  m.unitPrice = product->avgCost;
  m.departmentId = departmentId;
  m.recipient = department->name;
  m.encarregado = department->encarregado;
  m.requester = requester;
  m.date = date;
  m.obs = obs;
  m.resultingQty = newQty;
  m.resultingAvgCost = product->avgCost;
  m.createdAt = createdAt;
  insertMovement(db, m);

  tx.commit();
  return m;
}

Movement applyCorrecao(Database& db, const std::string& movementId, const std::string& productId,
                        double qtyReal, const std::string& motivo, const std::string& date,
                        const std::string& createdAt) {
  Transaction tx(db);
  auto product = findProduct(db, productId);
  if (!product) throw NotFoundError("produto não encontrado: " + productId);

  // Ajuste de inventário: corrige o saldo para o que foi contado fisicamente;
  // custo médio não muda (não é uma compra nem uma venda).
  double delta = qtyReal - product->qty;

  db.prepare("UPDATE products SET qty=? WHERE id=?").bind(1, qtyReal).bind(2, productId).step();

  Movement m;
  m.id = movementId;
  m.type = MovementType::Ajuste;
  m.productId = productId;
  m.qty = delta;
  m.unitPrice = product->avgCost;
  m.obs = motivo;
  m.date = date;
  m.resultingQty = qtyReal;
  m.resultingAvgCost = product->avgCost;
  m.createdAt = createdAt;
  insertMovement(db, m);

  tx.commit();
  return m;
}

}  // namespace estoque
