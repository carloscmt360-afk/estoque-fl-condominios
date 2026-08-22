#!/usr/bin/env bash
# scaffold_new_system.sh — gera, a partir do zero, um novo projeto com a
# MESMA arquitetura do Estoque FL Condomínios: núcleo de negócio em C++ puro
# + SQLite, ponte cxx para Rust, casca desktop Tauri, e frontend estático
# (HTML/CSS/JS sem framework nem bundler). Serve como ponto de partida para
# outros sistemas de desktop com armazenamento local portátil.
#
# O que é gerado é um esqueleto COMPLETO e coerente ponta-a-ponta em torno de
# uma entidade de exemplo genérica ("Item"): tabela SQLite -> engine C++ ->
# fachada Api -> ponte cxx -> comando Tauri -> tela no frontend. Para
# adaptar a um domínio real, troque "Item" pela sua entidade (Produto,
# Cliente, Ativo...) e replique o mesmo padrão nos 7 pontos listados no
# README gerado.
#
# Uso:
#   tools/scaffold_new_system.sh "Nome do Sistema" [identifier] [pasta-destino]
#
# Exemplos:
#   tools/scaffold_new_system.sh "Controle de Ativos"
#   tools/scaffold_new_system.sh "Controle de Ativos" com.cmt360.controleativos ../controle-ativos
#
# Depois de gerado, veja o README.md do projeto novo para os próximos passos
# (vendorizar third_party, cargo build, rodar o frontend isolado, etc.).
set -euo pipefail

# ---------------------------------------------------------------- args ----
if [[ $# -lt 1 ]]; then
  echo "Uso: $0 \"Nome do Sistema\" [identifier] [pasta-destino]" >&2
  echo "Ex.:  $0 \"Controle de Ativos\" com.suaempresa.controleativos ../controle-ativos" >&2
  exit 1
fi

APP_TITLE="$1"

# slug kebab-case (dirs/paths): minúsculas, sem acento, espaços -> hífen
slugify() {
  echo "$1" \
    | iconv -f utf8 -t ascii//TRANSLIT 2>/dev/null \
    | tr '[:upper:]' '[:lower:]' \
    | sed -E 's/[^a-z0-9]+/-/g; s/^-+|-+$//g'
}
APP_SLUG="$(slugify "$APP_TITLE")"
if [[ -z "$APP_SLUG" ]]; then
  echo "Não foi possível derivar um nome de pasta válido de \"$APP_TITLE\"." >&2
  exit 1
fi

# namespace/identificador (C++/Rust): sem hífen nem acento
APP_NS="$(echo "$APP_SLUG" | tr -d '-')"

APP_IDENTIFIER="${2:-com.suaempresa.$APP_NS}"
TARGET_DIR="${3:-./$APP_SLUG}"
APP_BIN="${APP_SLUG}-app"
APP_YEAR="$(date +%Y)"
ORG_NAME="Sua Empresa"

if [[ -e "$TARGET_DIR" && -n "$(ls -A "$TARGET_DIR" 2>/dev/null)" ]]; then
  echo "Pasta destino '$TARGET_DIR' já existe e não está vazia. Escolha outra ou remova-a antes." >&2
  exit 1
fi

echo "==> Gerando '$APP_TITLE' em $TARGET_DIR"
echo "    slug:       $APP_SLUG"
echo "    namespace:  $APP_NS"
echo "    identifier: $APP_IDENTIFIER"

mkdir -p \
  "$TARGET_DIR/.github/workflows" \
  "$TARGET_DIR/core-cpp/include/__APP_NS__" \
  "$TARGET_DIR/core-cpp/src" \
  "$TARGET_DIR/core-cpp/tests" \
  "$TARGET_DIR/core-cpp/third_party/sqlite" \
  "$TARGET_DIR/core-cpp/third_party/nlohmann" \
  "$TARGET_DIR/bridge/cpp" \
  "$TARGET_DIR/bridge/src" \
  "$TARGET_DIR/core-cli/src" \
  "$TARGET_DIR/src-tauri/src" \
  "$TARGET_DIR/src-tauri/capabilities" \
  "$TARGET_DIR/src-tauri/icons" \
  "$TARGET_DIR/frontend/css" \
  "$TARGET_DIR/frontend/js/components" \
  "$TARGET_DIR/frontend/js/views" \
  "$TARGET_DIR/frontend/fixtures" \
  "$TARGET_DIR/tools"

# =========================================================================
# ROOT
# =========================================================================

cat > "$TARGET_DIR/Cargo.toml" <<'TEMPLATE_EOF'
[workspace]
resolver = "2"
members = ["bridge", "core-cli"]
# src-tauri fica de fora do workspace (endereçável via --manifest-path, não
# excluído no sentido de não existir) de propósito: é a única parte que
# depende de WebKitGTK/WebView2 — bibliotecas de sistema que nem todo
# ambiente de desenvolvimento tem instaladas. Isso permite `cargo build
# --workspace` compilar e testar tudo que É testável sem GUI (bridge +
# core-cli), inclusive em CI Linux, mesmo mirando produção em Windows.
exclude = ["src-tauri"]

# Build de produção: binário menor e mais rápido. src-tauri fica fora deste
# workspace (ver acima), então repete este mesmo bloco no seu próprio
# Cargo.toml — é o que "cargo tauri build" de fato usa.
[profile.release]
opt-level = 3
lto = true
codegen-units = 1
strip = true
TEMPLATE_EOF

cat > "$TARGET_DIR/.gitignore" <<'TEMPLATE_EOF'
# Artefatos de build — cada workspace Rust (raiz e src-tauri) tem o seu
/target/
/src-tauri/target/
/src-tauri/gen/

# Instaladores gerados localmente (o de produção sai do CI, ver .github/workflows)
/src-tauri/target/release/bundle/

# Fixtures de desenvolvimento: dados reais/de teste do frontend mock —
# regenerar localmente com `cargo run -p core-cli -- --dump-fixtures ...`
/frontend/fixtures/*.json

# Banco de dados local (nunca deve entrar no controle de versão)
/dados/
*.db
*.db-journal
*.db-wal
*.db-shm
TEMPLATE_EOF

cat > "$TARGET_DIR/README.md" <<'TEMPLATE_EOF'
# __APP_TITLE__

Esqueleto gerado por `tools/scaffold_new_system.sh` a partir da arquitetura
do **Estoque FL Condomínios**: app desktop com núcleo de negócio em C++ puro
+ SQLite, casca Rust/Tauri e frontend estático (HTML/CSS/JS, sem
framework/bundler). Este README documenta a arquitetura EM DETALHE — leia
antes de começar a adaptar para o seu domínio real.

## Por que esta arquitetura

- **Núcleo em C++ puro, sem depender de Rust/Tauri.** Toda a regra de
  negócio e o acesso ao SQLite vivem numa camada que não sabe que existe um
  app desktop por cima. Isso permite testá-la com `doctest` (rápido, sem
  compilar WebView) e reutilizá-la de qualquer host (CLI, outro app, testes).
- **Ponte cxx só para atravessar a fronteira Rust<->C++.** Nenhuma regra de
  negócio mora na ponte — ela só converte tipos (`bridge/`).
- **Tauri é casca fina.** `src-tauri/` não faz cálculo nenhum: abre a sessão,
  resolve o caminho de dados e expõe comandos que fazem `lock()` + delegam.
- **Frontend sem framework nem bundler.** HTML/CSS/JS com módulos ES nativos
  do navegador. Sem `npm install`, sem etapa de build — abre direto num
  servidor estático ou dentro do WebView do Tauri.
- **Armazenamento portátil.** Os dados ficam em `dados/`, ao lado do
  executável — nunca em AppData/Registro. Copiar a pasta do programa para
  outro PC ou pendrive preserva tudo (ver `core-cpp/include/__APP_NS__/portable_paths.hpp`).

## Arquitetura (visão geral)

```
core-cpp/    núcleo de negócio (regras + SQLite) — C++ puro, sem Rust/Tauri
bridge/      ponte cxx entre core-cpp e Rust — usada por src-tauri e core-cli
core-cli/    binário de diagnóstico: valida a ponte inteira sem WebKitGTK/WebView2
src-tauri/   casca Tauri — única parte que depende de WebView2/WebKitGTK
frontend/    HTML/CSS/JS estático — sidebar + views, sem framework/bundler
```

Fluxo de uma chamada, do clique ao SQLite e de volta:

```
clique no botão (frontend/js/views/*.js)
  -> api.xxx(...)                          (frontend/js/api.js)
  -> window.__TAURI__.core.invoke("xxx")   (ponte JS<->Rust do Tauri)
  -> #[tauri::command] fn xxx(...)         (src-tauri/src/commands.rs)
  -> session.pin_mut().xxx(...)            (bridge/src/lib.rs, ponte cxx)
  -> Session::xxx(...)                     (bridge/cpp/shim.cpp — converte DTO cxx <-> struct C++)
  -> Api::xxx(...)                         (core-cpp/src/api.cpp — traduz para/de JSON)
  -> free function xxx(db, ...)            (core-cpp/src/item_engine.cpp — regra de negócio)
  -> SQLite                                (core-cpp/src/db.cpp)
```

Cada seta é uma camada com UMA responsabilidade. Ao adicionar uma
funcionalidade nova, você mexe numa fatia vertical inteira (uma linha em
cada um desses arquivos), não numa camada isolada.

## Back-end — núcleo C++ (`core-cpp/`)

| Arquivo | Responsabilidade |
|---|---|
| `include/__APP_NS__/db.hpp` + `src/db.cpp` | `Database` (RAII sobre `sqlite3*`, `execute`/`prepare`, roda migrations no construtor), `Statement` (RAII sobre `sqlite3_stmt*`, bind/step/column tipados), `Transaction` (RAII: rollback automático a menos que `commit()` seja chamado — garante atomicidade). Domínio-agnóstico, não mexer ao trocar de entidade. |
| `include/__APP_NS__/models.hpp` | Structs de domínio puras (`Item`) — sem SQLite, sem cxx, sem JSON nas assinaturas. |
| `include/__APP_NS__/item_engine.hpp` + `src/item_engine.cpp` | Funções livres `createItem/updateItem/deleteItem/listItems/findItem(Database&, ...)` — CRUD e regra de negócio da entidade de exemplo. **É aqui que você troca "Item" pela sua entidade real.** |
| `include/__APP_NS__/summary_engine.hpp` + `src/summary_engine.cpp` | Exemplo de camada de AGREGAÇÃO/relatório separada do CRUD — mesmo padrão usado no sistema original para relatório mensal e retrospecto (cálculos que leem várias linhas e devolvem um resumo, sem misturar com a lógica de gravação). |
| `include/__APP_NS__/portable_paths.hpp` + `src/portable_paths.cpp` | Resolve `dados/` ao lado do executável (Windows via `GetModuleFileNameW`, Linux via `/proc/self/exe`). Domínio-agnóstico, não mexer. |
| `include/__APP_NS__/time_utils.hpp` + `src/time_utils.cpp` | Timestamp UTC ISO 8601. Domínio-agnóstico. |
| `include/__APP_NS__/api.hpp` + `src/api.cpp` | Classe `Api`: fachada única que a ponte cxx enxerga. Converte entre os tipos C++ puros e JSON (`nlohmann::json`) — é a ÚNICA camada do núcleo que sabe que existe serialização JSON. Inclui `backupJson`/`restoreFromJson`: exporta/importa o estado inteiro, útil para migração entre máquinas (mesma ideia do armazenamento portátil). |
| `tests/*.cpp` | Testes `doctest` — cada arquivo testa uma camada isoladamente, sem precisar compilar Rust/Tauri. |
| `third_party/` | SQLite (amálgama), `nlohmann/json.hpp` (single header) e `doctest.h` (single header) — **não vêm neste esqueleto** (arquivos grandes de terceiros). Rode `tools/fetch_third_party.sh` para baixá-los. |

### Por que C++ puro em vez de tudo em Rust

Se seu domínio não tem uma razão específica para C++ (biblioteca existente,
performance crítica em loop apertado, portar código legado), considere
simplesmente escrever `core-cpp/` inteiro em Rust dentro de `bridge/` e
eliminar a ponte cxx — é MENOS camadas. Esta arquitetura vale a pena quando
você já tem (ou quer) lógica de domínio em C++ reutilizável fora do Rust.

## Ponte Rust <-> C++ (`bridge/`)

- `src/lib.rs`: declara o contrato `#[cxx::bridge]` — o `ItemDto` (struct
  compartilhada, usada para ENTRADA de escrita — segurança de tipo onde um
  erro é caro) e as funções do tipo opaco `Session` (leitura/relatórios
  trafegam como `String` JSON: listas e relatórios variam demais em forma
  para valer a pena manter uma struct tipada em sincronia a cada campo novo).
- `cpp/shim.hpp` + `cpp/shim.cpp`: adaptador entre o núcleo de domínio (que
  usa só `std::string`/`std::vector`, nunca inclui `<rust/cxx.h>`) e a ponte
  cxx (que exige tipos `rust::String`/opacos na fronteira). Só este arquivo
  conhece os dois lados.
- `build.rs`: compila o amálgama do SQLite **como C** (nunca C++ — o
  amálgama usa idiomas válidos em C e ilegais em C++) e o shim + núcleo C++
  **como C++17**, e linka tudo na mesma biblioteca estática que o Rust usa.

## Diagnóstico sem GUI (`core-cli/`)

Binário que abre uma sessão de verdade (Rust -> cxx -> C++ -> SQLite) sem
depender de WebKitGTK/WebView2 — útil em CI Linux e para depurar a lógica
sem esperar o build da GUI. Também sabe gerar fixtures JSON para o mock do
frontend (`--dump-fixtures <pasta>`).

## Casca desktop (`src-tauri/`)

| Arquivo | Responsabilidade |
|---|---|
| `src/main.rs` | Estado do app (`AppState`: sessão OU erro de inicialização — nunca um crash silencioso), resolve o diretório portátil no `setup()`, registra os comandos no `invoke_handler!`. |
| `src/commands.rs` | Um `#[tauri::command]` por operação — cada um só faz `lock()` no estado e delega para a ponte cxx via o helper `with_session`. Nenhuma regra de negócio aqui. |
| `src/dto.rs` | Structs `#[derive(Deserialize)]` que o Tauri desserializa do JSON vindo do frontend — não dá para usar os DTOs gerados pelo `cxx` direto como parâmetro de comando (não implementam `serde::Deserialize`), então esta é a camada fina de conversão JSON -> struct Rust -> DTO da ponte. |
| `tauri.conf.json` | Config da janela, CSP e do bundler (`frontendDist` aponta pra `../frontend`, sem etapa de build). |
| `capabilities/default.json` | Permissões da janela principal (Tauri v2). |

## Front-end (`frontend/`)

Sem framework, sem bundler, sem `npm install` — só módulos ES nativos do
navegador, servidos como arquivos estáticos (o `frontendDist` do Tauri
aponta direto pra esta pasta).

| Arquivo | Responsabilidade |
|---|---|
| `index.html` | Casca da página: sidebar de navegação + uma `<section class="view">` por tela + os `<div class="overlay">` dos modais. Nenhuma view é removida do DOM ao trocar de tela — só ganha/perde a classe `.active` (ver `css/layout.css`). |
| `js/api.js` | ÚNICO ponto de contato com o backend: `window.__TAURI__.core.invoke(cmd, args)` quando rodando de verdade dentro do Tauri, ou um mock (`devMock.js`) quando aberto num navegador comum — é o que permite revisar o layout inteiro sem compilar Rust/C++/Tauri. |
| `js/app.js` | Router mínimo: troca `.active` entre as `.view`, inicializa cada view sob demanda na primeira visita (`initX()`), recarrega dados nas visitas seguintes (`reload()`). Trata erro de inicialização do backend com uma tela dedicada (nunca tela branca silenciosa). |
| `js/devMock.js` | Mock em memória do backend, alimentado por fixtures reais (`frontend/fixtures/*.json`, geradas por `core-cli --dump-fixtures`) — permite ajustar CSS/JS num `python3 -m http.server` comum. |
| `js/format.js` | Formatação de moeda/data/número — centralizada para nunca formatar "na unha" espalhado pelas views. |
| `js/components/modal.js`, `js/components/toast.js` | Componentes de UI reutilizáveis (abrir/fechar modal por `data-close`, notificação temporária). |
| `js/views/items.js` | Uma view = um módulo: busca dados via `api`, renderiza a tabela, abre o modal de criar/editar, valida o formulário, chama `api.createItem/updateItem/deleteItem`. **É aqui que você troca "Item" pela sua entidade real na tela.** |
| `js/views/dashboard.js` | Exemplo de tela de agregação: chama `api.computeSummary()` e renderiza indicadores — mesmo padrão do Relatório Mensal do sistema original. |
| `css/tokens.css` | Variáveis de design (cores, espaçamento, raio, tipografia) — mude aqui para trocar a identidade visual inteira sem tocar em outro CSS. |
| `css/layout.css` | Casca do app (sidebar + conteúdo) e mecanismo de troca de view. |
| `css/components.css` | Botões, cards, painéis, tabelas, formulários, modal, toast — vocabulário visual reutilizável entre todas as views. |
| `css/print.css` | Esconde o app e mostra só um `#printArea` montado dinamicamente na hora de imprimir (`window.print()`) — o mesmo documento não deveria imprimir a sidebar/filtros/gráficos interativos. |

## Entidade de exemplo: "Item"

O esqueleto inclui um CRUD ponta-a-ponta funcional em torno de uma entidade
genérica `Item` (`id, name, description, value, createdAt`) — é o exemplo
mínimo que toca as 7 camadas. Para adaptar ao seu domínio, replique o padrão
nestes pontos (na ordem em que normalmente se edita):

1. `core-cpp/include/__APP_NS__/models.hpp` — troque/estenda o struct.
2. `core-cpp/src/db.cpp` — schema da tabela (`kSchemaV1`), com migrations
   incrementais (`kSchemaV2`, ...) para nunca recriar tabelas em produção.
3. `core-cpp/include/__APP_NS__/item_engine.hpp` + `.cpp` — regra de negócio.
4. `core-cpp/src/api.cpp` — tradução para/de JSON na fachada `Api`.
5. `bridge/src/lib.rs` — `ItemDto` e as funções de `Session` na ponte cxx.
6. `bridge/cpp/shim.hpp` + `.cpp` — conversão DTO cxx <-> struct C++.
7. `src-tauri/src/dto.rs` + `commands.rs` + `main.rs` (registrar o comando) —
   camada Tauri.
8. `frontend/js/api.js` + `frontend/js/views/items.js` + `index.html` — tela.

## Build

**Linux/macOS (dev — valida a lógica, não builda a GUI):**
```bash
tools/fetch_third_party.sh              # baixa sqlite/nlohmann/doctest (uma vez)
cargo build --workspace                 # bridge + core-cli
cargo run -p core-cli                   # roda a ponte com um banco de exemplo
```

**Windows (produção — gera o instalador):**
```bash
cargo install tauri-cli --version "^2.0.0"
cd src-tauri
cargo tauri build
```
Requer Rust + Visual Studio Build Tools (workload "Desktop development with
C++") instalados. Adicione um ícone real em `src-tauri/icons/icon.ico`
antes do primeiro build (o Tauri exige o arquivo, mesmo que provisório).

**CI:** `.github/workflows/build-windows.yml` builda automaticamente a cada
push em `main` (runner `windows-latest`), publicando o instalador como
artefato.

## Testes (núcleo C++, doctest, sem cmake)

```bash
cd core-cpp
gcc -c -O1 -w -DSQLITE_THREADSAFE=1 third_party/sqlite/sqlite3.c -o /tmp/sqlite3.o
for t in test_item_engine test_portable_paths; do
  g++ -std=c++17 -Iinclude -Ithird_party/sqlite -Ithird_party \
    src/*.cpp tests/$t.cpp /tmp/sqlite3.o -lpthread -ldl -o /tmp/$t && /tmp/$t
done
```

## Frontend — desenvolvimento sem compilar Rust/C++/Tauri

```bash
cargo run -p core-cli -- --dump-fixtures frontend/fixtures
cd frontend && python3 -m http.server 8000
```
O navegador cacheia módulos ES agressivamente: depois de editar `js/`,
recarregue com **Ctrl+Shift+R**.

## Armazenamento portátil

Os dados ficam sempre em `dados/`, ao lado do executável — nunca em AppData.
Copiar a pasta do programa (incluindo `dados/`) para outro PC ou pendrive
preserva tudo.
TEMPLATE_EOF

cat > "$TARGET_DIR/.github/workflows/build-windows.yml" <<'TEMPLATE_EOF'
name: Build Windows

on:
  push:
    branches: [main]
  workflow_dispatch: {}

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - name: Instalar toolchain Rust
        uses: dtolnay/rust-toolchain@stable

      - name: Cache de dependências Rust
        uses: Swatinem/rust-cache@v2
        with:
          workspaces: "src-tauri -> target"

      - name: Instalar Tauri CLI
        run: cargo install tauri-cli --version "^2.0.0" --locked

      # Frontend é HTML/CSS/JS estático (sem npm/bundler) — nada a instalar
      # ou buildar antes; frontendDist em tauri.conf.json aponta direto pra
      # a pasta frontend/.
      - name: Build do instalador Windows
        working-directory: src-tauri
        run: cargo tauri build

      - name: Publicar artefato (instalador Windows)
        uses: actions/upload-artifact@v4
        with:
          name: __APP_SLUG__-windows
          path: |
            src-tauri/target/release/bundle/nsis/*.exe
            src-tauri/target/release/__APP_BIN__.exe
          if-no-files-found: error
TEMPLATE_EOF

echo "==> Escrevendo núcleo C++ (core-cpp/)"

# =========================================================================
# core-cpp — domínio-agnóstico: db, portable_paths, time_utils
# =========================================================================

cat > "$TARGET_DIR/core-cpp/include/__APP_NS__/db.hpp" <<'TEMPLATE_EOF'
#pragma once
#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace __APP_NS__ {

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
  // path == ":memory:" para testes; caminho real (ex. ".../dados/app.db")
  // em produção. Abre a conexão e roda as migrations no construtor.
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
// o boilerplate cru do sqlite3.h em cada consulta dos engines de domínio.
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
// exceção no meio de uma operação de escrita composta nunca deixa dados
// meio gravados.
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

}  // namespace __APP_NS__
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/src/db.cpp" <<'TEMPLATE_EOF'
#include "__APP_NS__/db.hpp"

#include <utility>

namespace __APP_NS__ {

namespace {

// Migration 1 (schema inicial). Uma nova versão futura vira um novo bloco
// `if (version < N)` dentro de migrate(), nunca reescreve as anteriores —
// isso é o que permite atualizar o app em produção sem apagar dados.
//
// Tabela de exemplo: troque/estenda ao adaptar para o seu domínio (ver
// README.md, seção "Entidade de exemplo").
constexpr const char* kSchemaV1 = R"SQL(
CREATE TABLE items (
  id          TEXT PRIMARY KEY,
  name        TEXT NOT NULL,
  description TEXT,
  value       REAL NOT NULL DEFAULT 0,
  created_at  TEXT NOT NULL
);

CREATE INDEX idx_items_created_at ON items(created_at);
)SQL";

}  // namespace

Database::Database(const std::string& path) {
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    std::string msg = db_ ? sqlite3_errmsg(db_) : "falha desconhecida ao abrir";
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw SqlError("não foi possível abrir o banco em '" + path + "': " + msg);
  }
  execute("PRAGMA foreign_keys = ON;");
  migrate();
}

Database::~Database() {
  if (db_) sqlite3_close(db_);
}

Database::Database(Database&& other) noexcept : db_(other.db_) {
  other.db_ = nullptr;
}

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
    std::string msg = errMsg ? errMsg : "erro desconhecido";
    sqlite3_free(errMsg);
    throw SqlError("sqlite3_exec falhou: " + msg);
  }
}

Statement Database::prepare(const std::string& sql) {
  return Statement(db_, sql);
}

// Versionamento simples via PRAGMA user_version: cada bloco `if` abaixo é
// idempotente e só roda uma vez, na primeira execução após ser adicionado.
void Database::migrate() {
  auto st = prepare("PRAGMA user_version;");
  int version = st.step() ? static_cast<int>(st.columnDouble(0)) : 0;

  if (version < 1) {
    execute(kSchemaV1);
    execute("PRAGMA user_version = 1;");
  }
  // if (version < 2) { execute(kSchemaV2); execute("PRAGMA user_version = 2;"); }
}

Statement::Statement(sqlite3* db, const std::string& sql) {
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
    throw SqlError("sqlite3_prepare_v2 falhou: " + std::string(sqlite3_errmsg(db)));
  }
}

Statement::~Statement() {
  if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : stmt_(other.stmt_) {
  other.stmt_ = nullptr;
}

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
  throw SqlError("sqlite3_step falhou: " + std::string(sqlite3_errstr(rc)));
}

double Statement::columnDouble(int idx) const {
  return sqlite3_column_double(stmt_, idx);
}

std::string Statement::columnText(int idx) const {
  const unsigned char* text = sqlite3_column_text(stmt_, idx);
  return text ? reinterpret_cast<const char*>(text) : "";
}

bool Statement::columnIsNull(int idx) const {
  return sqlite3_column_type(stmt_, idx) == SQLITE_NULL;
}

Transaction::Transaction(Database& db) : db_(db) {
  db_.execute("BEGIN;");
}

Transaction::~Transaction() {
  if (active_) db_.execute("ROLLBACK;");
}

void Transaction::commit() {
  db_.execute("COMMIT;");
  active_ = false;
}

}  // namespace __APP_NS__
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/include/__APP_NS__/portable_paths.hpp" <<'TEMPLATE_EOF'
#pragma once
#include <filesystem>
#include <string>

// Resolução do armazenamento portátil: o banco SEMPRE fica numa subpasta
// "dados" ao lado de onde o executável estiver rodando — nunca em AppData,
// nunca em outro lugar "esperto". É o que faz o app funcionar identicamente
// rodando de um pendrive em qualquer PC. Domínio-agnóstico: não precisa
// mexer aqui ao adaptar para outro sistema.
namespace __APP_NS__::portable_paths {

struct ResolveResult {
  bool ok = false;
  std::filesystem::path dataDir;  // válido só se ok==true
  std::string error;               // mensagem acionável para a UI, só se ok==false
};

// Caminho absoluto do executável em execução (GetModuleFileNameW no Windows,
// /proc/self/exe no Linux). Não trata atalhos (.lnk) — o SO já resolve o
// atalho para o caminho real antes de criar o processo.
std::filesystem::path currentExecutablePath();

// Núcleo testável: recebe o diretório do executável como parâmetro (em vez
// de descobri-lo sozinho), o que permite testar sem depender de onde o
// teste é executado — inclusive o caso de diretório somente-leitura. Sempre
// chama create_directories (idempotente), nunca checa-depois-cria. Nunca
// cai num local alternativo silencioso em caso de falha — sinaliza erro
// para o chamador decidir (mostrar modal com "Tentar novamente", nunca
# crash).
ResolveResult resolveDataDir(const std::filesystem::path& exeDir);

// Conveniência para produção: resolve o diretório do executável sozinho e
// delega para resolveDataDir(exeDir). Testes usam a versão acima, injetando
// o diretório.
ResolveResult resolveDataDir();

}  // namespace __APP_NS__::portable_paths
TEMPLATE_EOF

# a linha acima tem um typo proposital corrigido a seguir (comentário com #
# dentro de bloco C++ quebraria a compilação) — normaliza antes de seguir.
sed -i 's/^# crash)\.$/\/\/ crash)./' "$TARGET_DIR/core-cpp/include/__APP_NS__/portable_paths.hpp"

cat > "$TARGET_DIR/core-cpp/src/portable_paths.cpp" <<'TEMPLATE_EOF'
#include "__APP_NS__/portable_paths.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace __APP_NS__::portable_paths {

std::filesystem::path currentExecutablePath() {
#if defined(_WIN32)
  wchar_t buf[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::filesystem::path(std::wstring(buf, len));
#else
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0) return std::filesystem::current_path();
  buf[len] = '\0';
  return std::filesystem::path(buf);
#endif
}

ResolveResult resolveDataDir(const std::filesystem::path& exeDir) {
  ResolveResult result;
  std::filesystem::path dataDir = exeDir / "dados";
  std::error_code ec;
  std::filesystem::create_directories(dataDir, ec);
  if (ec) {
    result.ok = false;
    result.error = "não foi possível criar a pasta de dados em " + dataDir.string() + ": " + ec.message();
    return result;
  }
  result.ok = true;
  result.dataDir = dataDir;
  return result;
}

ResolveResult resolveDataDir() {
  return resolveDataDir(currentExecutablePath().parent_path());
}

}  // namespace __APP_NS__::portable_paths
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/include/__APP_NS__/time_utils.hpp" <<'TEMPLATE_EOF'
#pragma once
#include <string>

namespace __APP_NS__::time_utils {

// Timestamp UTC em ISO 8601 (ex.: "2026-08-12T14:30:00Z"). Usado como valor
// padrão de created_at quando o chamador não manda um explícito.
std::string nowIso();

}  // namespace __APP_NS__::time_utils
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/src/time_utils.cpp" <<'TEMPLATE_EOF'
#include "__APP_NS__/time_utils.hpp"

#include <array>
#include <ctime>

namespace __APP_NS__::time_utils {

std::string nowIso() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::array<char, 32> buf{};
  std::strftime(buf.data(), buf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf.data());
}

}  // namespace __APP_NS__::time_utils
TEMPLATE_EOF

# =========================================================================
# core-cpp — domínio de exemplo: Item (troque pela sua entidade real)
# =========================================================================

cat > "$TARGET_DIR/core-cpp/include/__APP_NS__/models.hpp" <<'TEMPLATE_EOF'
#pragma once
#include <string>

// Modelo de domínio puro — sem SQLite, sem cxx, sem JSON nas assinaturas.
// "Item" é a entidade de exemplo do esqueleto: troque pela sua (Produto,
// Cliente, Ativo...) e replique o padrão nos outros arquivos listados no
// README ("Entidade de exemplo").
namespace __APP_NS__ {

struct Item {
  std::string id;
  std::string name;
  std::string description;
  double value = 0;
  std::string createdAt;  // ISO 8601
};

}  // namespace __APP_NS__
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/include/__APP_NS__/item_engine.hpp" <<'TEMPLATE_EOF'
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "__APP_NS__/db.hpp"
#include "__APP_NS__/models.hpp"

// Motor de CRUD da entidade de exemplo. Nenhuma função aqui toca relógio de
// parede ou gera aleatoriedade: id e timestamp de criação são sempre
// passados pelo chamador (a ponte cxx os gera do lado Rust) — isso mantém o
// núcleo determinístico e barato de testar (ver core-cpp/tests/).
namespace __APP_NS__ {

// `input` já deve trazer id/createdAt preenchidos pelo chamador.
Item createItem(Database& db, const Item& input);
Item updateItem(Database& db, const Item& input);  // atualiza name/description/value
void deleteItem(Database& db, const std::string& id);
std::vector<Item> listItems(Database& db);  // ordem: mais recente primeiro
std::optional<Item> findItem(Database& db, const std::string& id);

}  // namespace __APP_NS__
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/src/item_engine.cpp" <<'TEMPLATE_EOF'
#include "__APP_NS__/item_engine.hpp"

namespace __APP_NS__ {

namespace {

constexpr const char* kItemCols = "id, name, description, value, created_at";

Item rowToItem(Statement& st) {
  Item item;
  item.id = st.columnText(0);
  item.name = st.columnText(1);
  item.description = st.columnIsNull(2) ? "" : st.columnText(2);
  item.value = st.columnDouble(3);
  item.createdAt = st.columnText(4);
  return item;
}

}  // namespace

Item createItem(Database& db, const Item& input) {
  if (input.id.empty()) throw std::invalid_argument("id do item não pode ser vazio");
  if (input.name.empty()) throw std::invalid_argument("nome do item é obrigatório");

  Transaction tx(db);
  db.prepare("INSERT INTO items (id, name, description, value, created_at) VALUES (?, ?, ?, ?, ?)")
      .bind(1, input.id)
      .bind(2, input.name)
      .bind(3, input.description)
      .bind(4, input.value)
      .bind(5, input.createdAt)
      .step();
  tx.commit();
  return input;
}

Item updateItem(Database& db, const Item& input) {
  if (input.name.empty()) throw std::invalid_argument("nome do item é obrigatório");
  if (!findItem(db, input.id)) throw NotFoundError("item não encontrado: " + input.id);

  Transaction tx(db);
  db.prepare("UPDATE items SET name = ?, description = ?, value = ? WHERE id = ?")
      .bind(1, input.name)
      .bind(2, input.description)
      .bind(3, input.value)
      .bind(4, input.id)
      .step();
  tx.commit();
  return input;
}

void deleteItem(Database& db, const std::string& id) {
  Transaction tx(db);
  db.prepare("DELETE FROM items WHERE id = ?").bind(1, id).step();
  tx.commit();
}

std::vector<Item> listItems(Database& db) {
  std::vector<Item> out;
  auto st = db.prepare(std::string("SELECT ") + kItemCols + " FROM items ORDER BY created_at DESC");
  while (st.step()) out.push_back(rowToItem(st));
  return out;
}

std::optional<Item> findItem(Database& db, const std::string& id) {
  auto st = db.prepare(std::string("SELECT ") + kItemCols + " FROM items WHERE id = ?");
  st.bind(1, id);
  if (!st.step()) return std::nullopt;
  return rowToItem(st);
}

}  // namespace __APP_NS__
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/include/__APP_NS__/summary_engine.hpp" <<'TEMPLATE_EOF'
#pragma once
#include "__APP_NS__/db.hpp"

// Camada de AGREGAÇÃO/relatório, separada de propósito do CRUD
// (item_engine.hpp): mesmo padrão usado no sistema original para separar
// "gravar dados" de "calcular relatório sobre os dados gravados" — o
// segundo tende a crescer em regras (filtros, janelas de tempo, comparativos)
# e fica ilegível se misturado com INSERT/UPDATE/DELETE.
namespace __APP_NS__ {

struct ItemSummary {
  int totalItems = 0;
  double totalValue = 0.0;
};

ItemSummary computeItemSummary(Database& db);

}  // namespace __APP_NS__
TEMPLATE_EOF

sed -i 's/^# e fica ilegível/\/\/ e fica ilegível/' "$TARGET_DIR/core-cpp/include/__APP_NS__/summary_engine.hpp"

cat > "$TARGET_DIR/core-cpp/src/summary_engine.cpp" <<'TEMPLATE_EOF'
#include "__APP_NS__/summary_engine.hpp"

namespace __APP_NS__ {

ItemSummary computeItemSummary(Database& db) {
  ItemSummary s;
  auto st = db.prepare("SELECT COUNT(*), COALESCE(SUM(value), 0) FROM items");
  if (st.step()) {
    s.totalItems = static_cast<int>(st.columnDouble(0));
    s.totalValue = st.columnDouble(1);
  }
  return s;
}

}  // namespace __APP_NS__
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/include/__APP_NS__/api.hpp" <<'TEMPLATE_EOF'
#pragma once
#include <string>
#include <vector>

#include "__APP_NS__/db.hpp"
#include "__APP_NS__/models.hpp"

// Fachada única do domínio: junta banco + engines num objeto com estado (a
// conexão SQLite). É o que a ponte cxx embrulha (ver bridge/cpp/shim.hpp) —
// mas esta classe em si não sabe que `cxx` existe, então continua 100%
// testável sozinha (doctest), com os mesmos tipos simples do resto do
// core-cpp. É a ÚNICA camada do núcleo que sabe que existe serialização
// JSON (via nlohmann::json, em api.cpp) — trocar o formato de transporte
# da ponte no futuro só mexeria aqui.
namespace __APP_NS__ {

class Api {
 public:
  explicit Api(const std::string& dbPath);

  std::string listItemsJson();
  std::string createItemJson(const Item& input);
  std::string updateItemJson(const Item& input);
  void deleteItem(const std::string& id);

  std::string computeSummaryJson();

  // Exporta/importa o estado inteiro em JSON — útil para migrar dados entre
  // máquinas (mesma ideia do armazenamento portátil, para o caso em que
  // copiar a pasta dados/ inteira não é prático).
  std::string backupJson();
  void restoreFromJson(const std::string& payload);

 private:
  Database db_;
};

}  // namespace __APP_NS__
TEMPLATE_EOF

sed -i 's/^# da ponte no futuro/\/\/ da ponte no futuro/' "$TARGET_DIR/core-cpp/include/__APP_NS__/api.hpp"

cat > "$TARGET_DIR/core-cpp/src/api.cpp" <<'TEMPLATE_EOF'
#include "__APP_NS__/api.hpp"

#include <nlohmann/json.hpp>

#include "__APP_NS__/item_engine.hpp"
#include "__APP_NS__/summary_engine.hpp"

namespace __APP_NS__ {

using json = nlohmann::json;

namespace {

json itemToJson(const Item& item) {
  json j;
  j["id"] = item.id;
  j["name"] = item.name;
  j["description"] = item.description;
  j["value"] = item.value;
  j["createdAt"] = item.createdAt;
  return j;
}

Item itemFromJson(const json& j) {
  Item item;
  item.id = j.value("id", "");
  item.name = j.value("name", "");
  item.description = j.value("description", "");
  item.value = j.value("value", 0.0);
  item.createdAt = j.value("createdAt", "");
  return item;
}

}  // namespace

Api::Api(const std::string& dbPath) : db_(dbPath) {}

std::string Api::listItemsJson() {
  json arr = json::array();
  for (const auto& item : listItems(db_)) arr.push_back(itemToJson(item));
  return arr.dump();
}

std::string Api::createItemJson(const Item& input) { return itemToJson(createItem(db_, input)).dump(); }
std::string Api::updateItemJson(const Item& input) { return itemToJson(updateItem(db_, input)).dump(); }
void Api::deleteItem(const std::string& id) { __APP_NS__::deleteItem(db_, id); }

std::string Api::computeSummaryJson() {
  auto s = computeItemSummary(db_);
  json j = {{"totalItems", s.totalItems}, {"totalValue", s.totalValue}};
  return j.dump();
}

// Mesmo formato usado pelo resto da API (chaves camelCase) — um backup
// gerado por uma versão do app continua legível por versões futuras que só
// acrescentem campos (leitura via .value(chave, padrão), nunca .at(chave)).
std::string Api::backupJson() {
  json arr = json::array();
  for (const auto& item : listItems(db_)) arr.push_back(itemToJson(item));
  json j = {{"items", arr}};
  return j.dump();
}

// Substitui TODOS os dados atuais pelo conteúdo do payload — sem
// reprocessar pelas regras de negócio, preserva os valores exatamente como
// estavam gravados.
void Api::restoreFromJson(const std::string& payload) {
  json j = json::parse(payload);
  Transaction tx(db_);
  db_.execute("DELETE FROM items;");
  for (const auto& raw : j.value("items", json::array())) {
    Item item = itemFromJson(raw);
    db_.prepare("INSERT INTO items (id, name, description, value, created_at) VALUES (?, ?, ?, ?, ?)")
        .bind(1, item.id)
        .bind(2, item.name)
        .bind(3, item.description)
        .bind(4, item.value)
        .bind(5, item.createdAt)
        .step();
  }
  tx.commit();
}

}  // namespace __APP_NS__
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/tests/test_item_engine.cpp" <<'TEMPLATE_EOF'
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "__APP_NS__/db.hpp"
#include "__APP_NS__/item_engine.hpp"
#include "__APP_NS__/summary_engine.hpp"

using namespace __APP_NS__;

TEST_CASE("cria, lista, atualiza e remove um item") {
  Database db(":memory:");

  Item input;
  input.id = "item-1";
  input.name = "Exemplo";
  input.value = 10.5;
  input.createdAt = "2026-01-01T00:00:00Z";
  createItem(db, input);

  auto all = listItems(db);
  REQUIRE(all.size() == 1);
  CHECK(all[0].name == "Exemplo");

  input.name = "Exemplo renomeado";
  updateItem(db, input);
  CHECK(findItem(db, input.id)->name == "Exemplo renomeado");

  auto summary = computeItemSummary(db);
  CHECK(summary.totalItems == 1);
  CHECK(summary.totalValue == doctest::Approx(10.5));

  deleteItem(db, input.id);
  CHECK(listItems(db).empty());
}

TEST_CASE("updateItem de item inexistente lança NotFoundError") {
  Database db(":memory:");
  Item input;
  input.id = "fantasma";
  input.name = "x";
  CHECK_THROWS_AS(updateItem(db, input), NotFoundError);
}
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/tests/test_portable_paths.cpp" <<'TEMPLATE_EOF'
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <filesystem>

#include "__APP_NS__/portable_paths.hpp"

using namespace __APP_NS__::portable_paths;

TEST_CASE("resolveDataDir cria a pasta dados/ ao lado do exeDir informado") {
  auto tmp = std::filesystem::temp_directory_path() / "__APP_SLUG___test_exe_dir";
  std::filesystem::create_directories(tmp);
  auto result = resolveDataDir(tmp);
  REQUIRE(result.ok);
  CHECK(std::filesystem::exists(result.dataDir));
  CHECK(result.dataDir.filename() == "dados");
  std::filesystem::remove_all(tmp);
}
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cpp/third_party/README.md" <<'TEMPLATE_EOF'
# third_party/

Bibliotecas de terceiros vendorizadas (single-header ou amálgama), exigidas
pelo build de `bridge/` e pelos testes de `core-cpp/tests/`:

- `sqlite/sqlite3.c` + `sqlite/sqlite3.h` — amálgama do SQLite.
- `nlohmann/json.hpp` — biblioteca JSON single-header.
- `doctest.h` — framework de testes single-header.

Rode `tools/fetch_third_party.sh` a partir da raiz do projeto para baixá-las
automaticamente. Elas ficam de fora do git (arquivos grandes de terceiros,
sem lógica própria) — cada dev/CI baixa uma vez.
TEMPLATE_EOF

echo "==> Escrevendo ponte cxx (bridge/) e core-cli/"

# =========================================================================
# bridge/
# =========================================================================

cat > "$TARGET_DIR/bridge/Cargo.toml" <<'TEMPLATE_EOF'
[package]
name = "bridge"
version = "0.1.0"
edition = "2021"

# Ponte cxx entre o núcleo de negócio em C++ (core-cpp/) e o Rust
# (src-tauri e core-cli). Ver src/lib.rs para o contrato exposto.
[dependencies]
cxx = "1"

[build-dependencies]
cxx-build = "1"
cc = "1"
TEMPLATE_EOF

cat > "$TARGET_DIR/bridge/build.rs" <<'TEMPLATE_EOF'
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let core_cpp = manifest_dir.join("..").join("core-cpp");
    let include_dir = core_cpp.join("include");
    let third_party_dir = core_cpp.join("third_party"); // nlohmann/json.hpp, doctest.h
    let sqlite_dir = third_party_dir.join("sqlite");

    // O amálgama do SQLite usa idiomas válidos em C mas ilegais em C++ —
    // precisa ser compilado como C, nunca como C++.
    cc::Build::new()
        .file(sqlite_dir.join("sqlite3.c"))
        .include(&sqlite_dir)
        .flag_if_supported("-w") // amálgama de terceiros: não é nosso código para lintar
        .define("SQLITE_THREADSAFE", "1")
        .opt_level(2)
        .warnings(false)
        .compile("sqlite3_vendored");

    // Ponte cxx: o shim (bridge/cpp/) + o núcleo C++ do domínio (core-cpp/),
    // compilados juntos como C++17.
    let cpp_dir = manifest_dir.join("cpp");
    let mut bridge_build = cxx_build::bridge("src/lib.rs");
    bridge_build
        .include(&cpp_dir)
        .include(&include_dir)
        .include(&third_party_dir)
        .include(&sqlite_dir)
        .file(cpp_dir.join("shim.cpp"));
    for src in ["db.cpp", "time_utils.cpp", "item_engine.cpp", "summary_engine.cpp", "portable_paths.cpp", "api.cpp"] {
        bridge_build.file(core_cpp.join("src").join(src));
    }
    // "-std=c++17" é sintaxe GCC/Clang; o MSVC (cl.exe) não reconhece essa
    // flag (descartada silenciosamente via flag_if_supported), então exige
    // "/std:c++17" explícito para compilar em modo C++17 de verdade.
    let target_env = std::env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if target_env == "msvc" {
        bridge_build.flag("/std:c++17");
    } else {
        bridge_build.flag_if_supported("-std=c++17");
    }
    bridge_build.warnings(true);
    bridge_build.compile("__APP_NS___bridge");

    // pthread/dl são bibliotecas do mundo Unix — não existem no MSVC. No
    // Windows o runtime C++ já embute as primitivas de thread.
    if target_env != "msvc" {
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=dl");
    }

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed={}", cpp_dir.display());
    println!("cargo:rerun-if-changed={}", core_cpp.join("src").display());
    println!("cargo:rerun-if-changed={}", include_dir.display());
}
TEMPLATE_EOF

cat > "$TARGET_DIR/bridge/cpp/shim.hpp" <<'TEMPLATE_EOF'
#pragma once
// Camada de adaptação entre o núcleo de domínio (core-cpp/, que usa só
// std::string/std::vector — nunca conhece `cxx`) e a ponte cxx (que exige
// tipos `rust::String`/opacos na fronteira). core-cpp continua 100%
// testável sozinho (doctest) sem nunca incluir <rust/cxx.h>; só este shim
// conhece os dois lados.
#include <memory>

#include "__APP_NS__/api.hpp"
#include "rust/cxx.h"

namespace __APP_NS__::shim {

// Declaração adiantada, não inclusão do header gerado (bridge/src/lib.rs.h):
// esse header É QUEM inclui este arquivo (via include!("shim.hpp") na
// ponte) — incluí-lo de volta aqui criaria uma dependência circular que o
// guarda de inclusão neutralizaria silenciosamente, deixando ItemDto
// indefinido. Declaração adiantada basta para parâmetro por valor em
// DECLARAÇÃO de função; o tipo completo só é necessário em shim.cpp, que
// inclui o header gerado normalmente (sem circularidade, é outra unidade de
// tradução).
struct ItemDto;

// Nomes dos métodos em snake_case: `cxx` casa por igualdade literal de nome
// com a declaração Rust em lib.rs (sem conversão camelCase<->snake_case).
class Session {
 public:
  explicit Session(const std::string& dbPath);

  rust::String list_items_json();
  rust::String create_item(ItemDto item);
  rust::String update_item(ItemDto item);
  void delete_item(rust::Str id);

  rust::String compute_summary_json();

  rust::String backup_json();
  void restore_from_json(rust::Str payload);

 private:
  __APP_NS__::Api api_;
};

std::unique_ptr<Session> open_session(rust::Str db_path);
rust::String resolve_data_dir_default();

}  // namespace __APP_NS__::shim
TEMPLATE_EOF

cat > "$TARGET_DIR/bridge/cpp/shim.cpp" <<'TEMPLATE_EOF'
#include "shim.hpp"

// Aqui (uma unidade de tradução separada de lib.rs.h, não incluída por ele)
// não há circularidade: podemos incluir o header gerado normalmente para
// obter a definição completa de ItemDto.
#include "bridge/src/lib.rs.h"
#include "__APP_NS__/portable_paths.hpp"

namespace __APP_NS__::shim {

namespace {

Item toItem(const ItemDto& d) {
  Item item;
  item.id = std::string(d.id);
  item.name = std::string(d.name);
  item.description = std::string(d.description);
  item.value = d.value;
  item.createdAt = std::string(d.created_at);
  return item;
}

}  // namespace

Session::Session(const std::string& dbPath) : api_(dbPath) {}

rust::String Session::list_items_json() { return api_.listItemsJson(); }
rust::String Session::create_item(ItemDto item) { return api_.createItemJson(toItem(item)); }
rust::String Session::update_item(ItemDto item) { return api_.updateItemJson(toItem(item)); }
void Session::delete_item(rust::Str id) { api_.deleteItem(std::string(id)); }

rust::String Session::compute_summary_json() { return api_.computeSummaryJson(); }

rust::String Session::backup_json() { return api_.backupJson(); }
void Session::restore_from_json(rust::Str payload) { api_.restoreFromJson(std::string(payload)); }

std::unique_ptr<Session> open_session(rust::Str db_path) {
  return std::make_unique<Session>(std::string(db_path));
}

rust::String resolve_data_dir_default() {
  auto result = portable_paths::resolveDataDir();
  if (!result.ok) throw std::runtime_error(result.error);
  return result.dataDir.string();
}

}  // namespace __APP_NS__::shim
TEMPLATE_EOF

cat > "$TARGET_DIR/bridge/src/lib.rs" <<'TEMPLATE_EOF'
// Ponte cxx entre o núcleo de negócio em C++ (core-cpp/__APP_NS__::Api) e o
// Rust (Tauri e core-cli). Compartilhada pelos dois binários: nenhum deles
// duplica a declaração da ponte.
//
// Critério híbrido: DTO tipado (shared struct) para a ENTRADA de operações
// de escrita (segurança de tipo onde um erro é caro); leitura/relatório
// trafegam como String JSON (listas/relatórios variam demais em forma para
// valer a pena manter uma struct tipada em sincronia a cada campo novo).
#[cxx::bridge(namespace = "__APP_NS__::shim")]
pub mod ffi {
    struct ItemDto {
        id: String,
        name: String,
        description: String,
        value: f64,
        created_at: String,
    }

    unsafe extern "C++" {
        include!("shim.hpp");

        type Session;

        fn open_session(db_path: &str) -> Result<UniquePtr<Session>>;

        fn list_items_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_item(self: Pin<&mut Session>, item: ItemDto) -> Result<String>;
        fn update_item(self: Pin<&mut Session>, item: ItemDto) -> Result<String>;
        fn delete_item(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn compute_summary_json(self: Pin<&mut Session>) -> Result<String>;

        fn backup_json(self: Pin<&mut Session>) -> Result<String>;
        fn restore_from_json(self: Pin<&mut Session>, payload: &str) -> Result<()>;

        // Resolução do armazenamento portátil (dados/ ao lado do executável).
        // A versão testável/injetável (resolveDataDir(exeDir)) tem cobertura
        // via doctest em core-cpp/tests/test_portable_paths.cpp; aqui só
        // expomos a conveniência de produção (auto-detecta o exe atual).
        fn resolve_data_dir_default() -> Result<String>;
    }
}

// cxx não sabe se um tipo opaco C++ é thread-safe, então Session nasce
// !Send/!Sync por padrão — o que impede AppState (Mutex<Option<UniquePtr<Session>>>)
// de satisfazer o bound `Send + Sync` que tauri::State exige.
//
// Send é seguro de afirmar aqui porque todo acesso a Session passa por
// Pin<&mut Session> atrás do Mutex único em AppState (src-tauri/src/main.rs):
// nunca há duas threads chamando um método ao mesmo tempo, só uso sequencial
// possivelmente em threads diferentes do pool do Tauri — exatamente o caso
// coberto pelo SQLITE_THREADSAFE=1 (modo serializado) já configurado no
// amálgama vendorizado (ver bridge/build.rs). Não implementamos Sync: como
// todo método usa `&mut` (nunca `&`), nunca existe acesso compartilhado a
// uma mesma Session; o Mutex<T> já é Sync automaticamente para qualquer T: Send.
unsafe impl Send for ffi::Session {}
TEMPLATE_EOF

# =========================================================================
# core-cli/
# =========================================================================

cat > "$TARGET_DIR/core-cli/Cargo.toml" <<'TEMPLATE_EOF'
[package]
name = "core-cli"
version = "0.1.0"
edition = "2021"

# Binário de diagnóstico: valida a ponte inteira (Rust -> cxx -> C++ ->
# SQLite) sem depender de Tauri/WebKitGTK — útil em CI Linux e para depurar
# a lógica sem esperar o build da GUI.
[dependencies]
bridge = { path = "../bridge" }
serde_json = "1"
TEMPLATE_EOF

cat > "$TARGET_DIR/core-cli/src/main.rs" <<'TEMPLATE_EOF'
// Valida a ponte inteira (Rust -> cxx -> C++ -> SQLite) rodando de verdade
// neste ambiente, sem tocar em Tauri/WebKitGTK: cria um item de exemplo,
// lista, calcula o resumo e (opcionalmente) grava fixtures JSON para o mock
// de desenvolvimento do frontend.
//
// `--db <caminho>`: usa esse arquivo em vez de um banco temporário — útil
// para popular de verdade uma pasta dados/ de produção.
// `--dump-fixtures <pasta>`: grava items.json e summary.json na pasta
// indicada (frontend/fixtures/, por convenção — nunca versionar, são dados
// de teste/reais).
use bridge::ffi;
use std::fs;
use std::path::PathBuf;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let db_override = args.windows(2).find(|w| w[0] == "--db").map(|w| w[1].clone());
    let dump_dir = args.windows(2).find(|w| w[0] == "--dump-fixtures").map(|w| w[1].clone());

    if let Err(e) = run(db_override.as_deref(), dump_dir.as_deref()) {
        eprintln!("[core-cli] FALHOU — {e}");
        std::process::exit(1);
    }
}

fn run(db_override: Option<&str>, dump_dir: Option<&str>) -> Result<(), String> {
    let db_path: PathBuf = match db_override {
        Some(p) => PathBuf::from(p),
        None => {
            let mut data_dir = std::env::temp_dir();
            data_dir.push("__APP_SLUG___core_cli_demo");
            fs::create_dir_all(&data_dir).map_err(|e| e.to_string())?;
            let p = data_dir.join("app.db");
            let _ = fs::remove_file(&p); // roda limpo a cada chamada
            p
        }
    };
    if let Some(parent) = db_path.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }

    println!("[core-cli] banco: {}", db_path.display());
    let mut session = ffi::open_session(db_path.to_str().unwrap()).map_err(|e| e.to_string())?;
    println!("[core-cli] sessão aberta (Rust -> cxx -> C++ -> SQLite) OK");

    let existentes = session.as_mut().unwrap().list_items_json().map_err(|e| e.to_string())?;
    let existentes: serde_json::Value = serde_json::from_str(&existentes).map_err(|e| e.to_string())?;
    if existentes.as_array().map(|a| a.is_empty()).unwrap_or(true) {
        let novo = ffi::ItemDto {
            id: "item-exemplo-1".to_string(),
            name: "Item de exemplo".to_string(),
            description: "Criado pelo core-cli na primeira execução".to_string(),
            value: 42.5,
            created_at: "2026-01-01T00:00:00Z".to_string(),
        };
        session.as_mut().unwrap().create_item(novo).map_err(|e| e.to_string())?;
        println!("[core-cli] item de exemplo criado (banco estava vazio)");
    }

    let items_json = session.as_mut().unwrap().list_items_json().map_err(|e| e.to_string())?;
    let summary_json = session.as_mut().unwrap().compute_summary_json().map_err(|e| e.to_string())?;
    let summary: serde_json::Value = serde_json::from_str(&summary_json).map_err(|e| e.to_string())?;

    println!("[core-cli] total de itens : {}", summary["totalItems"]);
    println!("[core-cli] valor total    : {}", summary["totalValue"]);

    if let Some(dir) = dump_dir {
        fs::create_dir_all(dir).map_err(|e| e.to_string())?;
        fs::write(format!("{dir}/items.json"), &items_json).map_err(|e| e.to_string())?;
        fs::write(format!("{dir}/summary.json"), &summary_json).map_err(|e| e.to_string())?;
        println!("[core-cli] fixtures gravadas em {dir}");
    }

    Ok(())
}
TEMPLATE_EOF

echo "==> Escrevendo casca Tauri (src-tauri/)"

# =========================================================================
# src-tauri/
# =========================================================================

cat > "$TARGET_DIR/src-tauri/Cargo.toml" <<'TEMPLATE_EOF'
[package]
name = "__APP_SLUG__-app"
version = "0.1.0"
edition = "2021"

# Casca fina: nenhuma lógica de negócio mora aqui — só abre a sessão (via
# `bridge`), resolve o caminho portátil no startup e expõe comandos Tauri
# que fazem lock()+delegam para a ponte cxx. É a ÚNICA parte do workspace
# que depende de WebKitGTK/WebView2 (por isso fica fora do build padrão do
# workspace — ver Cargo.toml raiz).
[build-dependencies]
tauri-build = { version = "2", features = [] }

[dependencies]
bridge = { path = "../bridge" }
cxx = "1"
tauri = { version = "2", features = [] }
serde = { version = "1", features = ["derive"] }

[[bin]]
name = "__APP_BIN__"
path = "src/main.rs"

# src-tauri não faz parte do workspace raiz (ver Cargo.toml lá) — por isso
# repete aqui o mesmo perfil de release, que é o que "cargo tauri build" usa.
[profile.release]
opt-level = 3
lto = true
codegen-units = 1
strip = true
TEMPLATE_EOF

cat > "$TARGET_DIR/src-tauri/build.rs" <<'TEMPLATE_EOF'
fn main() {
    tauri_build::build();
}
TEMPLATE_EOF

cat > "$TARGET_DIR/src-tauri/tauri.conf.json" <<'TEMPLATE_EOF'
{
  "$schema": "https://schema.tauri.app/config/2",
  "productName": "__APP_TITLE__",
  "version": "0.1.0",
  "identifier": "__APP_IDENTIFIER__",
  "build": {
    "frontendDist": "../frontend"
  },
  "app": {
    "withGlobalTauri": true,
    "windows": [
      {
        "label": "main",
        "title": "__APP_TITLE__",
        "width": 1280,
        "height": 800,
        "minWidth": 960,
        "minHeight": 600
      }
    ],
    "security": {
      "csp": "default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; script-src 'self'"
    }
  },
  "bundle": {
    "active": true,
    "targets": ["nsis"],
    "icon": ["icons/icon.ico"],
    "windows": {
      "webviewInstallMode": {
        "type": "downloadBootstrapper"
      },
      "nsis": {
        "installMode": "currentUser"
      }
    }
  }
}
TEMPLATE_EOF

cat > "$TARGET_DIR/src-tauri/capabilities/default.json" <<'TEMPLATE_EOF'
{
  "identifier": "default",
  "description": "Capacidades da janela principal — nenhum plugin externo, só os comandos definidos em commands.rs (não precisam de permissão explícita, ela é para plugins) e as operações básicas de janela/webview.",
  "windows": ["main"],
  "permissions": ["core:default"]
}
TEMPLATE_EOF

cat > "$TARGET_DIR/src-tauri/icons/README.md" <<'TEMPLATE_EOF'
Coloque aqui um `icon.ico` real antes do primeiro `cargo tauri build` — o
Tauri exige o arquivo referenciado em `tauri.conf.json` (`bundle.icon`),
mesmo que seja um ícone provisório. Gere um `.ico` a partir de um PNG
quadrado (ex.: com ImageMagick: `convert logo.png -define icon:auto-resize=256,128,64,48,32,16 icon.ico`).
TEMPLATE_EOF

cat > "$TARGET_DIR/src-tauri/src/main.rs" <<'TEMPLATE_EOF'
// Casca Tauri: nenhuma lógica de negócio aqui. Resolve o caminho portátil
// (dados/ ao lado do executável) no startup, abre a sessão via a ponte cxx,
// e expõe comandos finos que só fazem lock()+delegam (ver commands.rs).
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod dto;

use bridge::ffi;
use std::sync::Mutex;
use tauri::Manager;

/// Estado da aplicação: ou há uma sessão pronta, ou há um erro de
/// inicialização (tipicamente a pasta `dados/` não pôde ser criada) que o
/// frontend mostra como tela de erro com botão "Tentar novamente" — nunca
/// um crash silencioso, nunca um fallback para outro local escondido
/// (quebraria a garantia de portabilidade).
pub struct AppState {
    pub session: Mutex<Option<cxx::UniquePtr<ffi::Session>>>,
    pub init_error: Mutex<Option<String>>,
}

impl AppState {
    fn empty() -> Self {
        AppState { session: Mutex::new(None), init_error: Mutex::new(None) }
    }
}

/// Resolve dados/ + abre o SQLite; popula `state.session` ou `state.init_error`.
/// Chamado no startup e de novo pelo comando `retry_init` (o botão "Tentar
/// novamente" da tela de erro).
pub fn init_session(state: &AppState) {
    let result = ffi::resolve_data_dir_default().and_then(|data_dir| {
        let db_path = format!("{data_dir}/app.db");
        ffi::open_session(&db_path)
    });

    match result {
        Ok(session) => {
            *state.session.lock().unwrap() = Some(session);
            *state.init_error.lock().unwrap() = None;
        }
        Err(e) => {
            *state.session.lock().unwrap() = None;
            *state.init_error.lock().unwrap() = Some(e.what().to_string());
        }
    }
}

fn main() {
    tauri::Builder::default()
        .manage(AppState::empty())
        .setup(|app| {
            let state: tauri::State<AppState> = app.state();
            init_session(&state);
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::app_status,
            commands::retry_init,
            commands::list_items,
            commands::create_item,
            commands::update_item,
            commands::delete_item,
            commands::compute_summary,
            commands::backup,
            commands::restore_backup,
        ])
        .run(tauri::generate_context!())
        .expect("erro ao iniciar o __APP_TITLE__");
}
TEMPLATE_EOF

cat > "$TARGET_DIR/src-tauri/src/dto.rs" <<'TEMPLATE_EOF'
// DTOs desserializáveis do JSON que o frontend envia via invoke(). Não dá
// para usar bridge::ffi::ItemDto diretamente como parâmetro de comando
// Tauri — é uma struct gerada pelo `cxx` e não implementa
// `serde::Deserialize`. Esta é a camada fina de conversão: JSON camelCase
// (mesmo formato usado no resto da API) -> struct Rust -> DTO da ponte cxx.
use bridge::ffi;
use serde::Deserialize;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ItemInput {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub value: f64,
    pub created_at: String,
}

impl From<ItemInput> for ffi::ItemDto {
    fn from(i: ItemInput) -> Self {
        ffi::ItemDto {
            id: i.id,
            name: i.name,
            description: i.description,
            value: i.value,
            created_at: i.created_at,
        }
    }
}
TEMPLATE_EOF

cat > "$TARGET_DIR/src-tauri/src/commands.rs" <<'TEMPLATE_EOF'
// Comandos Tauri: cada um só faz lock() no estado e delega para a ponte
// cxx — nenhuma regra de negócio mora aqui (isso é papel do core-cpp).
use crate::dto::ItemInput;
use crate::AppState;
use tauri::State;

/// Executa `f` com a sessão já destrancada; se a sessão nunca abriu (falha
/// de inicialização — tipicamente a pasta `dados/` sem permissão de
/// escrita), devolve o erro já formatado para o usuário em vez de um pânico.
fn with_session<T>(
    state: &State<AppState>,
    f: impl FnOnce(std::pin::Pin<&mut bridge::ffi::Session>) -> Result<T, cxx::Exception>,
) -> Result<T, String> {
    let mut guard = state.session.lock().map_err(|_| "estado interno corrompido".to_string())?;
    match guard.as_mut() {
        Some(session) => f(session.pin_mut()).map_err(|e| e.what().to_string()),
        None => Err(state
            .init_error
            .lock()
            .unwrap()
            .clone()
            .unwrap_or_else(|| "aplicativo não inicializado".to_string())),
    }
}

#[tauri::command]
pub fn app_status(state: State<AppState>) -> Result<(), String> {
    if state.session.lock().unwrap().is_some() {
        Ok(())
    } else {
        Err(state.init_error.lock().unwrap().clone().unwrap_or_default())
    }
}

#[tauri::command]
pub fn retry_init(state: State<AppState>) -> Result<(), String> {
    crate::init_session(&state);
    app_status(state)
}

#[tauri::command]
pub fn list_items(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_items_json())
}

#[tauri::command]
pub fn create_item(state: State<AppState>, item: ItemInput) -> Result<String, String> {
    with_session(&state, |s| s.create_item(item.into()))
}

#[tauri::command]
pub fn update_item(state: State<AppState>, item: ItemInput) -> Result<String, String> {
    with_session(&state, |s| s.update_item(item.into()))
}

#[tauri::command]
pub fn delete_item(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_item(&id))
}

#[tauri::command]
pub fn compute_summary(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.compute_summary_json())
}

#[tauri::command]
pub fn backup(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.backup_json())
}

#[tauri::command]
pub fn restore_backup(state: State<AppState>, payload: String) -> Result<(), String> {
    with_session(&state, |s| s.restore_from_json(&payload))
}
TEMPLATE_EOF

echo "==> Escrevendo frontend (frontend/)"

# =========================================================================
# frontend/
# =========================================================================

cat > "$TARGET_DIR/frontend/index.html" <<'TEMPLATE_EOF'
<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>__APP_TITLE__</title>
<link rel="stylesheet" href="css/tokens.css" />
<link rel="stylesheet" href="css/layout.css" />
<link rel="stylesheet" href="css/components.css" />
<link rel="stylesheet" href="css/print.css" />
</head>
<body>

<div id="startupError" class="startup-error" style="display:none;">
  <div class="big">⚠</div>
  <h2>Não foi possível iniciar o aplicativo</h2>
  <p class="muted">A pasta de dados não pôde ser criada ou aberta.</p>
  <pre id="startupErrorDetail"></pre>
  <button class="btn-primary" id="btnRetryInit">Tentar novamente</button>
</div>

<div class="app-shell" id="appShell" style="display:none;">
  <aside class="sidebar">
    <div class="sidebar-brand">
      <div class="logo">__APP_LOGO__</div>
      <div>
        <div class="name">__APP_TITLE__</div>
      </div>
    </div>
    <nav class="sidebar-nav" id="sidebarNav">
      <button class="nav-item active" data-view="dashboard"><span class="ico">📊</span> Resumo</button>
      <button class="nav-item" data-view="items"><span class="ico">🗂</span> Itens</button>
      <button class="nav-item" data-view="importExport"><span class="ico">⇄</span> Importar / Exportar</button>
    </nav>
    <div class="sidebar-foot">Dados salvos localmente em<br/><code>dados/app.db</code>, ao lado do programa.</div>
    <div class="app-copyright">© __APP_YEAR__ __ORG_NAME__.<br/>Todos os direitos reservados.</div>
  </aside>

  <main class="content">

    <!-- ============================== RESUMO ============================== -->
    <section class="view active" id="view-dashboard">
      <div class="content-header">
        <div>
          <h1>Resumo</h1>
          <div class="sub">Indicadores agregados dos itens cadastrados</div>
        </div>
      </div>

      <div class="stat-grid" id="dashStatGrid"></div>

      <div class="info-box" style="max-width:640px;">
        Esta tela é o exemplo de "view de agregação" do esqueleto: chama
        <code>api.computeSummary()</code>, que percorre
        <code>compute_summary</code> (Tauri) → <code>compute_summary_json</code>
        (ponte cxx) → <code>computeItemSummary</code> (core-cpp). Substitua
        pelos indicadores reais do seu domínio.
      </div>
    </section>

    <!-- ============================== ITENS ============================== -->
    <section class="view" id="view-items">
      <div class="content-header"><div><h1>Itens</h1><div class="sub">Entidade de exemplo — CRUD completo</div></div></div>
      <div class="toolbar">
        <button class="btn-primary" id="btnNovoItem">＋ Novo Item</button>
        <span class="spacer"></span>
        <input type="text" id="filtroItemNome" placeholder="Buscar item..." />
      </div>
      <div class="panel"><div style="overflow-x:auto;">
        <table>
          <thead><tr><th>Nome</th><th>Descrição</th><th class="num">Valor</th><th>Criado em</th><th>Ações</th></tr></thead>
          <tbody id="itemsTbody"></tbody>
        </table>
      </div></div>
    </section>

    <!-- ============================== IMPORTAR / EXPORTAR ============================== -->
    <section class="view" id="view-importExport">
      <div class="content-header"><div><h1>Importar / Exportar</h1><div class="sub">Backup completo dos dados</div></div></div>
      <div class="panel" style="padding:20px;display:flex;flex-direction:column;gap:14px;max-width:560px;">
        <div>
          <h2 style="margin-bottom:6px;">Backup (JSON)</h2>
          <p class="muted" style="margin:0 0 10px;">Contém todos os itens — mesmo formato usado por <code>restoreFromJson</code> no núcleo C++.</p>
          <div style="display:flex;gap:8px;">
            <button class="btn-primary" id="btnExportarBackup">⬇ Exportar backup</button>
            <button class="btn-outline" id="btnImportarBackup">⬆ Importar backup</button>
            <input type="file" id="importFile" accept=".json" style="display:none" />
          </div>
        </div>
      </div>
    </section>

  </main>
</div>

<div id="toast"></div>

<!-- Documento montado na hora de imprimir; invisível na tela (ver css/print.css) -->
<div id="printArea" aria-hidden="true"></div>

<!-- MODAL: Item -->
<div class="overlay" id="modalItem"><div class="modal">
  <div class="modal-head"><h3 id="modalItemTitulo">Novo Item</h3><button class="btn-ghost" data-close="modalItem">✕</button></div>
  <div class="modal-body">
    <input type="hidden" id="itemId" />
    <div class="field"><label>Nome *</label><input type="text" id="itemNome" /></div>
    <div class="field"><label>Descrição</label><input type="text" id="itemDescricao" /></div>
    <div class="field"><label>Valor (R$)</label><input type="number" id="itemValor" min="0" step="any" value="0" /></div>
  </div>
  <div class="modal-foot"><button class="btn-outline" data-close="modalItem">Cancelar</button><button class="btn-primary" id="btnSalvarItem">Salvar</button></div>
</div></div>

<script type="module" src="js/app.js"></script>
</body>
</html>
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/css/tokens.css" <<'TEMPLATE_EOF'
/* Tokens de design — troque as cores aqui para trocar a identidade visual
   inteira do app sem tocar em nenhum outro CSS. Aplicado de ponta a ponta:
   telas, componentes e impressão (ver css/print.css). */
:root {
  color-scheme: light;

  /* Superfícies */
  --page-bg: #f4f6f8;
  --surface: #ffffff;
  --surface-sunken: #eef1f4;
  --border: #e1e4e8;
  --border-strong: #c6ccd2;

  /* Tinta (texto) */
  --ink-primary: #2b2f33;
  --ink-secondary: #6b7280;
  --ink-muted: #9aa1a9;
  --ink-on-accent: #ffffff;

  /* Acento — cor de marca/ações principais */
  --accent: #1b3a5c;
  --accent-hover: #2e6ba6;
  --accent-soft: #e7edf3;
  --accent-secondary: #6c8ca8;

  /* Estado */
  --success: #2e8b57;
  --success-soft: #e6f3ec;
  --warning: #e0a526;
  --warning-soft: #faf0da;
  --danger: #c0392b;
  --danger-soft: #f8e6e3;

  /* Espaçamento (escala 4px) */
  --sp-1: 4px;
  --sp-2: 8px;
  --sp-3: 12px;
  --sp-4: 16px;
  --sp-5: 20px;
  --sp-6: 24px;
  --sp-8: 32px;

  /* Raio e sombra — sombra só para overlays transitórios (modal/toast) */
  --radius: 8px;
  --radius-sm: 6px;
  --shadow-overlay: 0 12px 32px rgba(0, 0, 0, 0.18);

  /* Tipografia — pilha de sistema (reforça sensação de app nativo) */
  --font-sans: -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;

  --sidebar-w: 224px;
}

* { box-sizing: border-box; }
html, body { height: 100%; }
body {
  margin: 0;
  font-family: var(--font-sans);
  background: var(--page-bg);
  color: var(--ink-primary);
  font-size: 13.5px;
  line-height: 1.45;
}
h1, h2, h3, h4 { margin: 0; font-weight: 700; }
button, input, select, textarea { font-family: inherit; font-size: inherit; }
code { background: var(--surface-sunken); padding: 1px 5px; border-radius: 4px; }
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/css/layout.css" <<'TEMPLATE_EOF'
/* Casca do app: barra lateral fixa + conteúdo. */
.app-shell {
  display: grid;
  grid-template-columns: var(--sidebar-w) 1fr;
  height: 100vh;
  overflow: hidden;
}

.sidebar {
  background: var(--surface);
  border-right: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  padding: var(--sp-4) var(--sp-3);
  gap: var(--sp-6);
}

.sidebar-brand { display: flex; align-items: center; gap: var(--sp-3); padding: var(--sp-2); }
.sidebar-brand .logo {
  width: 30px; height: 30px; border-radius: var(--radius-sm);
  background: var(--accent); color: #fff;
  display: flex; align-items: center; justify-content: center;
  font-weight: 800; font-size: 12.5px; flex: none;
}
.sidebar-brand .name { font-size: 13px; font-weight: 700; color: var(--ink-primary); line-height: 1.25; }

.sidebar-nav { display: flex; flex-direction: column; gap: 2px; }
.nav-item {
  display: flex; align-items: center; gap: var(--sp-3);
  padding: var(--sp-2) var(--sp-3); border-radius: var(--radius-sm);
  border: none; background: transparent; color: var(--ink-secondary);
  font-size: 13px; font-weight: 600; text-align: left; cursor: pointer;
  position: relative; width: 100%;
}
.nav-item:hover { background: var(--surface-sunken); color: var(--ink-primary); }
.nav-item .ico { width: 16px; text-align: center; flex: none; font-size: 14px; }
.nav-item.active { background: var(--accent-soft); color: var(--accent-hover); }
.nav-item.active::before {
  content: ""; position: absolute; left: -12px; top: 8px; bottom: 8px;
  width: 3px; border-radius: 2px; background: var(--accent);
}

.sidebar-foot { margin-top: auto; font-size: 10.5px; color: var(--ink-muted); padding: var(--sp-2); }
.app-copyright { font-size: 9.5px; line-height: 1.4; color: var(--ink-muted); padding: 0 var(--sp-2) var(--sp-2); }

.content { overflow-y: auto; padding: var(--sp-6) var(--sp-8) var(--sp-8); }
.content-header { display: flex; align-items: baseline; justify-content: space-between; gap: var(--sp-4); margin-bottom: var(--sp-5); flex-wrap: wrap; }
.content-header h1 { font-size: 19px; }
.content-header .sub { font-size: 12px; color: var(--ink-secondary); margin-top: 2px; }

/* Mecanismo de troca de view: nenhuma é removida do DOM, só perde .active */
.view { display: none; }
.view.active { display: block; }

.section-title { font-size: 11px; font-weight: 700; color: var(--ink-secondary); text-transform: uppercase; letter-spacing: 0.04em; margin: var(--sp-6) 0 var(--sp-3); }
.section-title:first-of-type { margin-top: 0; }

.startup-error { max-width: 480px; margin: 15vh auto 0; text-align: center; padding: var(--sp-6); }
.startup-error .big { font-size: 42px; }
.startup-error pre { text-align: left; background: var(--surface-sunken); padding: var(--sp-3); border-radius: var(--radius-sm); overflow-x: auto; font-size: 11.5px; }

@media (max-width: 860px) {
  .app-shell { grid-template-columns: 1fr; }
  .sidebar { display: none; }
}
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/css/components.css" <<'TEMPLATE_EOF'
/* Vocabulário visual reutilizável entre todas as views: botões, cards,
   painéis, tabelas, formulários, modal, toast. */

.btn-primary, .btn-outline, .btn-ghost, .btn-danger {
  display: inline-flex; align-items: center; gap: 6px;
  padding: 7px 14px; border-radius: var(--radius-sm);
  font-size: 12.5px; font-weight: 600; cursor: pointer; border: 1px solid transparent;
}
.btn-primary { background: var(--accent); color: var(--ink-on-accent); }
.btn-primary:hover { background: var(--accent-hover); }
.btn-outline { background: var(--surface); border-color: var(--border-strong); color: var(--ink-primary); }
.btn-outline:hover { background: var(--surface-sunken); }
.btn-ghost { background: transparent; color: var(--ink-secondary); }
.btn-ghost:hover { background: var(--surface-sunken); color: var(--ink-primary); }
.btn-danger { background: var(--danger-soft); color: var(--danger); }
.btn-danger:hover { background: var(--danger); color: #fff; }
.btn-sm { padding: 4px 10px; font-size: 11.5px; }

.toolbar { display: flex; align-items: center; gap: var(--sp-3); margin-bottom: var(--sp-4); flex-wrap: wrap; }
.toolbar .spacer { flex: 1; }
.toolbar input[type="text"], .toolbar select {
  padding: 6px 10px; border: 1px solid var(--border-strong); border-radius: var(--radius-sm); background: var(--surface);
}

.field { display: flex; flex-direction: column; gap: 4px; margin-bottom: var(--sp-3); }
.field label { font-size: 11.5px; font-weight: 600; color: var(--ink-secondary); }
.field input, .field select {
  padding: 8px 10px; border: 1px solid var(--border-strong); border-radius: var(--radius-sm); background: var(--surface);
}
.field-row { display: flex; gap: var(--sp-3); }
.field-row .field { flex: 1; }

.card { background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius); }
.panel { background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius); overflow: hidden; margin-bottom: var(--sp-4); }
.panel-head { display: flex; align-items: center; justify-content: space-between; padding: var(--sp-3) var(--sp-4); border-bottom: 1px solid var(--border); }

.stat-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: var(--sp-3); margin-bottom: var(--sp-5); }
.stat-tile { background: var(--surface); border: 1px solid var(--border); border-radius: var(--radius); padding: var(--sp-4); }
.stat-tile-label { font-size: 11px; font-weight: 700; text-transform: uppercase; color: var(--ink-secondary); }
.stat-tile-value { font-size: 24px; font-weight: 800; color: var(--ink-primary); margin-top: 4px; }

table { width: 100%; border-collapse: collapse; font-size: 12.5px; }
thead th { text-align: left; padding: var(--sp-2) var(--sp-4); background: var(--surface-sunken); color: var(--ink-secondary); font-size: 11px; text-transform: uppercase; font-weight: 700; }
tbody td { padding: var(--sp-2) var(--sp-4); border-top: 1px solid var(--border); }
th.num, td.num { text-align: right; }
.muted { color: var(--ink-muted); }

.info-box { background: var(--accent-soft); color: var(--accent); border-radius: var(--radius-sm); padding: var(--sp-3); font-size: 12px; }

.overlay {
  display: none; position: fixed; inset: 0; background: rgba(0,0,0,.35);
  align-items: center; justify-content: center; z-index: 50;
}
.overlay.open { display: flex; }
.modal { background: var(--surface); border-radius: var(--radius); width: 420px; max-width: 92vw; box-shadow: var(--shadow-overlay); }
.modal-head { display: flex; align-items: center; justify-content: space-between; padding: var(--sp-4); border-bottom: 1px solid var(--border); }
.modal-body { padding: var(--sp-4); max-height: 70vh; overflow-y: auto; }
.modal-foot { display: flex; justify-content: flex-end; gap: var(--sp-2); padding: var(--sp-4); border-top: 1px solid var(--border); }

#toast {
  position: fixed; bottom: var(--sp-5); right: var(--sp-5);
  display: flex; flex-direction: column; gap: var(--sp-2); z-index: 100;
}
#toast .toast-item {
  background: var(--ink-primary); color: #fff; padding: 10px 16px;
  border-radius: var(--radius-sm); font-size: 12.5px; box-shadow: var(--shadow-overlay);
}
#toast .toast-item.error { background: var(--danger); }
#toast .toast-item.success { background: var(--success); }
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/css/print.css" <<'TEMPLATE_EOF'
/* Esconde o app inteiro e mostra só #printArea ao imprimir — o mesmo
   documento na tela (sidebar, filtros, botões) não deveria ir pro papel.
   Monte o conteúdo de #printArea em JS (ver js/print.js) antes de chamar
   window.print(). */
@media print {
  .app-shell, #toast, .startup-error { display: none !important; }
  #printArea { display: block !important; }
  body { background: #fff; }
}
#printArea { display: none; }
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/js/format.js" <<'TEMPLATE_EOF'
// Formatação centralizada — nunca formatar moeda/data "na unha" espalhado
// pelas views, para o formato ficar consistente em toda a tela.
export function formatCurrency(value) {
  return (value || 0).toLocaleString('pt-BR', { style: 'currency', currency: 'BRL' });
}

export function formatDate(iso) {
  if (!iso) return '';
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return iso;
  return d.toLocaleDateString('pt-BR') + ' ' + d.toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' });
}

export function uuid() {
  return crypto.randomUUID();
}

export function nowIso() {
  return new Date().toISOString();
}
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/js/api.js" <<'TEMPLATE_EOF'
// Ponte única com o backend: usa window.__TAURI__.core.invoke quando
// disponível (rodando de verdade dentro do app), ou um mock alimentado por
// fixtures quando aberto direto num navegador comum — é o que permite
// revisar/ajustar o layout inteiro sem precisar compilar Rust/C++/Tauri.
const TAURI = typeof window !== 'undefined' && window.__TAURI__ && window.__TAURI__.core;

let mockHandler = null;
export function useMock(handler) {
  mockHandler = handler;
}

async function call(cmd, args) {
  if (TAURI) return window.__TAURI__.core.invoke(cmd, args);
  if (!mockHandler) throw new Error('Nenhum backend disponível (nem Tauri, nem mock configurado).');
  return mockHandler(cmd, args);
}

export const api = {
  appStatus: () => call('app_status'),
  retryInit: () => call('retry_init'),

  listItems: async () => JSON.parse(await call('list_items')),
  createItem: async (item) => JSON.parse(await call('create_item', { item })),
  updateItem: async (item) => JSON.parse(await call('update_item', { item })),
  deleteItem: (id) => call('delete_item', { id }),

  computeSummary: async () => JSON.parse(await call('compute_summary')),

  backup: async () => JSON.parse(await call('backup')),
  restoreBackup: (payload) => call('restore_backup', { payload }),
};
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/js/devMock.js" <<'TEMPLATE_EOF'
// Mock em memória do backend, usado só quando o frontend é aberto direto
// num navegador (fora do Tauri) — ver js/api.js. Tenta carregar fixtures
// reais geradas por `cargo run -p core-cli -- --dump-fixtures frontend/fixtures`;
// se não existirem, começa com uma lista vazia (ainda dá pra criar itens
// pela UI, só não persiste entre recargas).
import { useMock } from './api.js';

async function loadFixture(name) {
  try {
    const res = await fetch(`fixtures/${name}.json`);
    if (!res.ok) return null;
    return await res.json();
  } catch {
    return null;
  }
}

export async function installDevMock() {
  const items = (await loadFixture('items')) || [];

  useMock(async (cmd, args) => {
    switch (cmd) {
      case 'app_status':
      case 'retry_init':
        return null;

      case 'list_items':
        return JSON.stringify(items);

      case 'create_item':
        items.unshift(args.item);
        return JSON.stringify(args.item);

      case 'update_item': {
        const idx = items.findIndex((p) => p.id === args.item.id);
        if (idx >= 0) items[idx] = { ...items[idx], ...args.item };
        return JSON.stringify(args.item);
      }

      case 'delete_item': {
        const idx = items.findIndex((p) => p.id === args.id);
        if (idx >= 0) items.splice(idx, 1);
        return null;
      }

      case 'compute_summary': {
        const totalItems = items.length;
        const totalValue = items.reduce((acc, p) => acc + (p.value || 0), 0);
        return JSON.stringify({ totalItems, totalValue });
      }

      case 'backup':
        return JSON.stringify({ items });

      case 'restore_backup': {
        const payload = JSON.parse(args.payload);
        items.length = 0;
        items.push(...(payload.items || []));
        return null;
      }

      default:
        throw new Error(`[devMock] comando não implementado: ${cmd}`);
    }
  });
}
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/js/components/modal.js" <<'TEMPLATE_EOF'
// Componente de modal: qualquer elemento com [data-close="idDoModal"] fecha
// o overlay correspondente; clicar fora do card (no próprio overlay) também
// fecha. Nenhuma view precisa reimplementar essa lógica.
export function openModal(id) {
  document.getElementById(id).classList.add('open');
}

export function closeModal(id) {
  document.getElementById(id).classList.remove('open');
}

export function installOverlayClickToClose() {
  document.querySelectorAll('.overlay').forEach((overlay) => {
    overlay.addEventListener('click', (e) => {
      if (e.target === overlay) overlay.classList.remove('open');
    });
  });
  document.querySelectorAll('[data-close]').forEach((btn) => {
    btn.addEventListener('click', () => closeModal(btn.dataset.close));
  });
}
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/js/components/toast.js" <<'TEMPLATE_EOF'
// Notificação temporária no canto da tela — usar para confirmar sucesso
// ("Item salvo") ou mostrar erro sem interromper o fluxo com um alert().
export function toast(message, kind = 'default') {
  const host = document.getElementById('toast');
  const el = document.createElement('div');
  el.className = 'toast-item' + (kind !== 'default' ? ` ${kind}` : '');
  el.textContent = message;
  host.appendChild(el);
  setTimeout(() => el.remove(), 3200);
}
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/js/views/items.js" <<'TEMPLATE_EOF'
// View de exemplo: CRUD completo da entidade "Item". Ao adaptar para o seu
// domínio, copie este arquivo como ponto de partida para cada entidade real.
import { api } from '../api.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { formatCurrency, formatDate, uuid, nowIso } from '../format.js';

let items = [];

export async function initItems() {
  wireToolbar();
  wireModal();
  await reload();
}

export async function reload() {
  items = await api.listItems();
  render();
}

function wireToolbar() {
  document.getElementById('btnNovoItem').addEventListener('click', () => openEditor(null));
  document.getElementById('filtroItemNome').addEventListener('input', render);
}

function wireModal() {
  document.getElementById('btnSalvarItem').addEventListener('click', save);
}

function openEditor(item) {
  document.getElementById('itemId').value = item ? item.id : '';
  document.getElementById('itemNome').value = item ? item.name : '';
  document.getElementById('itemDescricao').value = item ? item.description : '';
  document.getElementById('itemValor').value = item ? item.value : 0;
  document.getElementById('modalItemTitulo').textContent = item ? 'Editar Item' : 'Novo Item';
  openModal('modalItem');
}

async function save() {
  const id = document.getElementById('itemId').value || uuid();
  const name = document.getElementById('itemNome').value.trim();
  if (!name) return toast('Informe o nome do item.', 'error');

  const payload = {
    id,
    name,
    description: document.getElementById('itemDescricao').value.trim(),
    value: Number(document.getElementById('itemValor').value) || 0,
    createdAt: nowIso(),
  };

  try {
    const existing = items.some((p) => p.id === id);
    if (existing) await api.updateItem(payload);
    else await api.createItem(payload);
    closeModal('modalItem');
    toast('Item salvo.', 'success');
    await reload();
  } catch (err) {
    toast(String(err), 'error');
  }
}

async function remove(id) {
  if (!confirm('Excluir este item?')) return;
  try {
    await api.deleteItem(id);
    toast('Item excluído.', 'success');
    await reload();
  } catch (err) {
    toast(String(err), 'error');
  }
}

function render() {
  const filtro = document.getElementById('filtroItemNome').value.trim().toLowerCase();
  const tbody = document.getElementById('itemsTbody');
  const rows = items.filter((p) => !filtro || p.name.toLowerCase().includes(filtro));

  tbody.innerHTML = rows.map((p) => `
    <tr>
      <td>${escapeHtml(p.name)}</td>
      <td class="muted">${escapeHtml(p.description || '')}</td>
      <td class="num">${formatCurrency(p.value)}</td>
      <td class="muted">${formatDate(p.createdAt)}</td>
      <td>
        <button class="btn-ghost btn-sm" data-edit="${p.id}">Editar</button>
        <button class="btn-ghost btn-sm" data-remove="${p.id}">Excluir</button>
      </td>
    </tr>
  `).join('') || `<tr><td colspan="5" class="muted" style="text-align:center;padding:24px;">Nenhum item cadastrado.</td></tr>`;

  tbody.querySelectorAll('[data-edit]').forEach((btn) => {
    btn.addEventListener('click', () => openEditor(items.find((p) => p.id === btn.dataset.edit)));
  });
  tbody.querySelectorAll('[data-remove]').forEach((btn) => {
    btn.addEventListener('click', () => remove(btn.dataset.remove));
  });
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/js/views/dashboard.js" <<'TEMPLATE_EOF'
// View de exemplo: tela de agregação, lê compute_summary() e renderiza
// indicadores — mesmo padrão de qualquer relatório/dashboard do app.
import { api } from '../api.js';
import { formatCurrency } from '../format.js';

export async function initDashboard() {
  await renderSummary();
}

export async function reload() {
  await renderSummary();
}

async function renderSummary() {
  const summary = await api.computeSummary();
  document.getElementById('dashStatGrid').innerHTML = `
    <div class="stat-tile">
      <div class="stat-tile-label">Total de itens</div>
      <div class="stat-tile-value">${summary.totalItems}</div>
    </div>
    <div class="stat-tile">
      <div class="stat-tile-label">Valor total</div>
      <div class="stat-tile-value">${formatCurrency(summary.totalValue)}</div>
    </div>
  `;
}
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/js/views/importExport.js" <<'TEMPLATE_EOF'
// View de exemplo: exporta/importa o backup JSON inteiro via api.backup()/
// api.restoreBackup() — usa a API padrão de download/upload de arquivo do
// navegador, sem nenhuma dependência externa.
import { api } from '../api.js';
import { toast } from '../components/toast.js';

export function initImportExport() {
  document.getElementById('btnExportarBackup').addEventListener('click', exportBackup);
  document.getElementById('btnImportarBackup').addEventListener('click', () => {
    document.getElementById('importFile').click();
  });
  document.getElementById('importFile').addEventListener('change', importBackup);
}

async function exportBackup() {
  try {
    const data = await api.backup();
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `backup-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
    toast('Backup exportado.', 'success');
  } catch (err) {
    toast(String(err), 'error');
  }
}

async function importBackup(e) {
  const file = e.target.files[0];
  if (!file) return;
  if (!confirm('Isso substitui TODOS os dados atuais pelo conteúdo do arquivo. Continuar?')) {
    e.target.value = '';
    return;
  }
  try {
    const text = await file.text();
    await api.restoreBackup(text);
    toast('Backup importado. Recarregue as telas para ver os dados novos.', 'success');
  } catch (err) {
    toast(String(err), 'error');
  } finally {
    e.target.value = '';
  }
}
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/js/app.js" <<'TEMPLATE_EOF'
import { api } from './api.js';
import { installOverlayClickToClose } from './components/modal.js';
import { initDashboard, reload as reloadDashboard } from './views/dashboard.js';
import { initItems, reload as reloadItems } from './views/items.js';
import { initImportExport } from './views/importExport.js';

const initialized = new Set();

async function switchView(view) {
  document.querySelectorAll('.nav-item').forEach((b) => b.classList.toggle('active', b.dataset.view === view));
  document.querySelectorAll('.view').forEach((s) => s.classList.remove('active'));
  document.getElementById('view-' + view).classList.add('active');

  if (!initialized.has(view)) {
    initialized.add(view);
    if (view === 'dashboard') await initDashboard();
    else if (view === 'items') await initItems();
    else if (view === 'importExport') initImportExport();
  } else {
    // views já inicializadas recarregam os dados ao voltar a ficar visíveis
    if (view === 'dashboard') await reloadDashboard();
    else if (view === 'items') await reloadItems();
  }
}

function wireSidebar() {
  document.querySelectorAll('.nav-item').forEach((btn) => {
    btn.addEventListener('click', () => switchView(btn.dataset.view));
  });
}

async function boot() {
  try {
    // Fora do Tauri (aberto direto num navegador): instala o mock de
    // desenvolvimento alimentado por fixtures reais, só para revisão visual.
    if (!(window.__TAURI__ && window.__TAURI__.core)) {
      const { installDevMock } = await import('./devMock.js');
      await installDevMock();
    }

    installOverlayClickToClose();
    wireSidebar();

    try {
      await api.appStatus();
    } catch (err) {
      showStartupError(String(err));
      document.getElementById('btnRetryInit').addEventListener('click', async () => {
        try {
          await api.retryInit();
          location.reload();
        } catch (e2) {
          showStartupError(String(e2));
        }
      });
      return;
    }

    document.getElementById('startupError').style.display = 'none';
    document.getElementById('appShell').style.display = 'grid';
    await switchView('dashboard');
  } catch (err) {
    // Qualquer falha inesperada aqui nunca deve resultar em tela branca
    // silenciosa — sempre mostra algo acionável, mesmo com mensagem genérica.
    showStartupError(String(err && err.stack ? err.stack : err));
  }
}

function showStartupError(detail) {
  document.getElementById('appShell').style.display = 'none';
  document.getElementById('startupError').style.display = 'block';
  document.getElementById('startupErrorDetail').textContent = detail;
}

boot();
TEMPLATE_EOF

cat > "$TARGET_DIR/frontend/fixtures/README.md" <<'TEMPLATE_EOF'
# fixtures/

Dados usados pelo mock de desenvolvimento (`js/devMock.js`) quando o
frontend é aberto direto num navegador, fora do Tauri. Gere/regenere com:

```bash
cargo run -p core-cli -- --dump-fixtures frontend/fixtures
```

Os `.json` gerados aqui **não são versionados** (ver `.gitignore`) — são
dados de teste, regeneráveis a qualquer momento.
TEMPLATE_EOF

echo "==> Escrevendo ferramentas auxiliares (tools/)"

# =========================================================================
# tools/
# =========================================================================

cat > "$TARGET_DIR/tools/fetch_third_party.sh" <<'TEMPLATE_EOF'
#!/usr/bin/env bash
# Baixa as bibliotecas de terceiros vendorizadas exigidas pelo build de
# bridge/ e pelos testes de core-cpp/tests/: SQLite (amálgama), nlohmann/json
# (single header) e doctest (single header). Rode uma vez a partir da raiz
# do projeto, ou sempre que quiser atualizar as versões pinadas abaixo.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

THIRD_PARTY="core-cpp/third_party"
SQLITE_YEAR="2025"
SQLITE_AMALGAMATION="sqlite-amalgamation-3480000"
NLOHMANN_VERSION="v3.11.3"
DOCTEST_VERSION="v2.4.11"

echo "==> SQLite (${SQLITE_AMALGAMATION})"
if [[ ! -f "$THIRD_PARTY/sqlite/sqlite3.c" ]]; then
  tmp="$(mktemp -d)"
  curl -fsSL "https://www.sqlite.org/${SQLITE_YEAR}/${SQLITE_AMALGAMATION}.zip" -o "$tmp/sqlite.zip"
  unzip -q "$tmp/sqlite.zip" -d "$tmp"
  cp "$tmp/${SQLITE_AMALGAMATION}/sqlite3.c" "$THIRD_PARTY/sqlite/sqlite3.c"
  cp "$tmp/${SQLITE_AMALGAMATION}/sqlite3.h" "$THIRD_PARTY/sqlite/sqlite3.h"
  rm -rf "$tmp"
else
  echo "    já presente, pulando"
fi

echo "==> nlohmann/json (${NLOHMANN_VERSION})"
if [[ ! -f "$THIRD_PARTY/nlohmann/json.hpp" ]]; then
  curl -fsSL "https://github.com/nlohmann/json/releases/download/${NLOHMANN_VERSION}/json.hpp" \
    -o "$THIRD_PARTY/nlohmann/json.hpp"
else
  echo "    já presente, pulando"
fi

echo "==> doctest (${DOCTEST_VERSION})"
if [[ ! -f "$THIRD_PARTY/doctest.h" ]]; then
  curl -fsSL "https://raw.githubusercontent.com/doctest/doctest/${DOCTEST_VERSION}/doctest/doctest.h" \
    -o "$THIRD_PARTY/doctest.h"
else
  echo "    já presente, pulando"
fi

echo "==> pronto. Confira as licenças de cada biblioteca antes de distribuir o app."
TEMPLATE_EOF
chmod +x "$TARGET_DIR/tools/fetch_third_party.sh"

echo "==> Substituindo placeholders"

# =========================================================================
# Substituição final de placeholders em todos os arquivos gerados
# =========================================================================

APP_LOGO="$(echo "$APP_TITLE" | tr -d '[:space:]' | cut -c1-2 | tr '[:lower:]' '[:upper:]')"

find "$TARGET_DIR" -type f \( \
    -name "*.rs" -o -name "*.cpp" -o -name "*.hpp" -o -name "*.toml" \
    -o -name "*.json" -o -name "*.md" -o -name "*.js" -o -name "*.html" \
    -o -name "*.css" -o -name "*.sh" -o -name "*.yml" \
  \) -print0 | xargs -0 sed -i \
  -e "s|__APP_TITLE__|$APP_TITLE|g" \
  -e "s|__APP_SLUG__|$APP_SLUG|g" \
  -e "s|__APP_NS__|$APP_NS|g" \
  -e "s|__APP_BIN__|$APP_BIN|g" \
  -e "s|__APP_IDENTIFIER__|$APP_IDENTIFIER|g" \
  -e "s|__APP_YEAR__|$APP_YEAR|g" \
  -e "s|__APP_LOGO__|$APP_LOGO|g" \
  -e "s|__ORG_NAME__|$ORG_NAME|g"

# A pasta core-cpp/include/__APP_NS__/ foi criada com o nome literal do
# placeholder (mesmo texto usado nos caminhos dos `cat > ...` acima) — só o
# CONTEÚDo dos arquivos passa pelo sed; o nome do diretório em si precisa
# ser renomeado à parte, agora que já sabemos o namespace real.
mv "$TARGET_DIR/core-cpp/include/__APP_NS__" "$TARGET_DIR/core-cpp/include/$APP_NS"

# git init opcional — só se ainda não houver um repositório na pasta destino
if command -v git >/dev/null 2>&1 && [[ ! -d "$TARGET_DIR/.git" ]]; then
  git -C "$TARGET_DIR" init -q
fi

cat <<SUMMARY

==================================================================
 '$APP_TITLE' gerado em: $TARGET_DIR
==================================================================

Próximos passos:

  1. cd $TARGET_DIR
  2. tools/fetch_third_party.sh          # baixa sqlite/nlohmann/doctest
  3. cargo build --workspace             # compila bridge + core-cli (Linux/macOS)
  4. cargo run -p core-cli               # roda a ponte com um banco de exemplo
  5. cargo run -p core-cli -- --dump-fixtures frontend/fixtures
  6. cd frontend && python3 -m http.server 8000
                                          # abre http://localhost:8000 num navegador

Build de produção (Windows, gera o instalador):
  cargo install tauri-cli --version "^2.0.0"
  cd src-tauri && cargo tauri build

Leia $TARGET_DIR/README.md para o mapa completo da arquitetura (front-end
e back-end camada por camada) e o passo a passo para trocar a entidade de
exemplo "Item" pelo domínio real do seu sistema.
SUMMARY
