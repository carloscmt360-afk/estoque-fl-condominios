#include "estoque/inventory_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

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
  p.sku = st.columnIsNull(8) ? "" : st.columnText(8);
  p.imagePath = st.columnIsNull(9) ? "" : st.columnText(9);
  p.thumbnailPath = st.columnIsNull(10) ? "" : st.columnText(10);
  return p;
}

// Próximo SKU disponível, no formato CMT###### — usa e AVANÇA o contador
// persistido em app_settings (chave 'sku_seq'), nunca a contagem de linhas
// da tabela (produtos excluídos não podem fazer um número ser reciclado —
// ver o comentário da migration 4 em db.cpp). O laço de verificação de
// colisão é só uma segunda trava defensiva (o índice único em products.sku
// é a que realmente impede duplicata em produção); nunca deveria repetir
// mais de uma vez na prática.
std::string nextSku(Database& db) {
  long long seq = 0;
  {
    auto st = db.prepare("SELECT value FROM app_settings WHERE key='sku_seq'");
    if (st.step()) seq = std::stoll(st.columnText(0));
  }

  std::string sku;
  do {
    ++seq;
    std::ostringstream oss;
    oss << "CMT" << std::setw(6) << std::setfill('0') << seq;
    sku = oss.str();
    auto check = db.prepare("SELECT 1 FROM products WHERE sku=?");
    check.bind(1, sku);
    if (!check.step()) break;
  } while (true);

  db.prepare("INSERT OR REPLACE INTO app_settings (key, value) VALUES ('sku_seq', ?)")
      .bind(1, std::to_string(seq))
      .step();
  return sku;
}

// ---- Categoria administrativa (independente do SKU) ----
//
// Categoria é uma classificação administrativa fixa — SEIS valores, ponto —
// nunca texto livre. "Não Classificado" não é uma sétima categoria válida:
// é um estado transitório que só o backfill de dados legados (abaixo)
// atribui; validateCategory() nunca a aceita como entrada, então um item
// nesse estado só sai dele quando alguém escolhe uma categoria de verdade.
constexpr const char* kValidCategories[] = {
    "Papelaria", "Informática", "Assembleia", "Gráfica", "Brinde", "Valor",
};
constexpr const char* kUnclassifiedCategory = "Não Classificado";

bool isValidCategory(const std::string& c) {
  for (const char* v : kValidCategories) {
    if (c == v) return true;
  }
  return false;
}

void validateCategory(const std::string& category) {
  if (!isValidCategory(category)) {
    throw std::invalid_argument(
        "categoria inválida — selecione uma das categorias do sistema (Papelaria, Informática, "
        "Assembleia, Gráfica, Brinde ou Valor)");
  }
}

// Minúsculas + sem acento (só o range Latin-1 Supplement usado em
// português) — pra "Lápis"/"LAPIS"/"lápis" baterem com a mesma palavra-chave
// sem precisar listar cada variante. Note: isto REMOVE o acento (vira ASCII
// puro); é o oposto de canonDeptKey em retrospect_engine.cpp, que preserva
// o acento e só uppercasa — funções diferentes pra propósitos diferentes.
std::string foldAscii(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xC3 && i + 1 < s.size()) {
      unsigned char d = static_cast<unsigned char>(s[i + 1]);
      char base = 0;
      switch (d) {
        case 0x81: case 0xA1: case 0x80: case 0xA0:  // Á á À à
        case 0x82: case 0xA2: case 0x83: case 0xA3:  // Â â Ã ã
          base = 'a'; break;
        case 0x89: case 0xA9: case 0x8A: case 0xAA:  // É é Ê ê
          base = 'e'; break;
        case 0x8D: case 0xAD:  // Í í
          base = 'i'; break;
        case 0x93: case 0xB3: case 0x94: case 0xB4:  // Ó ó Ô ô
        case 0x95: case 0xB5:                        // Õ õ
          base = 'o'; break;
        case 0x9A: case 0xBA:  // Ú ú
          base = 'u'; break;
        case 0x87: case 0xA7:  // Ç ç
          base = 'c'; break;
        default: base = 0;
      }
      if (base) {
        out.push_back(base);
        i += 2;
        continue;
      }
    }
    out.push_back(static_cast<char>(std::tolower(c)));
    i++;
  }
  return out;
}

// Ordem importa: a primeira regra cuja palavra-chave aparecer no nome
// vence. Lista deliberadamente enxuta e literal aos exemplos do pedido —
// o objetivo não é acertar 100% (impossível sem revisão humana), é reduzir
// o trabalho manual; o que sobrar vira kUnclassifiedCategory, nunca um
// palpite arriscado.
const std::vector<std::pair<std::string, std::vector<std::string>>>& categoryKeywordRules() {
  static const std::vector<std::pair<std::string, std::vector<std::string>>> kRules = {
      {"Papelaria",
       {"papel", "caneta", "lapis", "marca-texto", "marca texto", "marcador", "pasta", "grampeador",
        "grampo", "clipe", "post-it", "post it", "caderno", "envelope", "fita adesiva", "borracha",
        "corretivo", "regua", "tesoura", "cola escolar", "cartucho de tinta", "toner", "arquivo morto",
        "pincel atomico", "bloco de notas"}},
      {"Informática",
       {"mouse", "teclado", "cabo hdmi", "cabo usb", "cabo de rede", "adaptador", "pendrive", "pen drive",
        "carregador", "fonte de alimentacao", "notebook", "monitor", "impressora", "hd externo", "ssd",
        "webcam", "roteador", "memoria ram", "cartucho hp", "cartucho epson", "no-break", "nobreak"}},
      {"Assembleia",
       {"urna", "cracha", "votacao", "identificador", "assembleia", "cedula"}},
      {"Gráfica",
       {"cartao de visita", "envelope personalizado", "folder", "adesivo", "banner", "convite",
        "divisoria", "impresso", "grafica"}},
      {"Brinde",
       {"caneca", "chaveiro", "agenda personalizada", "sacola", "garrafa personalizada", "brinde",
        "squeeze", "ecobag", "camiseta promocional"}},
  };
  return kRules;
}

// Tenta classificar um item legado (SEM usar SKU/marca/cor — item 10 do
// pedido) primeiro pela categoria antiga (se ela já bater literalmente com
// uma das seis), depois por palavra-chave no nome. kUnclassifiedCategory se
// nada bater — fica marcado pra revisão administrativa, nunca um chute.
std::string classifyCategory(const std::string& oldCategory, const std::string& name) {
  for (const char* v : kValidCategories) {
    if (foldAscii(oldCategory) == foldAscii(v)) return v;
  }
  std::string foldedName = foldAscii(name);
  for (const auto& [categoria, palavras] : categoryKeywordRules()) {
    for (const auto& p : palavras) {
      if (foldedName.find(foldAscii(p)) != std::string::npos) return categoria;
    }
  }
  return kUnclassifiedCategory;
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
    "id, name, unit, min_stock, category, qty, avg_cost, created_at, sku, image_path, thumbnail_path";
constexpr const char* kDepartmentCols = "id, name, encarregado, monthly_limit, created_at";
constexpr const char* kMovementCols =
    "id, type, product_id, qty, unit_price, supplier, nf, department_id, recipient, encarregado, "
    "requester, obs, date, resulting_qty, resulting_avg_cost, created_at, new_avg_cost";

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
  m.newAvgCost = st.columnIsNull(16) ? 0.0 : st.columnDouble(16);
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
void step(Position& s, const std::string& type, double qty, double unitPrice, double newAvgCost = 0) {
  if (type == "entrada") {
    double baseQty = std::max(s.qty, 0.0);  // saldo negativo nunca entra na ponderação
    double novaQty = s.qty + qty;
    s.avgCost = (baseQty + qty) > 0 ? (baseQty * s.avgCost + qty * unitPrice) / (baseQty + qty) : 0.0;
    s.qty = novaQty;
  } else if (type == "saida") {
    s.qty -= qty;
  } else {
    s.qty += qty;  // ajuste: qty já é o delta
    if (newAvgCost > 0) s.avgCost = newAvgCost;  // correção manual do custo médio, opcional
  }
}

// Replay do razão inteiro do produto (todos os lançamentos, do zero).
Position replayLedger(Database& db, const std::string& productId) {
  Position s;
  auto st = db.prepare(
      "SELECT type, qty, unit_price, new_avg_cost FROM movements WHERE product_id=? ORDER BY date, created_at");
  st.bind(1, productId);
  while (st.step()) {
    step(s, st.columnText(0), st.columnDouble(1), st.columnIsNull(2) ? 0.0 : st.columnDouble(2),
         st.columnIsNull(3) ? 0.0 : st.columnDouble(3));
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
      "SELECT type, qty, unit_price, new_avg_cost FROM movements "
      "WHERE product_id=? AND id<>? AND (date < ? OR (date = ? AND created_at < ?)) "
      "ORDER BY date, created_at");
  st.bind(1, productId).bind(2, excludeMovementId).bind(3, date).bind(4, date).bind(5, createdAt);
  while (st.step()) {
    step(s, st.columnText(0), st.columnDouble(1), st.columnIsNull(2) ? 0.0 : st.columnDouble(2),
         st.columnIsNull(3) ? 0.0 : st.columnDouble(3));
  }
  s.qty += offsetQty;  // saldo comparável ao que o usuário vê no produto
  return s;
}

}  // namespace

// -------------------------------------------------------------- Produtos

Product createProduct(Database& db, const Product& input) {
  validateCategory(input.category);

  // Transação: leitura+avanço do contador de SKU e o INSERT viram uma coisa
  // só, pra dois createProduct não poderem ler o mesmo contador antes de
  // qualquer um dos dois avançá-lo (ver nextSku — item 7 do pedido de SKU).
  Transaction tx(db);
  std::string sku = nextSku(db);

  auto st = db.prepare(
      "INSERT INTO products (id, name, unit, min_stock, category, qty, avg_cost, created_at, sku) "
      "VALUES (?, ?, ?, ?, ?, 0, 0, ?, ?)");
  st.bind(1, input.id).bind(2, input.name).bind(3, input.unit).bind(4, input.minStock);
  st.bind(5, input.category);
  st.bind(6, input.createdAt);
  st.bind(7, sku);
  st.step();
  tx.commit();

  Product created = input;
  created.qty = 0;
  created.avgCost = 0;
  created.sku = sku;  // sempre gerado pelo servidor — o que o chamador mandar em input.sku é ignorado
  return created;
}

// Atribui SKU aos produtos que ainda não têm um (dados restaurados de um
// backup anterior a este recurso, ou uma reconciliação manual) — em ordem
// de criação, sem tocar em quem já tem. Chamada a cada abertura do app (ver
// Api::Api) e ao final de uma restauração de backup.
void backfillMissingSkus(Database& db) {
  std::vector<std::string> ids;
  auto st = db.prepare("SELECT id FROM products WHERE sku IS NULL OR sku = '' ORDER BY created_at, id");
  while (st.step()) ids.push_back(st.columnText(0));

  for (auto& id : ids) {
    Transaction tx(db);
    std::string sku = nextSku(db);
    db.prepare("UPDATE products SET sku=? WHERE id=?").bind(1, sku).bind(2, id).step();
    tx.commit();
  }
}

Product updateProduct(Database& db, const Product& input) {
  if (!findProduct(db, input.id)) {
    throw NotFoundError("produto não encontrado: " + input.id);
  }
  validateCategory(input.category);
  auto st = db.prepare("UPDATE products SET name=?, unit=?, min_stock=?, category=? WHERE id=?");
  st.bind(1, input.name).bind(2, input.unit).bind(3, input.minStock);
  st.bind(4, input.category);
  st.bind(5, input.id);
  st.step();
  return *findProduct(db, input.id);
}

// Grava os caminhos (relativos, já resolvidos pelo módulo Rust de imagens —
// ver o comentário de Product::imagePath em models.hpp) da foto principal e
// da miniatura de um produto. Independente de tudo mais: alterar/remover
// foto nunca mexe em SKU, categoria, saldo ou histórico (item 9/13 do
// pedido de fotos) — por isso é um UPDATE à parte, fora de updateProduct.
Product setProductImage(Database& db, const std::string& productId, const std::string& imagePath,
                         const std::string& thumbnailPath) {
  if (!findProduct(db, productId)) {
    throw NotFoundError("produto não encontrado: " + productId);
  }
  db.prepare("UPDATE products SET image_path=?, thumbnail_path=? WHERE id=?")
      .bind(1, imagePath)
      .bind(2, thumbnailPath)
      .bind(3, productId)
      .step();
  return *findProduct(db, productId);
}

// Remove a REFERÊNCIA da foto (volta pro estado "sem foto" — item 3 do
// pedido: remover é uma opção de primeira classe, não só substituir).
// Não mexe no disco: apagar o arquivo físico é responsabilidade do
// chamador (bridge/src/images.rs::delete_product_images), chamado ANTES
// disto pelo comando Tauri — nessa ordem, uma falha ao apagar o arquivo
// nunca deixa o banco achando que o produto está sem foto quando o
// arquivo ainda existe no disco.
Product clearProductImage(Database& db, const std::string& productId) {
  if (!findProduct(db, productId)) {
    throw NotFoundError("produto não encontrado: " + productId);
  }
  db.prepare("UPDATE products SET image_path=NULL, thumbnail_path=NULL WHERE id=?").bind(1, productId).step();
  return *findProduct(db, productId);
}

// Reclassifica, uma única vez, produtos cuja categoria não é uma das seis
// válidas — dado legado (campo era texto livre antes deste recurso) ou
// restaurado de um backup anterior a ele. Ver classifyCategory/kUnclassifiedCategory
// acima: tenta por palavra-chave, cai em "Não Classificado" (com log via
// contagem no retorno) quando não consegue. Idempotente via flag em
// app_settings — chamada a cada abertura do app e ao final de uma
// restauração de backup, então roda de novo sozinha se algum dia a lista de
// regras crescer (basta trocar a versão da flag).
int classifyLegacyCategories(Database& db) {
  {
    auto st = db.prepare("SELECT value FROM app_settings WHERE key='category_migration_v1'");
    if (st.step()) return 0;
  }

  struct Row {
    std::string id, name, category;
  };
  std::vector<Row> rows;
  {
    auto st = db.prepare("SELECT id, name, category FROM products");
    while (st.step()) {
      Row r;
      r.id = st.columnText(0);
      r.name = st.columnText(1);
      r.category = st.columnIsNull(2) ? "" : st.columnText(2);
      if (!isValidCategory(r.category)) rows.push_back(std::move(r));
    }
  }

  for (auto& r : rows) {
    std::string nova = classifyCategory(r.category, r.name);
    db.prepare("UPDATE products SET category=? WHERE id=?").bind(1, nova).bind(2, r.id).step();
  }

  db.prepare("INSERT OR REPLACE INTO app_settings (key, value) VALUES ('category_migration_v1', 'done')").step();
  return static_cast<int>(rows.size());
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
      "recipient, encarregado, requester, obs, date, resulting_qty, resulting_avg_cost, created_at, "
      "new_avg_cost) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
  st.bind(1, m.id).bind(2, movementTypeToString(m.type)).bind(3, m.productId).bind(4, m.qty).bind(5, m.unitPrice);
  m.supplier.empty() ? st.bindNull(6) : st.bind(6, m.supplier);
  m.nf.empty() ? st.bindNull(7) : st.bind(7, m.nf);
  m.departmentId.empty() ? st.bindNull(8) : st.bind(8, m.departmentId);
  m.recipient.empty() ? st.bindNull(9) : st.bind(9, m.recipient);
  m.encarregado.empty() ? st.bindNull(10) : st.bind(10, m.encarregado);
  m.requester.empty() ? st.bindNull(11) : st.bind(11, m.requester);
  m.obs.empty() ? st.bindNull(12) : st.bind(12, m.obs);
  st.bind(13, m.date).bind(14, m.resultingQty).bind(15, m.resultingAvgCost).bind(16, m.createdAt);
  m.newAvgCost > 0 ? st.bind(17, m.newAvgCost) : st.bindNull(17);
  st.step();
}

}  // namespace

void recomputeProduct(Database& db, const std::string& productId, double offsetQty, double previousQty) {
  struct Row {
    std::string id, type;
    double qty = 0, unitPrice = 0, newAvgCost = 0;
  };
  std::vector<Row> rows;
  {
    auto st = db.prepare(
        "SELECT id, type, qty, unit_price, new_avg_cost FROM movements WHERE product_id=? "
        "ORDER BY date, created_at");
    st.bind(1, productId);
    while (st.step()) {
      Row r;
      r.id = st.columnText(0);
      r.type = st.columnText(1);
      r.qty = st.columnDouble(2);
      r.unitPrice = st.columnIsNull(3) ? 0.0 : st.columnDouble(3);
      r.newAvgCost = st.columnIsNull(4) ? 0.0 : st.columnDouble(4);
      rows.push_back(std::move(r));
    }
  }

  Position s;
  for (const auto& r : rows) {
    step(s, r.type, r.qty, r.unitPrice, r.newAvgCost);

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

  // Trava: uma operação não pode fazer um produto FICAR mais negativo do que
  // já estava (nunca de saldo saudável para negativo; nunca de negativo para
  // "mais negativo ainda"). Só o saldo FINAL é comparado, não cada ponto
  // intermediário do razão — um produto com deficit herdado (saída lançada
  // no passado sem entrada correspondente) pode ter passos intermediários
  // negativos que uma correção só resolve no fim da linha do tempo; travar
  // em cada passo impediria a própria correção. E um produto JÁ negativo
  // (dado legado, antes desta trava existir — corrigido automaticamente no
  // próximo start do app, ver repairNegativeBalances) não fica bloqueado
  // para edições que não pioram seu saldo.
  double finalQty = s.qty + offsetQty;
  if (finalQty < -0.000001 && finalQty < previousQty - 0.000001) {
    throw std::invalid_argument("esta operação deixaria o saldo do produto negativo");
  }

  db.prepare("UPDATE products SET qty=?, avg_cost=? WHERE id=?")
      .bind(1, finalQty)
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

  recomputeProduct(db, productId, offset, product->qty);
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

  recomputeProduct(db, productId, offset, product->qty);
  tx.commit();
  return *findMovement(db, movementId);
}

Movement applyCorrecao(Database& db, const std::string& movementId, const std::string& productId,
                        double qtyReal, const std::string& motivo, const std::string& date,
                        const std::string& createdAt, double newAvgCost) {
  Transaction tx(db);
  auto product = findProduct(db, productId);
  if (!product) throw NotFoundError("produto não encontrado: " + productId);
  double offset = ledgerOffset(db, *product);

  // Ajuste de inventário: corrige o saldo para o que foi contado fisicamente;
  // custo médio não muda (não é uma compra nem uma venda) A MENOS que
  // newAvgCost venha preenchido — ver o comentário dele no header.
  // O delta de saldo é contra o vigente NA DATA do ajuste — num ajuste
  // lançado hoje isso é o saldo atual (comportamento de sempre), num
  // retroativo é o saldo daquele dia.
  Position before = positionBefore(db, productId, date, createdAt, movementId, offset);

  Movement m;
  m.id = movementId;
  m.type = MovementType::Ajuste;
  m.productId = productId;
  m.qty = qtyReal - before.qty;
  m.unitPrice = before.avgCost;
  m.newAvgCost = newAvgCost;
  m.obs = motivo;
  m.date = date;
  m.createdAt = createdAt;
  insertMovement(db, m);

  recomputeProduct(db, productId, offset, product->qty);
  tx.commit();
  return *findMovement(db, movementId);
}

// Corrige, uma única vez, produtos com saldo negativo herdado (dados de
// antes da trava acima existir). Gravar um lançamento de ajuste — em vez de
// só fazer UPDATE products — preserva a garantia de que qty/avgCost são
// SEMPRE reprodutíveis pelo razão (ver recomputeProduct); o motivo fica
// visível na Linha do Tempo do produto afetado. Idempotente via a flag em
// app_settings: reabrir o app não gera o ajuste de novo, nem tenta reaplicar
// sobre um produto que o usuário tenha deixado negativo de novo depois.
std::vector<std::string> repairNegativeBalances(Database& db, const std::string& nowIso) {
  constexpr const char* kFlag = "negative_balance_repair_v1";
  {
    auto st = db.prepare("SELECT value FROM app_settings WHERE key=?");
    st.bind(1, kFlag);
    if (st.step()) return {};
  }

  std::vector<std::string> fixed;
  for (const auto& p : listProducts(db)) {
    if (p.qty < -0.000001) {
      applyCorrecao(db, "repair-zero-" + p.id, p.id, 0.0,
                    "Correção automática: saldo negativo herdado zerado ao abrir o aplicativo", nowIso, nowIso);
      fixed.push_back(p.id);
    }
  }

  db.prepare("INSERT OR REPLACE INTO app_settings (key, value) VALUES (?, ?)").bind(1, kFlag).bind(2, "done").step();
  return fixed;
}

// Reconcilia products.avg_cost com o que o razão de movimentações reproduz.
//
// `recomputeProduct` já mantém os dois em sincronia em toda entrada/saída/
// correção — mas um backup restaurado (Api::restoreFromJson) grava
// products.avg_cost e movements.unit_price/resulting_avg_cost DIRETO do JSON,
// sem nunca passar por ali. Se a planilha de origem trouxer um avg_cost que
// não bate com o preço das entradas dela mesma, o produto fica com dois
// números DIFERENTES para o mesmo custo: a tela de Produtos (lê
// products.avg_cost direto) mostra um, e o Relatório/Retrospecto (NUNCA leem
// essa coluna — sempre recalculam pelo razão, ver report_engine.cpp) mostram
// outro. Silencioso até alguém comparar as duas telas.
//
// Ao contrário do saldo (o offsetQty de recomputeProduct PRESERVA de
// propósito uma divergência herdada de saída sem entrada correspondente —
// ver o comentário longo no header), não existe divergência "legítima" de
// custo médio: ele é sempre e só o que o razão reproduz. Por isso todo
// produto passa por aqui, preservando o offset de saldo dele (ledgerOffset)
// e só reconciliando o custo. Barato de rodar sempre — cada produto só paga
// o replay completo (recomputeProduct) quando a comparação já não bate; os
// demais são só uma leitura.
std::vector<std::string> reconcileAvgCost(Database& db) {
  std::vector<std::string> fixed;
  for (const auto& p : listProducts(db)) {
    double custoPeloRazao = replayLedger(db, p.id).avgCost;
    // custoPeloRazao == 0 significa "o razão não tem nenhuma entrada/ajuste
    // que estabeleça um custo" (produto novo, ou histórico de compra nunca
    // importado) — sobrescrever com 0 destruiria um preço porventura real
    // vindo da origem sem NENHUM ganho de precisão. Só reconcilia quando o
    // razão tem uma resposta de verdade para dar.
    if (custoPeloRazao > 0 && std::abs(custoPeloRazao - p.avgCost) > 0.005) {
      recomputeProduct(db, p.id, ledgerOffset(db, p), p.qty);
      fixed.push_back(p.id);
    }
  }
  return fixed;
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

  recomputeProduct(db, existing->productId, offset, product->qty);
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

  recomputeProduct(db, existing->productId, offset, product->qty);
  tx.commit();
}

}  // namespace estoque
