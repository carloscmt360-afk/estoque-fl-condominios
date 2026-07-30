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

Database::Database(Database&& other) noexcept : db_(other.db_) { other.db_ = nullptr; }

Database& Database::operator=(Database&& other) noexcept {
  if (this != &other) {
    if (db_) sqlite3_close(db_);
    db_ = other.db_;
    other.db_ = nullptr;
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

// -------------------------------------------------------------- Transaction

Transaction::Transaction(Database& db) : db_(db) { db_.execute("BEGIN;"); }

Transaction::~Transaction() {
  if (active_) {
    // rollback silencioso: se já estamos desenrolando por causa de outra
    // exceção, uma falha aqui não deve mascarar a original.
    try {
      db_.execute("ROLLBACK;");
    } catch (...) {
    }
  }
}

void Transaction::commit() {
  db_.execute("COMMIT;");
  active_ = false;
}

}  // namespace estoque
