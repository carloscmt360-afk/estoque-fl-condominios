#include "estoque/request_engine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "estoque/inventory_engine.hpp"

namespace estoque {

namespace {

// Tolerância para comparar quantidades vindas de double: pedir 3 de um saldo
// de 3 não pode falhar por 1e-15 de erro de representação.
constexpr double kEps = 1e-9;

constexpr const char* kRequestCols =
    "id, department_id, department_name, requester_user_id, requester_name, status, obs, created_at, "
    "decided_at, decided_by_user_id, decided_by_name, decision_note, delivered_at, "
    "delivered_by_user_id, delivered_by_name";

std::string textOrEmpty(Statement& st, int idx) { return st.columnIsNull(idx) ? "" : st.columnText(idx); }

Request rowToRequest(Statement& st) {
  Request r;
  r.id = st.columnText(0);
  r.departmentId = textOrEmpty(st, 1);
  r.departmentName = st.columnText(2);
  r.requesterUserId = textOrEmpty(st, 3);
  r.requesterName = st.columnText(4);
  r.status = st.columnText(5);
  r.obs = textOrEmpty(st, 6);
  r.createdAt = st.columnText(7);
  r.decidedAt = textOrEmpty(st, 8);
  r.decidedByUserId = textOrEmpty(st, 9);
  r.decidedByName = textOrEmpty(st, 10);
  r.decisionNote = textOrEmpty(st, 11);
  r.deliveredAt = textOrEmpty(st, 12);
  r.deliveredByUserId = textOrEmpty(st, 13);
  r.deliveredByName = textOrEmpty(st, 14);
  return r;
}

void loadItems(Database& db, Request& r) {
  auto st = db.prepare(
      "SELECT id, request_id, product_id, product_name, unit, qty, movement_id FROM request_items "
      "WHERE request_id=? ORDER BY product_name");
  st.bind(1, r.id);
  while (st.step()) {
    RequestItem it;
    it.id = st.columnText(0);
    it.requestId = st.columnText(1);
    it.productId = st.columnText(2);
    it.productName = st.columnText(3);
    it.unit = textOrEmpty(st, 4);
    it.qty = st.columnDouble(5);
    it.movementId = textOrEmpty(st, 6);
    r.items.push_back(std::move(it));
  }
}

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Carrega a requisição e recusa a transição se ela não estiver num dos
// estados aceitos — a mensagem diz o estado atual, que é o que o usuário
// precisa saber quando dois validadores mexem no mesmo pedido.
Request loadForTransition(Database& db, const std::string& id, const std::vector<std::string>& allowed,
                          const std::string& acao) {
  auto found = findRequest(db, id);
  if (!found) throw NotFoundError("requisição não encontrada: " + id);
  if (std::find(allowed.begin(), allowed.end(), found->status) == allowed.end()) {
    throw std::invalid_argument("não é possível " + acao + " uma requisição com situação '" +
                                found->status + "'");
  }
  return *found;
}

}  // namespace

double reservedQty(Database& db, const std::string& productId) {
  auto st = db.prepare(
      "SELECT COALESCE(SUM(i.qty), 0) FROM request_items i JOIN requests r ON r.id = i.request_id "
      "WHERE i.product_id=? AND r.status IN ('pendente','aprovado')");
  st.bind(1, productId);
  st.step();
  return st.columnDouble(0);
}

std::vector<StockAvailability> stockAvailability(Database& db) {
  std::vector<StockAvailability> out;
  auto st = db.prepare(
      "SELECT p.id, p.name, p.unit, p.category, p.qty, p.avg_cost, p.min_stock, "
      "  COALESCE((SELECT SUM(i.qty) FROM request_items i JOIN requests r ON r.id = i.request_id "
      "            WHERE i.product_id = p.id AND r.status IN ('pendente','aprovado')), 0) "
      "FROM products p ORDER BY p.name");
  while (st.step()) {
    StockAvailability a;
    a.productId = st.columnText(0);
    a.name = st.columnText(1);
    a.unit = st.columnText(2);
    a.category = textOrEmpty(st, 3);
    a.qty = st.columnDouble(4);
    a.avgCost = st.columnDouble(5);
    a.minStock = st.columnDouble(6);
    a.reserved = st.columnDouble(7);
    a.available = a.qty - a.reserved;
    out.push_back(std::move(a));
  }
  return out;
}

std::vector<Request> listRequests(Database& db, const std::string& departmentFilter) {
  std::vector<Request> out;
  {
    std::string sql = std::string("SELECT ") + kRequestCols + " FROM requests";
    if (!departmentFilter.empty()) sql += " WHERE department_id=?";
    sql += " ORDER BY created_at DESC";
    auto st = db.prepare(sql);
    if (!departmentFilter.empty()) st.bind(1, departmentFilter);
    while (st.step()) out.push_back(rowToRequest(st));
  }
  for (auto& r : out) loadItems(db, r);
  return out;
}

std::optional<Request> findRequest(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kRequestCols + " FROM requests WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  Request r = rowToRequest(st);
  loadItems(db, r);
  return r;
}

Request createRequest(Database& db, const RequestInput& input) {
  if (input.items.empty()) throw std::invalid_argument("inclua pelo menos um material na requisição");

  auto department = findDepartment(db, input.departmentId);
  if (!department) throw NotFoundError("departamento não encontrado: " + input.departmentId);

  // Agrega por produto ANTES de validar: pedir 2 e depois mais 3 do mesmo item
  // na mesma requisição tem que ser conferido como 5 contra o disponível.
  struct Agg {
    double qty = 0;
    std::string firstItemId;
  };
  std::vector<std::pair<std::string, Agg>> agregado;
  for (const auto& it : input.items) {
    if (!(it.qty > 0)) throw std::invalid_argument("informe uma quantidade maior que zero em cada item");
    auto found = std::find_if(agregado.begin(), agregado.end(),
                              [&](const auto& p) { return p.first == it.productId; });
    if (found == agregado.end()) {
      agregado.push_back({it.productId, Agg{it.qty, it.id}});
    } else {
      found->second.qty += it.qty;
    }
  }

  Transaction tx(db);

  auto st = db.prepare(
      "INSERT INTO requests (id, department_id, department_name, requester_user_id, requester_name, "
      "status, obs, created_at) VALUES (?, ?, ?, ?, ?, 'pendente', ?, ?)");
  st.bind(1, input.id).bind(2, department->id).bind(3, department->name);
  input.requesterUserId.empty() ? st.bindNull(4) : st.bind(4, input.requesterUserId);
  st.bind(5, input.requesterName);
  trim(input.obs).empty() ? st.bindNull(6) : st.bind(6, trim(input.obs));
  st.bind(7, input.createdAt);
  st.step();

  int seq = 0;
  for (const auto& [productId, agg] : agregado) {
    auto product = findProduct(db, productId);
    if (!product) throw NotFoundError("produto não encontrado: " + productId);

    double disponivel = product->qty - reservedQty(db, productId);
    if (agg.qty > disponivel + kEps) {
      throw std::invalid_argument("não há saldo disponível de \"" + product->name + "\": pedido " +
                                  std::to_string(agg.qty) + ", disponível " +
                                  std::to_string(disponivel < 0 ? 0.0 : disponivel) +
                                  " (o restante já está reservado em outras requisições)");
    }

    // Um item por produto, com o id do primeiro item que citou aquele produto
    // (ou derivado da requisição, se ele veio vazio).
    std::string itemId = agg.firstItemId.empty() ? input.id + "_i" + std::to_string(++seq) : agg.firstItemId;
    auto ist = db.prepare(
        "INSERT INTO request_items (id, request_id, product_id, product_name, unit, qty) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    ist.bind(1, itemId).bind(2, input.id).bind(3, product->id).bind(4, product->name);
    ist.bind(5, product->unit).bind(6, agg.qty);
    ist.step();
  }

  tx.commit();
  return *findRequest(db, input.id);
}

namespace {

// Grava a decisão (aprovação/rejeição/cancelamento) — as três só diferem no
// status final, então compartilham a escrita.
Request applyDecision(Database& db, const Request& current, const std::string& newStatus,
                      const User& actor, const std::string& note, const std::string& nowIso) {
  auto st = db.prepare(
      "UPDATE requests SET status=?, decided_at=?, decided_by_user_id=?, decided_by_name=?, "
      "decision_note=? WHERE id=?");
  st.bind(1, newStatus).bind(2, nowIso);
  actor.id.empty() ? st.bindNull(3) : st.bind(3, actor.id);
  st.bind(4, actor.name);
  trim(note).empty() ? st.bindNull(5) : st.bind(5, trim(note));
  st.bind(6, current.id);
  st.step();
  return *findRequest(db, current.id);
}

}  // namespace

Request approveRequest(Database& db, const std::string& id, const User& actor, const std::string& note,
                       const std::string& nowIso) {
  Request current = loadForTransition(db, id, {request_status::kPendente}, "aprovar");
  return applyDecision(db, current, request_status::kAprovado, actor, note, nowIso);
}

Request rejectRequest(Database& db, const std::string& id, const User& actor, const std::string& note,
                      const std::string& nowIso) {
  Request current = loadForTransition(db, id, {request_status::kPendente}, "rejeitar");
  return applyDecision(db, current, request_status::kRejeitado, actor, note, nowIso);
}

Request cancelRequest(Database& db, const std::string& id, const User& actor, const std::string& note,
                      const std::string& nowIso) {
  Request current =
      loadForTransition(db, id, {request_status::kPendente, request_status::kAprovado}, "cancelar");
  return applyDecision(db, current, request_status::kCancelado, actor, note, nowIso);
}

Request deliverRequest(Database& db, const std::string& id, const User& actor, const std::string& nowIso,
                       const std::string& movementIdPrefix) {
  Request current = loadForTransition(db, id, {request_status::kAprovado}, "confirmar a entrega de");
  if (current.items.empty()) throw std::invalid_argument("a requisição não tem itens para entregar");
  if (current.departmentId.empty()) {
    throw std::invalid_argument(
        "a requisição não aponta para um departamento existente — não é possível gerar a saída");
  }
  if (!findDepartment(db, current.departmentId)) {
    throw NotFoundError("o departamento desta requisição foi excluído: " + current.departmentName);
  }

  Transaction tx(db);

  int seq = 0;
  for (const auto& item : current.items) {
    auto product = findProduct(db, item.productId);
    if (!product) throw NotFoundError("produto não encontrado: " + item.productName);
    // Confere contra o SALDO, não contra o disponível: a reserva deste próprio
    // pedido faz parte do que ainda está no estoque e é justamente o que está
    // sendo entregue agora.
    if (item.qty > product->qty + kEps) {
      throw std::invalid_argument("saldo insuficiente de \"" + product->name +
                                  "\" para entregar: o estoque foi consumido depois da aprovação");
    }

    std::string movementId = movementIdPrefix + "_" + std::to_string(++seq);
    std::string obs = "Requisição " + current.id;
    if (!current.obs.empty()) obs += " — " + current.obs;

    applySaida(db, movementId, item.productId, item.qty, current.departmentId, nowIso, obs,
               current.requesterName, nowIso);

    db.prepare("UPDATE request_items SET movement_id=? WHERE id=?")
        .bind(1, movementId)
        .bind(2, item.id)
        .step();
  }

  auto st = db.prepare(
      "UPDATE requests SET status='entregue', delivered_at=?, delivered_by_user_id=?, "
      "delivered_by_name=? WHERE id=?");
  st.bind(1, nowIso);
  actor.id.empty() ? st.bindNull(2) : st.bind(2, actor.id);
  st.bind(3, actor.name).bind(4, current.id);
  st.step();

  tx.commit();
  return *findRequest(db, id);
}

}  // namespace estoque
