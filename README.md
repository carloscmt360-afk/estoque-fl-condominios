# Estoque FL Condomínios

App desktop de controle de estoque — refatorado do app web original (HTML/JS
+ localStorage) para C++ (motor de negócio + SQLite) com uma casca Rust/Tauri
e um frontend redesenhado.

## Arquitetura

```
core-cpp/    motor de negócio (custo médio ponderado, relatório mensal) + SQLite — puro C++, sem Rust/Tauri
bridge/      ponte cxx entre core-cpp e Rust (usada por src-tauri e core-cli)
core-cli/    binário de diagnóstico: valida a ponte inteira sem depender de Tauri/WebKitGTK
src-tauri/   casca Tauri (única parte que depende de WebView2/WebKitGTK)
frontend/    HTML/CSS/JS estático (sidebar, gráficos SVG, sem framework/bundler)
```

Detalhes de design (por quê C++, por que SQLite sem WAL, como funciona o
armazenamento portátil) estão comentados nos próprios arquivos-fonte,
principalmente `core-cpp/include/estoque/*.hpp`.

## Armazenamento portátil

Os dados ficam sempre em `dados/estoque.db`, **ao lado do executável** —
nunca em AppData. Copiar a pasta do programa (incluindo `dados/`) para
outro PC ou pendrive preserva tudo.

## Build

**Linux (dev — só valida a lógica, não builda a GUI):**
```bash
cargo build --workspace          # bridge + core-cli
cargo run -p core-cli            # roda a ponte com um backup real (opcional)
```

**Windows (produção — gera o instalador):**
```bash
cargo install tauri-cli --version "^2.0.0"
cd src-tauri
cargo tauri build
```
Requer Rust + Visual Studio Build Tools (C++ workload) instalados.

**CI:** `.github/workflows/build-windows.yml` builda automaticamente a cada
push em `main` (runner `windows-latest`), publicando o instalador como
artefato.

## Testes

```bash
# núcleo C++ (doctest, sem cmake — g++ direto)
cd core-cpp
g++ -std=c++17 -Iinclude -Ithird_party/sqlite -Ithird_party \
  src/*.cpp tests/test_report_engine.cpp -lpthread -ldl -o /tmp/t && /tmp/t
```

## Frontend — desenvolvimento sem compilar Rust/C++/Tauri

```bash
cargo run -p core-cli -- --dump-fixtures frontend/fixtures   # gera dados de teste reais (não versionados)
cd frontend && python3 -m http.server 8000                  # abre com um mock alimentado por essas fixtures
```
