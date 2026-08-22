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
    "delivered_by_user_id, delivered_by_name, edited_at, edited_by_user_id, edited_by_name";

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
  r.editedAt = textOrEmpty(st, 15);
  r.editedByUserId = textOrEmpty(st, 16);
  r.editedByName = textOrEmpty(st, 17);
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
      "SELECT p.id, p.name, p.unit, p.category, p.qty, p.avg_cost, p.min_stock, p.sku, "
      "  COALESCE((SELECT SUM(i.qty) FROM request_items i JOIN requests r ON r.id = i.request_id "
      "            WHERE i.product_id = p.id AND r.status IN ('pendente','aprovado')), 0), "
      "  p.image_path, p.thumbnail_path "
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
    a.sku = textOrEmpty(st, 7);
    a.reserved = st.columnDouble(8);
    a.available = a.qty - a.reserved;
    a.imagePath = textOrEmpty(st, 9);
    a.thumbnailPath = textOrEmpty(st, 10);
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

// Reservado de um produto, ignorando a própria requisição `excludeRequestId`
// — é o que sobra "para os outros" quando se está prestes a regravar os itens
// dela mesma.
double reservedQtyExcluding(Database& db, const std::string& productId, const std::string& excludeRequestId) {
  auto st = db.prepare(
      "SELECT COALESCE(SUM(i.qty), 0) FROM request_items i JOIN requests r ON r.id = i.request_id "
      "WHERE i.product_id=? AND r.status IN ('pendente','aprovado') AND r.id != ?");
  st.bind(1, productId).bind(2, excludeRequestId);
  st.step();
  return st.columnDouble(0);
}

}  // namespace

Request updateRequestItems(Database& db, const std::string& id, const std::vector<RequestItem>& items,
                           const User& actor, const std::string& nowIso) {
  if (items.empty()) throw std::invalid_argument("a requisição precisa ter pelo menos um material");

  auto found = findRequest(db, id);
  if (!found) throw NotFoundError("requisição não encontrada: " + id);
  if (found->status != request_status::kPendente && found->status != request_status::kAprovado) {
    throw std::invalid_argument("não é possível editar os itens de uma requisição com situação '" +
                                found->status + "' — depois de entregue, rejeitada ou cancelada os "
                                "itens são histórico");
  }

  // Mesma agregação por produto de createRequest: duas linhas do mesmo item
  // na edição têm que ser conferidas juntas contra o disponível.
  struct Agg {
    double qty = 0;
    std::string firstItemId;
  };
  std::vector<std::pair<std::string, Agg>> agregado;
  for (const auto& it : items) {
    if (!(it.qty > 0)) throw std::invalid_argument("informe uma quantidade maior que zero em cada item");
    auto ag = std::find_if(agregado.begin(), agregado.end(),
                           [&](const auto& p) { return p.first == it.productId; });
    if (ag == agregado.end()) {
      agregado.push_back({it.productId, Agg{it.qty, it.id}});
    } else {
      ag->second.qty += it.qty;
    }
  }

  Transaction tx(db);

  for (const auto& [productId, agg] : agregado) {
    auto product = findProduct(db, productId);
    if (!product) throw NotFoundError("produto não encontrado: " + productId);

    double disponivel = product->qty - reservedQtyExcluding(db, productId, id);
    if (agg.qty > disponivel + kEps) {
      throw std::invalid_argument("não há saldo disponível de \"" + product->name + "\": pedido " +
                                  std::to_string(agg.qty) + ", disponível " +
                                  std::to_string(disponivel < 0 ? 0.0 : disponivel) +
                                  " (o restante já está reservado em outras requisições)");
    }
  }

  db.prepare("DELETE FROM request_items WHERE request_id=?").bind(1, id).step();

  int seq = 0;
  for (const auto& [productId, agg] : agregado) {
    auto product = findProduct(db, productId);
    std::string itemId = agg.firstItemId.empty() ? id + "_i" + std::to_string(++seq) : agg.firstItemId;
    auto ist = db.prepare(
        "INSERT INTO request_items (id, request_id, product_id, product_name, unit, qty) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    ist.bind(1, itemId).bind(2, id).bind(3, product->id).bind(4, product->name);
    ist.bind(5, product->unit).bind(6, agg.qty);
    ist.step();
  }

  auto ust = db.prepare(
      "UPDATE requests SET edited_at=?, edited_by_user_id=?, edited_by_name=? WHERE id=?");
  ust.bind(1, nowIso);
  actor.id.empty() ? ust.bindNull(2) : ust.bind(2, actor.id);
  ust.bind(3, actor.name).bind(4, id);
  ust.step();

  tx.commit();
  return *findRequest(db, id);
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

// ------------------------------------------------------- janela de pedidos

namespace {

constexpr const char* kWindowCols =
    "id, opens_at, closes_at, obs, created_at, created_by_user_id, created_by_name, "
    "closed_at, closed_by_user_id, closed_by_name";

RequestWindow rowToWindow(Statement& st) {
  RequestWindow w;
  w.id = st.columnText(0);
  w.opensAt = st.columnText(1);
  w.closesAt = st.columnText(2);
  w.obs = textOrEmpty(st, 3);
  w.createdAt = st.columnText(4);
  w.createdByUserId = textOrEmpty(st, 5);
  w.createdByName = st.columnText(6);
  w.closedAt = textOrEmpty(st, 7);
  w.closedByUserId = textOrEmpty(st, 8);
  w.closedByName = textOrEmpty(st, 9);
  return w;
}

// O instante em que a janela realmente deixa (ou deixou) de valer: o
// fechamento antecipado, quando existe, manda; senão o fim programado.
const std::string& effectiveEnd(const RequestWindow& w) {
  return w.closedAt.empty() ? w.closesAt : w.closedAt;
}

// Todas as datas do app são "YYYY-MM-DDTHH:MM:SS.mmmZ" em UTC, largura fixa —
// nesse formato a ordem alfabética É a ordem cronológica, então comparar as
// strings dispensa converter para epoch (e não inventa fuso onde não há).
bool isOpenAt(const RequestWindow& w, const std::string& nowIso) {
  return w.opensAt <= nowIso && nowIso < effectiveEnd(w);
}

}  // namespace

std::vector<RequestWindow> listRequestWindows(Database& db) {
  std::vector<RequestWindow> out;
  auto st = db.prepare(std::string("SELECT ") + kWindowCols +
                       " FROM request_windows ORDER BY opens_at DESC");
  while (st.step()) out.push_back(rowToWindow(st));
  return out;
}

std::optional<RequestWindow> findRequestWindow(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kWindowCols + " FROM request_windows WHERE id=?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToWindow(st);
}

std::optional<RequestWindow> openRequestWindowAt(Database& db, const std::string& nowIso) {
  // O filtro de fechamento antecipado fica no C++ (e não no SQL) porque
  // `closed_at` NULL não se compara com data em SQL sem um COALESCE que
  // esconderia a regra; isOpenAt() é a única definição de "aberta" no código.
  auto st = db.prepare(std::string("SELECT ") + kWindowCols +
                       " FROM request_windows WHERE opens_at <= ? AND closes_at > ? "
                       "ORDER BY opens_at DESC");
  st.bind(1, nowIso).bind(2, nowIso);
  while (st.step()) {
    RequestWindow w = rowToWindow(st);
    if (isOpenAt(w, nowIso)) return w;
  }
  return std::nullopt;
}

std::optional<RequestWindow> nextRequestWindowAfter(Database& db, const std::string& nowIso) {
  auto st = db.prepare(std::string("SELECT ") + kWindowCols +
                       " FROM request_windows WHERE opens_at > ? AND closed_at IS NULL "
                       "ORDER BY opens_at ASC");
  st.bind(1, nowIso);
  if (!st.step()) return std::nullopt;
  return rowToWindow(st);
}

void requireOpenRequestWindow(Database& db, const std::string& nowIso) {
  if (openRequestWindowAt(db, nowIso)) return;

  // A mensagem precisa dizer o que fazer, não só que deu errado: quem tenta
  // pedir fora do prazo não tem como saber se o período acabou ou se ainda
  // nem começou.
  auto proxima = nextRequestWindowAfter(db, nowIso);
  if (proxima) {
    throw std::invalid_argument(
        "[janela] as requisições estão fechadas no momento — a próxima janela de pedidos abre em " +
        proxima->opensAt);
  }
  throw std::invalid_argument(
      "[janela] as requisições estão fechadas no momento — nenhuma janela de pedidos está aberta. "
      "Peça ao administrador para abrir um novo período de requisições.");
}

RequestWindow createRequestWindow(Database& db, const RequestWindow& input, const std::string& nowIso) {
  const std::string abre = trim(input.opensAt);
  const std::string fecha = trim(input.closesAt);
  if (abre.empty() || fecha.empty()) {
    throw std::invalid_argument("informe a data/hora de abertura e de fechamento da janela");
  }
  if (fecha <= abre) {
    throw std::invalid_argument("o fechamento da janela precisa ser depois da abertura");
  }
  if (fecha <= nowIso) {
    throw std::invalid_argument("o fechamento da janela precisa estar no futuro — do jeito que está, "
                                "ela já nasceria encerrada");
  }

  // Duas janelas valendo ao mesmo tempo tornariam "a janela aberta agora" uma
  // pergunta de duas respostas. Comparar contra effectiveEnd já trata o
  // fechamento antecipado sozinho: uma janela encerrada às 12h devolve as
  // horas seguintes, e um período novo pode ocupá-las.
  for (const auto& w : listRequestWindows(db)) {
    if (abre < effectiveEnd(w) && w.opensAt < fecha) {
      throw std::invalid_argument(
          "esse período se sobrepõe a uma janela já cadastrada (de " + w.opensAt + " a " +
          effectiveEnd(w) + ") — ajuste as datas ou encerre a outra janela antes");
    }
  }

  auto st = db.prepare(
      "INSERT INTO request_windows (id, opens_at, closes_at, obs, created_at, created_by_user_id, "
      "created_by_name) VALUES (?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, input.id).bind(2, abre).bind(3, fecha);
  trim(input.obs).empty() ? st.bindNull(4) : st.bind(4, trim(input.obs));
  st.bind(5, input.createdAt.empty() ? nowIso : input.createdAt);
  input.createdByUserId.empty() ? st.bindNull(6) : st.bind(6, input.createdByUserId);
  st.bind(7, input.createdByName);
  st.step();

  return *findRequestWindow(db, input.id);
}

RequestWindow closeRequestWindowNow(Database& db, const std::string& id, const User& actor,
                                    const std::string& nowIso) {
  auto found = findRequestWindow(db, id);
  if (!found) throw NotFoundError("janela de requisições não encontrada: " + id);
  if (!found->closedAt.empty()) {
    throw std::invalid_argument("essa janela já foi encerrada antes do prazo");
  }
  if (found->closesAt <= nowIso) {
    throw std::invalid_argument("essa janela já terminou no horário programado — não há o que encerrar");
  }

  // Encerrar uma janela que ainda nem abriu equivale a cancelá-la: closed_at
  // fica antes de opens_at e isOpenAt() nunca vai considerá-la aberta.
  auto st = db.prepare(
      "UPDATE request_windows SET closed_at=?, closed_by_user_id=?, closed_by_name=? WHERE id=?");
  st.bind(1, nowIso);
  actor.id.empty() ? st.bindNull(2) : st.bind(2, actor.id);
  st.bind(3, actor.name).bind(4, id);
  st.step();

  return *findRequestWindow(db, id);
}

void deleteRequestWindow(Database& db, const std::string& id, const std::string& nowIso) {
  auto found = findRequestWindow(db, id);
  if (!found) throw NotFoundError("janela de requisições não encontrada: " + id);
  if (found->opensAt <= nowIso) {
    throw std::invalid_argument(
        "essa janela já começou — apagá-la esconderia por que os pedidos daquele período foram "
        "aceitos. Use 'Encerrar agora' para interromper o prazo");
  }
  db.prepare("DELETE FROM request_windows WHERE id=?").bind(1, id).step();
}

}  // namespace estoque
