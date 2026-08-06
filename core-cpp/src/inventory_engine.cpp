#include "estoque/inventory_engine.hpp"

#include <algorithm>
#include <stdexcept>

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
  d.monthlyLimit = st.columnIsNull(3) ? 0.0 : st.columnDouble(3);
  d.createdAt = st.columnText(4);
  return d;
}

constexpr const char* kProductCols =
    "id, name, unit, min_stock, category, qty, avg_cost, created_at";
constexpr const char* kDepartmentCols = "id, name, encarregado, monthly_limit, created_at";
constexpr const char* kMovementCols =
    "id, type, product_id, qty, unit_price, supplier, nf, department_id, recipient, encarregado, "
    "requester, obs, date, resulting_qty, resulting_avg_cost, created_at";

Movement rowToMovement(Statement& st) {
  Movement m;
  m.id = st.columnText(0);
  m.type = movementTypeFromString(st.columnText(1));
  m.productId = st.columnText(2);
  m.qty = st.columnDouble(3);
  m.unitPrice = st.columnIsNull(4) ? 0.0 : st.columnDouble(4);
  m.supplier = st.columnIsNull(5) ? "" : st.columnText(5);
  m.nf = st.columnIsNull(6) ? "" : st.columnText(6);
  m.departmentId = st.columnIsNull(7) ? "" : st.columnText(7);
  m.recipient = st.columnIsNull(8) ? "" : st.columnText(8);
  m.encarregado = st.columnIsNull(9) ? "" : st.columnText(9);
  m.requester = st.columnIsNull(10) ? "" : st.columnText(10);
  m.obs = st.columnIsNull(11) ? "" : st.columnText(11);
  m.date = st.columnText(12);
  m.resultingQty = st.columnIsNull(13) ? 0.0 : st.columnDouble(13);
  m.resultingAvgCost = st.columnIsNull(14) ? 0.0 : st.columnDouble(14);
  m.createdAt = st.columnText(15);
  return m;
}

// Posição de um produto durante o replay do razão.
struct Position {
  double qty = 0;
  double avgCost = 0;
};

// Um passo do replay — ESTA é a aritmética canônica do custo médio ponderado
// móvel, idêntica à de report_engine.cpp::applyToAcc. Se algum dia mudar,
// muda nos dois lugares (há teste cruzando os dois resultados).
void step(Position& s, const std::string& type, double qty, double unitPrice) {
  if (type == "entrada") {
    double baseQty = std::max(s.qty, 0.0);  // saldo negativo nunca entra na ponderação
    double novaQty = s.qty + qty;
    s.avgCost = (baseQty + qty) > 0 ? (baseQty * s.avgCost + qty * unitPrice) / (baseQty + qty) : 0.0;
    s.qty = novaQty;
  } else if (type == "saida") {
    s.qty -= qty;
  } else {
    s.qty += qty;  // ajuste: qty já é o delta
  }
}

// Replay do razão inteiro do produto (todos os lançamentos, do zero).
Position replayLedger(Database& db, const std::string& productId) {
  Position s;
  auto st = db.prepare("SELECT type, qty, unit_price FROM movements WHERE product_id=? ORDER BY date, created_at");
  st.bind(1, productId);
  while (st.step()) {
    step(s, st.columnText(0), st.columnDouble(1), st.columnIsNull(2) ? 0.0 : st.columnDouble(2));
  }
  return s;
}

// Diferença pré-existente entre o saldo GRAVADO no produto e o que o razão
// reproduz. Precisa ser lida ANTES de qualquer alteração no razão. Ver o
// comentário longo de recomputeProduct no header para o porquê.
double ledgerOffset(Database& db, const Product& stored) {
  return stored.qty - replayLedger(db, stored.id).qty;
}

// Posição do produto imediatamente ANTES de uma posição (date, createdAt) na
// linha do tempo, ignorando um lançamento específico (o próprio, quando se
// está editando). A comparação de tupla espelha o ORDER BY date, created_at:
// nosso ISO 8601 tem largura fixa, então ordem lexicográfica == cronológica.
Position positionBefore(Database& db, const std::string& productId, const std::string& date,
                        const std::string& createdAt, const std::string& excludeMovementId,
                        double offsetQty) {
  Position s;
  auto st = db.prepare(
      "SELECT type, qty, unit_price FROM movements "
      "WHERE product_id=? AND id<>? AND (date < ? OR (date = ? AND created_at < ?)) "
      "ORDER BY date, created_at");
  st.bind(1, productId).bind(2, excludeMovementId).bind(3, date).bind(4, date).bind(5, createdAt);
  while (st.step()) {
    step(s, st.columnText(0), st.columnDouble(1), st.columnIsNull(2) ? 0.0 : st.columnDouble(2));
  }
  s.qty += offsetQty;  // saldo comparável ao que o usuário vê no produto
  return s;
}

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
  auto st = db.prepare(
      "INSERT INTO departments (id, name, encarregado, monthly_limit, created_at) VALUES (?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, input.name);
  if (input.encarregado.empty())
    st.bindNull(3);
  else
    st.bind(3, input.encarregado);
  // Limite negativo não tem significado (o zero já é "sem limite") — normaliza
  // aqui para o motor de alertas nunca ter que se defender disso.
  st.bind(4, std::max(0.0, input.monthlyLimit));
  st.bind(5, input.createdAt);
  st.step();
  return *findDepartment(db, input.id);
}

Department updateDepartment(Database& db, const Department& input) {
  if (!findDepartment(db, input.id)) {
    throw NotFoundError("departamento não encontrado: " + input.id);
  }
  auto st = db.prepare("UPDATE departments SET name=?, encarregado=?, monthly_limit=? WHERE id=?");
  st.bind(1, input.name);
  if (input.encarregado.empty())
    st.bindNull(2);
  else
    st.bind(2, input.encarregado);
  st.bind(3, std::max(0.0, input.monthlyLimit));
  st.bind(4, input.id);
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

void recomputeProduct(Database& db, const std::string& productId, double offsetQty) {
  struct Row {
    std::string id, type;
    double qty = 0, unitPrice = 0;
  };
  std::vector<Row> rows;
  {
    auto st = db.prepare(
        "SELECT id, type, qty, unit_price FROM movements WHERE product_id=? ORDER BY date, created_at");
    st.bind(1, productId);
    while (st.step()) {
      Row r;
      r.id = st.columnText(0);
      r.type = st.columnText(1);
      r.qty = st.columnDouble(2);
      r.unitPrice = st.columnIsNull(3) ? 0.0 : st.columnDouble(3);
      rows.push_back(std::move(r));
    }
  }

  Position s;
  for (const auto& r : rows) {
    step(s, r.type, r.qty, r.unitPrice);

    // unit_price só é REESCRITO no ajuste, onde ele é pura valoração derivada
    // (o ajuste não tem preço próprio — vale o custo médio do momento).
    // Na entrada é o preço de compra real e na saída é o preço gravado no
    // lançamento — este último vem da planilha de origem nos dados
    // importados e alimenta a conferência "custo médio × preço da saída" do
    // relatório; reescrevê-lo apagaria justamente o que ela existe para
    // detectar.
    double unitPrice = r.type == "ajuste" ? s.avgCost : r.unitPrice;

    db.prepare("UPDATE movements SET resulting_qty=?, resulting_avg_cost=?, unit_price=? WHERE id=?")
        .bind(1, s.qty + offsetQty)
        .bind(2, s.avgCost)
        .bind(3, unitPrice)
        .bind(4, r.id)
        .step();
  }

  db.prepare("UPDATE products SET qty=?, avg_cost=? WHERE id=?")
      .bind(1, s.qty + offsetQty)
      .bind(2, s.avgCost)
      .bind(3, productId)
      .step();
}

Movement applyEntrada(Database& db, const std::string& movementId, const std::string& productId,
                       double qty, double unitPrice, const std::string& supplier, const std::string& nf,
                       const std::string& date, const std::string& obs, const std::string& createdAt) {
  Transaction tx(db);
  auto product = findProduct(db, productId);
  if (!product) throw NotFoundError("produto não encontrado: " + productId);
  double offset = ledgerOffset(db, *product);

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
  m.createdAt = createdAt;
  insertMovement(db, m);

  recomputeProduct(db, productId, offset);
  tx.commit();
  return *findMovement(db, movementId);
}

Movement applySaida(Database& db, const std::string& movementId, const std::string& productId, double qty,
                     const std::string& departmentId, const std::string& date, const std::string& obs,
                     const std::string& requester, const std::string& createdAt) {
  Transaction tx(db);
  auto product = findProduct(db, productId);
  if (!product) throw NotFoundError("produto não encontrado: " + productId);
  auto department = findDepartment(db, departmentId);
  if (!department) throw NotFoundError("departamento não encontrado: " + departmentId);
  double offset = ledgerOffset(db, *product);

  Movement m;
  m.id = movementId;
  m.type = MovementType::Saida;
  m.productId = productId;
  m.qty = qty;
  // Saída é valorizada ao custo médio vigente NA DATA dela (não ao de hoje),
  // para que uma saída retroativa não seja avaliada por compras posteriores.
  m.unitPrice = positionBefore(db, productId, date, createdAt, movementId, offset).avgCost;
  m.departmentId = departmentId;
  m.recipient = department->name;
  m.encarregado = department->encarregado;
  m.requester = requester;
  m.date = date;
  m.obs = obs;
  m.createdAt = createdAt;
  insertMovement(db, m);

  recomputeProduct(db, productId, offset);
  tx.commit();
  return *findMovement(db, movementId);
}

Movement applyCorrecao(Database& db, const std::string& movementId, const std::string& productId,
                        double qtyReal, const std::string& motivo, const std::string& date,
                        const std::string& createdAt) {
  Transaction tx(db);
  auto product = findProduct(db, productId);
  if (!product) throw NotFoundError("produto não encontrado: " + productId);
  double offset = ledgerOffset(db, *product);

  // Ajuste de inventário: corrige o saldo para o que foi contado fisicamente;
  // custo médio não muda (não é uma compra nem uma venda). O delta é contra o
  // saldo vigente NA DATA do ajuste — num ajuste lançado hoje isso é o saldo
  // atual (comportamento de sempre), num retroativo é o saldo daquele dia.
  Position before = positionBefore(db, productId, date, createdAt, movementId, offset);

  Movement m;
  m.id = movementId;
  m.type = MovementType::Ajuste;
  m.productId = productId;
  m.qty = qtyReal - before.qty;
  m.unitPrice = before.avgCost;
  m.obs = motivo;
  m.date = date;
  m.createdAt = createdAt;
  insertMovement(db, m);

  recomputeProduct(db, productId, offset);
  tx.commit();
  return *findMovement(db, movementId);
}

// ------------------------------------------- Consulta / edição / exclusão

std::vector<Movement> listMovements(Database& db) {
  std::vector<Movement> out;
  auto st = db.prepare(std::string("SELECT ") + kMovementCols + " FROM movements ORDER BY date, created_at");
  while (st.step()) out.push_back(rowToMovement(st));
  return out;
}

std::optional<Movement> findMovement(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kMovementCols + " FROM movements WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToMovement(st);
}

Movement updateMovement(Database& db, const MovementPatch& patch) {
  Transaction tx(db);
  auto existing = findMovement(db, patch.id);
  if (!existing) throw NotFoundError("lançamento não encontrado: " + patch.id);
  auto product = findProduct(db, existing->productId);
  if (!product) throw NotFoundError("produto não encontrado: " + existing->productId);
  double offset = ledgerOffset(db, *product);

  const std::string date = patch.date.empty() ? existing->date : patch.date;

  double qty = patch.qty;
  double unitPrice = patch.unitPrice;
  std::string supplier = patch.supplier;
  std::string nf = patch.nf;
  std::string departmentId = existing->departmentId;
  std::string recipient = existing->recipient;
  std::string encarregado = existing->encarregado;
  std::string requester = patch.requester;

  if (existing->type == MovementType::Entrada) {
    if (!(qty > 0)) throw std::invalid_argument("a quantidade da entrada deve ser maior que zero");
    if (unitPrice < 0) throw std::invalid_argument("o preço unitário não pode ser negativo");
    departmentId.clear();
    recipient.clear();
    encarregado.clear();
    requester.clear();
  } else if (existing->type == MovementType::Saida) {
    if (!(qty > 0)) throw std::invalid_argument("a quantidade da saída deve ser maior que zero");
    if (unitPrice < 0) throw std::invalid_argument("o preço unitário não pode ser negativo");
    auto department = findDepartment(db, patch.departmentId);
    if (!department) throw NotFoundError("departamento não encontrado: " + patch.departmentId);
    // Redenormaliza: o histórico passa a apontar para o departamento escolhido
    // AGORA, com o nome/encarregado que ele tem agora.
    departmentId = department->id;
    recipient = department->name;
    encarregado = department->encarregado;
    supplier.clear();
    nf.clear();
  } else {
    // Ajuste: o usuário informa a quantidade CONTADA; o delta é derivado
    // contra o saldo vigente na (possivelmente nova) data — ignorando este
    // próprio lançamento, que está sendo reposicionado.
    Position before = positionBefore(db, existing->productId, date, existing->createdAt, existing->id, offset);
    qty = patch.qtyReal - before.qty;
    unitPrice = before.avgCost;  // recomputeProduct reescreve mesmo assim
    supplier.clear();
    nf.clear();
    departmentId.clear();
    recipient.clear();
    encarregado.clear();
    requester.clear();
  }

  auto st = db.prepare(
      "UPDATE movements SET qty=?, unit_price=?, supplier=?, nf=?, department_id=?, recipient=?, "
      "encarregado=?, requester=?, obs=?, date=? WHERE id=?");
  st.bind(1, qty).bind(2, unitPrice);
  supplier.empty() ? st.bindNull(3) : st.bind(3, supplier);
  nf.empty() ? st.bindNull(4) : st.bind(4, nf);
  departmentId.empty() ? st.bindNull(5) : st.bind(5, departmentId);
  recipient.empty() ? st.bindNull(6) : st.bind(6, recipient);
  encarregado.empty() ? st.bindNull(7) : st.bind(7, encarregado);
  requester.empty() ? st.bindNull(8) : st.bind(8, requester);
  patch.obs.empty() ? st.bindNull(9) : st.bind(9, patch.obs);
  st.bind(10, date).bind(11, patch.id);
  st.step();

  recomputeProduct(db, existing->productId, offset);
  tx.commit();
  return *findMovement(db, patch.id);
}

void deleteMovement(Database& db, const std::string& id) {
  Transaction tx(db);
  auto existing = findMovement(db, id);
  if (!existing) throw NotFoundError("lançamento não encontrado: " + id);
  auto product = findProduct(db, existing->productId);
  if (!product) throw NotFoundError("produto não encontrado: " + existing->productId);
  double offset = ledgerOffset(db, *product);

  db.prepare("DELETE FROM movements WHERE id=?").bind(1, id).step();

  recomputeProduct(db, existing->productId, offset);
  tx.commit();
}

}  // namespace estoque
