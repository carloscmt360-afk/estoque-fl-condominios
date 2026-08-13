#include "estoque/db.hpp"

#include <utility>

namespace estoque {

namespace {

// Migration 1 (schema inicial). Uma nova versão futura vira `if (version < N)`
// dentro de migrate(), nunca reescreve as anteriores.
constexpr const char* kSchemaV1 = R"SQL(
CREATE TABLE products (
  id TEXT PRIMARY KEY, name TEXT NOT NULL, unit TEXT NOT NULL,
  min_stock REAL NOT NULL DEFAULT 0, category TEXT,
  qty REAL NOT NULL DEFAULT 0, avg_cost REAL NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);

CREATE TABLE departments (
  id TEXT PRIMARY KEY, name TEXT NOT NULL, encarregado TEXT, created_at TEXT NOT NULL
);

CREATE TABLE movements (
  id TEXT PRIMARY KEY,
  type TEXT NOT NULL CHECK (type IN ('entrada','saida','ajuste')),
  product_id TEXT NOT NULL REFERENCES products(id),
  qty REAL NOT NULL, unit_price REAL, supplier TEXT, nf TEXT,
  department_id TEXT,  -- sem FK: departamento pode ser excluído mantendo o histórico
                       -- (recipient/encarregado já ficam denormalizados abaixo por isso)
  recipient TEXT, encarregado TEXT, requester TEXT, obs TEXT,
  date TEXT NOT NULL,
  resulting_qty REAL, resulting_avg_cost REAL,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_movements_date         ON movements(date, created_at);
CREATE INDEX idx_movements_product_date ON movements(product_id, date);
CREATE INDEX idx_movements_dept_date    ON movements(department_id, date);
CREATE INDEX idx_movements_type_date    ON movements(type, date);
)SQL";

// Migration 2 — retrospecto e teto de gastos.
//
// `dept_cost_history` é uma fonte de dados SEPARADA do razão de movimentações:
// guarda o custo mensal por departamento vindo da planilha (aba RESTROSPECTO /
// CUSTO POR DEPTO), que cobre anos anteriores à existência deste app e, nos
// meses em que os dois coexistem, diverge do razão de propósito (a planilha
// valoriza a saída pelo preço histórico da linha, o razão pelo custo médio da
// data). Ver retrospect_engine.hpp para a regra de mesclagem.
//
// `departments.monthly_limit` é o teto de gasto acordado com cada setor (0 =
// sem limite); vira ALTER TABLE e não recriação, para nunca tocar nas linhas
// já gravadas no banco de produção.
constexpr const char* kSchemaV2 = R"SQL(
CREATE TABLE dept_cost_history (
  year   INTEGER NOT NULL,
  month0 INTEGER NOT NULL CHECK (month0 BETWEEN 0 AND 11),
  dept_key  TEXT NOT NULL,  -- nome canônico (maiúsculas, espaços normalizados)
  dept_name TEXT NOT NULL,  -- nome como exibir
  amount REAL NOT NULL,
  PRIMARY KEY (year, month0, dept_key)
);

CREATE TABLE app_settings (
  key TEXT PRIMARY KEY, value TEXT NOT NULL
);

ALTER TABLE departments ADD COLUMN monthly_limit REAL NOT NULL DEFAULT 0;
)SQL";

// Migration 3 — usuários, grupos de permissão e requisições de material.
//
// `users.email_key` é o e-mail em minúsculas e é ele que carrega o UNIQUE:
// login não pode diferenciar maiúsculas (ninguém digita o próprio e-mail
// sempre igual), mas o `email` original é preservado para exibir. A senha
// nunca é gravada — só o PBKDF2 dela, com salt próprio e o número de rodadas
// usado (ver crypto.hpp), para que aumentar o custo no futuro não invalide as
// senhas já existentes.
//
// `departments.permission_group_id` é o cerne do modelo pedido: a permissão é
// atribuída ao DEPARTAMENTO, e o usuário herda a do departamento a que está
// atrelado. Não existe permissão por usuário — mudar o setor de alguém já
// muda o que ele enxerga, sem ninguém revisar uma segunda lista.
//
// `requests`/`request_items` são o razão das requisições. Elas NÃO tocam no
// estoque enquanto estão em aberto: enquanto `pendente` ou `aprovado`, os
// itens contam como RESERVADOS (o saldo continua no produto, mas o disponível
// para novas requisições desconta a reserva). Só a confirmação de entrega
// gera as saídas de verdade em `movements` — é por isso que `request_items`
// guarda `movement_id`: é o vínculo auditável entre o pedido e a baixa que
// ele produziu.
constexpr const char* kSchemaV3 = R"SQL(
CREATE TABLE users (
  id TEXT PRIMARY KEY,
  email TEXT NOT NULL,
  email_key TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  role TEXT NOT NULL CHECK (role IN ('superadmin','usuario')),
  department_id TEXT,  -- sem FK: excluir um departamento não pode apagar gente
  active INTEGER NOT NULL DEFAULT 1,
  password_hash TEXT NOT NULL,
  password_salt TEXT NOT NULL,
  password_iterations INTEGER NOT NULL,
  created_at TEXT NOT NULL,
  last_login_at TEXT
);

CREATE INDEX idx_users_department ON users(department_id);

CREATE TABLE permission_groups (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  description TEXT,
  created_at TEXT NOT NULL
);

CREATE TABLE permission_group_perms (
  group_id TEXT NOT NULL REFERENCES permission_groups(id) ON DELETE CASCADE,
  feature TEXT NOT NULL,
  can_create INTEGER NOT NULL DEFAULT 0,
  can_read   INTEGER NOT NULL DEFAULT 0,
  can_update INTEGER NOT NULL DEFAULT 0,
  can_delete INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (group_id, feature)
);

ALTER TABLE departments ADD COLUMN permission_group_id TEXT;

CREATE TABLE requests (
  id TEXT PRIMARY KEY,
  department_id TEXT,
  department_name TEXT NOT NULL,   -- denormalizado: o histórico não muda se o setor for renomeado
  requester_user_id TEXT,
  requester_name TEXT NOT NULL,    -- idem, para o autor continuar legível se o usuário sair
  status TEXT NOT NULL CHECK (status IN ('pendente','aprovado','entregue','rejeitado','cancelado')),
  obs TEXT,
  created_at TEXT NOT NULL,        -- data/hora da SOLICITAÇÃO
  decided_at TEXT, decided_by_user_id TEXT, decided_by_name TEXT, decision_note TEXT,
  delivered_at TEXT, delivered_by_user_id TEXT, delivered_by_name TEXT
);

CREATE INDEX idx_requests_dept   ON requests(department_id, created_at);
CREATE INDEX idx_requests_status ON requests(status, created_at);

CREATE TABLE request_items (
  id TEXT PRIMARY KEY,
  request_id TEXT NOT NULL REFERENCES requests(id) ON DELETE CASCADE,
  product_id TEXT NOT NULL,
  product_name TEXT NOT NULL,
  unit TEXT,
  qty REAL NOT NULL,
  movement_id TEXT  -- preenchido na entrega: a saída em movements que este item gerou
);

CREATE INDEX idx_request_items_request ON request_items(request_id);
CREATE INDEX idx_request_items_product ON request_items(product_id);
)SQL";

}  // namespace

Database::Database(const std::string& path) {
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    std::string msg = db_ ? sqlite3_errmsg(db_) : "falha desconhecida ao abrir";
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw SqlError("não foi possível abrir o banco em '" + path + "': " + msg);
  }

  // DELETE (não WAL): mantém um único arquivo .db em repouso, sem -wal/-shm
  // ao lado — essencial para copiar a pasta dados/ de um pendrive para outro
  // PC sem deixar arquivos auxiliares inconsistentes para trás.
  execute("PRAGMA journal_mode=DELETE;");
  execute("PRAGMA foreign_keys=ON;");
  migrate();
}

Database::~Database() {
  if (db_) sqlite3_close(db_);
}

Database::Database(Database&& other) noexcept
    : db_(other.db_), txDepth_(other.txDepth_), txAborted_(other.txAborted_) {
  other.db_ = nullptr;
  other.txDepth_ = 0;
  other.txAborted_ = false;
}

Database& Database::operator=(Database&& other) noexcept {
  if (this != &other) {
    if (db_) sqlite3_close(db_);
    db_ = other.db_;
    txDepth_ = other.txDepth_;
    txAborted_ = other.txAborted_;
    other.db_ = nullptr;
    other.txDepth_ = 0;
    other.txAborted_ = false;
  }
  return *this;
}

void Database::execute(const std::string& sql) {
  char* errMsg = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
    std::string msg = errMsg ? errMsg : "erro SQL desconhecido";
    sqlite3_free(errMsg);
    throw SqlError("falha ao executar SQL: " + msg);
  }
}

Statement Database::prepare(const std::string& sql) { return Statement(db_, sql); }

void Database::migrate() {
  Statement st(db_, "PRAGMA user_version;");
  int version = 0;
  if (st.step()) version = static_cast<int>(st.columnDouble(0));

  if (version < 1) {
    execute(kSchemaV1);
    execute("PRAGMA user_version = 1;");
  }
  if (version < 2) {
    execute(kSchemaV2);
    execute("PRAGMA user_version = 2;");
  }
  if (version < 3) {
    execute(kSchemaV3);
    execute("PRAGMA user_version = 3;");
  }
}

// ---------------------------------------------------------------- Statement

Statement::Statement(sqlite3* db, const std::string& sql) {
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
    throw SqlError("falha ao preparar SQL: " + std::string(sqlite3_errmsg(db)) + " | SQL: " + sql);
  }
}

Statement::~Statement() {
  if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : stmt_(other.stmt_) { other.stmt_ = nullptr; }

Statement& Statement::operator=(Statement&& other) noexcept {
  if (this != &other) {
    if (stmt_) sqlite3_finalize(stmt_);
    stmt_ = other.stmt_;
    other.stmt_ = nullptr;
  }
  return *this;
}

Statement& Statement::bind(int idx, double v) {
  sqlite3_bind_double(stmt_, idx, v);
  return *this;
}

Statement& Statement::bind(int idx, const std::string& v) {
  sqlite3_bind_text(stmt_, idx, v.c_str(), -1, SQLITE_TRANSIENT);
  return *this;
}

Statement& Statement::bindNull(int idx) {
  sqlite3_bind_null(stmt_, idx);
  return *this;
}

bool Statement::step() {
  int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  throw SqlError("falha ao executar step: " + std::string(sqlite3_errmsg(sqlite3_db_handle(stmt_))));
}

double Statement::columnDouble(int idx) const { return sqlite3_column_double(stmt_, idx); }

std::string Statement::columnText(int idx) const {
  const unsigned char* text = sqlite3_column_text(stmt_, idx);
  return text ? reinterpret_cast<const char*>(text) : "";
}

bool Statement::columnIsNull(int idx) const { return sqlite3_column_type(stmt_, idx) == SQLITE_NULL; }

// ------------------------------------------------- Transações (aninháveis)

void Database::txBegin() {
  if (txDepth_ == 0) {
    execute("BEGIN;");
    txAborted_ = false;
  }
  ++txDepth_;
}

void Database::txCommit() {
  if (txDepth_ == 0) throw SqlError("commit sem transação ativa");
  --txDepth_;
  if (txDepth_ > 0) return;  // transação interna: quem manda é a mais externa

  if (txAborted_) {
    // Uma transação interna já desfez tudo; a externa não pode "confirmar"
    // um estado que não existe mais. Desfaz o resto e avisa em voz alta.
    txAborted_ = false;
    try {
      execute("ROLLBACK;");
    } catch (...) {
    }
    throw SqlError("transação abortada por uma operação interna — nada foi gravado");
  }
  execute("COMMIT;");
}

void Database::txRollback() noexcept {
  if (txDepth_ == 0) return;
  --txDepth_;
  txAborted_ = true;
  if (txDepth_ == 0) {
    // rollback silencioso: se já estamos desenrolando por causa de outra
    // exceção, uma falha aqui não deve mascarar a original.
    try {
      execute("ROLLBACK;");
    } catch (...) {
    }
    txAborted_ = false;
  }
}

Transaction::Transaction(Database& db) : db_(db) { db_.txBegin(); }

Transaction::~Transaction() {
  if (active_) db_.txRollback();
}

void Transaction::commit() {
  active_ = false;  // antes do txCommit: se ele lançar, o destrutor não desfaz duas vezes
  db_.txCommit();
}

}  // namespace estoque
