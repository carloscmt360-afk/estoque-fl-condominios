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

## Retrospecto (custo mensal por departamento)

A aba **Retrospecto** reproduz dentro do app a aba `RESTROSPECTO` da planilha
do usuário: matriz departamento × mês, total do ano, comparativo com o ano
anterior no mesmo número de meses e o teto de gastos sugerido.

Os dados vêm de **duas fontes distintas**, mescladas **por mês inteiro**
(nunca metade de um mês de cada):

1. `dept_cost_history` — histórico importado da planilha (aba `CUSTO POR
   DEPTO` onde há detalhe; o quadro da `RESTROSPECTO` para anos que só existem
   ali, como 2025).
2. as próprias movimentações de saída registradas no app.

Onde há histórico importado, ele vence; nos demais meses entra o razão. Isso é
necessário porque as duas fontes **divergem de propósito**: a planilha
valoriza cada retirada pelo preço digitado na linha, o razão pelo custo médio
ponderado vigente na data. Ver `core-cpp/include/estoque/retrospect_engine.hpp`.

Para importar/reimportar o histórico de uma planilha nova:

```bash
python3 tools/extrair_retrospecto.py "ESTOQUE FL copia DD-MM.xlsx" /tmp/retro.json
cp dados/estoque.db "dados/estoque.db.bak-$(date -u +%Y%m%dT%H%M%SZ)"   # sempre
cargo run -p core-cli -- --db dados/estoque.db --import-history /tmp/retro.json
cargo run -p core-cli -- --db dados/estoque.db --retrospect 2026        # confere
```

A reimportação **substitui o ano inteiro** presente no arquivo (um
departamento removido da planilha não sobrevive como fantasma). O extrator
imprime a diferença contra o total do quadro da planilha: ela costuma ser
maior que zero porque os `SUMIFS` do quadro perdem lançamentos em silêncio
(departamento fora da lista fixa de 13 linhas, nome com espaço sobrando) — o
script recupera esses lançamentos e diz quanto.

## Limite mensal por departamento

Cada departamento tem um `monthly_limit` (0 = sem limite). O Relatório Mensal
compara o gasto do mês de referência com esse teto e mostra um aviso **que não
se fecha** a partir de 90% e enquanto estiver estourado — o alerta é calculado
ignorando o filtro de departamento da tela, senão filtrar por um setor
esconderia o estouro dos outros.

O botão "Usar como limite mensal dos departamentos", no Retrospecto, grava o
teto sugerido de cada setor como o limite cadastrado.

## Impressão

"Imprimir estoque" (posição de estoque), "Imprimir analítico" (relatório
completo) e "Imprimir" no Retrospecto montam um documento próprio em
`#printArea` e chamam `window.print()`; `frontend/css/print.css` esconde o app
e mostra só esse bloco. Não se imprime a tela: a sidebar, os filtros e os
gráficos SVG não têm função no papel, e `window.open` é bloqueado dentro do
Tauri.

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

# 1) o amálgama do SQLite precisa ser compilado como C, NUNCA como C++:
#    ele usa idiomas válidos em C e ilegais em C++ (o mesmo motivo explicado
#    em bridge/build.rs). Compile uma vez e reaproveite o .o.
gcc -c -O1 -w -DSQLITE_THREADSAFE=1 third_party/sqlite/sqlite3.c -o /tmp/sqlite3.o

# 2) um binário por arquivo de teste (cada um define seu próprio main)
for t in test_inventory_engine test_report_engine test_retrospect_engine test_api test_time_utils test_portable_paths; do
  g++ -std=c++17 -Iinclude -Ithird_party/sqlite -Ithird_party \
    src/*.cpp tests/$t.cpp /tmp/sqlite3.o -lpthread -ldl -o /tmp/$t && /tmp/$t
done
```

`test_api.cpp` importa o backup real do usuário a partir de um caminho
absoluto e confere os números já validados contra a planilha (R$ 56.664,37 em
estoque, R$ 10.790,36 de consumo em junho/2026, 67 pedidos) — se esse arquivo
não existir na máquina, só esse teste falha.

## Frontend — desenvolvimento sem compilar Rust/C++/Tauri

```bash
cargo run -p core-cli -- --dump-fixtures frontend/fixtures   # gera dados de teste reais (não versionados)
# a fixture do Retrospecto precisa de um banco COM histórico importado:
cargo run -p core-cli -- --db <estoque.db real> --retrospect 2026 --dump-fixtures frontend/fixtures
cd frontend && python3 -m http.server 8000                  # abre com um mock alimentado por essas fixtures
```

O navegador cacheia módulos ES agressivamente: depois de editar um arquivo em
`js/`, recarregue com **Ctrl+Shift+R**. Um `import` quebrado dá tela branca
(o erro acontece antes de `boot()` rodar) — confira o console.
