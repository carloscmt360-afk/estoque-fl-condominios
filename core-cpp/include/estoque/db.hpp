#pragma once
#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace estoque {

class SqlError : public std::runtime_error {
 public:
  explicit SqlError(const std::string& msg) : std::runtime_error(msg) {}
};

class NotFoundError : public std::runtime_error {
 public:
  explicit NotFoundError(const std::string& msg) : std::runtime_error(msg) {}
};

class Statement;  // fwd decl, definida abaixo

// RAII sobre uma conexão sqlite3. Deliberadamente NÃO thread-safe — a
// exclusão mútua entre chamadas concorrentes do Tauri é responsabilidade
// exclusiva do Mutex do lado Rust (ver bridge/src/lib.rs); este objeto
// assume acesso sempre serializado, o que também o mantém simples de testar.
class Database {
 public:
  // path == ":memory:" para testes; caminho real (ex. ".../dados/estoque.db")
  // em produção. Abre a conexão, aplica PRAGMA e roda as migrations.
  explicit Database(const std::string& path);
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  Database(Database&&) noexcept;
  Database& operator=(Database&&) noexcept;

  void execute(const std::string& sql);
  Statement prepare(const std::string& sql);

  sqlite3* handle() const { return db_; }

 private:
  void migrate();
  sqlite3* db_ = nullptr;
};

// RAII sobre um sqlite3_stmt*, com bind/step/column tipados — evita repetir
// o boilerplate cru do sqlite3.h em cada consulta do inventory_engine e do
// report_engine.
class Statement {
 public:
  Statement(sqlite3* db, const std::string& sql);
  ~Statement();

  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  Statement(Statement&&) noexcept;
  Statement& operator=(Statement&&) noexcept;

  Statement& bind(int idx, double v);
  Statement& bind(int idx, const std::string& v);
  Statement& bindNull(int idx);

  // true = há linha disponível (SQLITE_ROW); false = terminou (SQLITE_DONE)
  bool step();

  double columnDouble(int idx) const;
  std::string columnText(int idx) const;
  bool columnIsNull(int idx) const;

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

// RAII para transação: começa no construtor, faz ROLLBACK automático no
// destrutor a menos que commit() já tenha sido chamado — garante que uma
// exceção no meio de applyEntrada/applySaida/applyCorrecao nunca deixa o
// produto e a movimentação dessincronizados.
class Transaction {
 public:
  explicit Transaction(Database& db);
  ~Transaction();

  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  void commit();

 private:
  Database& db_;
  bool active_ = true;
};

}  // namespace estoque
