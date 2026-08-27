# Gestão de Suprimentos - FL

App desktop de gestão de suprimentos: estoque e requisições, compras e
orçamentos, cadastro de condomínios com prazos de serviço, e a Gestão SOS
(comissões, fechamento e pagamentos). Nasceu como controle de estoque e hoje o
estoque é só um dos módulos — refatorado do app web original (HTML/JS +
localStorage) para C++ (motor de negócio + SQLite) com uma casca Rust/Tauri e
um frontend redesenhado.

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

## Login, usuários e permissões

O app abre numa **tela de login** e nada é carregado antes de o backend
confirmar quem entrou. O superadministrador de fábrica é semeado na primeira
abertura do banco:

```
carlos.matos@flcondominios.com.br / Mudar@2025
```

Troque essa senha no primeiro acesso ("Trocar senha", no rodapé da barra
lateral). A semeadura é idempotente e **não** redefine a senha de um
superadmin que já existe — ela só volta a agir se o banco ficar sem nenhum
superadministrador ativo.

Senha nunca é gravada: guarda-se PBKDF2-HMAC-SHA256 com salt de 128 bits por
usuário e o número de rodadas usado (`core-cpp/include/estoque/crypto.hpp`,
com vetores oficiais em `tests/test_crypto.cpp`).

### O modelo de permissão

```
grupo de permissão --atribuído a--> DEPARTAMENTO --atrelado a--> usuário
```

A permissão **nunca** é dada a uma pessoa: é dada ao setor, e quem está no
setor herda. Mover alguém de departamento já muda tudo que ele enxerga, sem
existir uma segunda lista para alguém esquecer de revisar. O papel
`superadmin` ignora a matriz inteira (acesso total, gestão de usuários e
validação de requisições).

Cada par (grupo, função) guarda os quatro bits do CRUD. As funções são
Relatório Mensal, Retrospecto, Produtos, Linha do Tempo, Departamentos,
Importar e exportar e Requisições. **O que cada bit libera em cada função**
está definido uma única vez, em `kFeatureCatalog`
(`core-cpp/src/auth_engine.cpp`), e é esse mesmo texto que a tela de
Permissões exibe em cada linha da matriz — a UI não reescreve a regra.

Autorização é conferida no **C++**, em toda operação (`Api::require*` em
`core-cpp/src/api.cpp`). Esconder botão na tela é conveniência, não controle
de acesso: o frontend só evita oferecer o que seria negado depois do clique.

`Api::loginAsService` dá acesso total sem senha e existe para o `core-cli` e
os testes, que operam direto no arquivo do banco (onde autenticação não
protege nada — quem tem o arquivo já tem tudo). Ela **não** é registrada como
comando Tauri em `src-tauri/src/main.rs`: essa lista de comandos é a fronteira
de confiança do app, e o frontend não alcança o que não está nela.

## Requisições de material

```
pendente ──aprovar──> aprovado ──confirmar entrega──> entregue
   │                     │
   ├──rejeitar──> rejeitado
   └──cancelar──> cancelado <──cancelar──┘
```

Enquanto o pedido está `pendente` ou `aprovado`, os itens ficam
**reservados**: o saldo do produto continua intacto (relatório, retrospecto e
custo médio não enxergam nada), mas o **disponível** para novas requisições
desconta a reserva — duas pessoas não conseguem pedir o mesmo último galão. A
tela de Produtos ganhou as colunas *Reservado* e *Disponível* por causa disso.

Só a **confirmação de entrega** debita o estoque, e o faz gerando uma saída de
verdade por item (`applySaida`), com o solicitante e o departamento do pedido
— não um `UPDATE` escondido em `products`. É o que mantém requisição, Linha do
Tempo, custo médio e relatórios contando a mesma história; o id da saída fica
gravado no item da requisição. A entrega inteira é uma transação só: se um
item não tiver saldo, nenhum é baixado.

Por padrão quem valida é o superadministrador. Conceder `Requisições →
editar` a um grupo delega essa validação ao setor correspondente.

O histórico é a própria tela: cada pedido mostra autor, data e hora da
solicitação, de quem aprovou/rejeitou e de quem confirmou a entrega. Um
usuário comum vê os pedidos **do seu departamento**; quem valida vê todos —
e é o backend quem filtra, não a tela (`Api::listRequestsJson`).

### Janela de requisições (prazo de pedidos)

O sistema só aceita requisição **dentro de uma janela aberta**: um período com
data/hora de abertura e de fechamento, cadastrado por quem valida requisições.
Passado o fechamento, o app **tranca sozinho** e recusa qualquer pedido novo
até alguém abrir a próxima janela.

Um banco **sem nenhuma janela cadastrada está fechado** — é o estado de
fábrica, e a mensagem de recusa diz o que fazer.

"Aberta" nunca é uma coluna gravada, e sim uma comparação contra o instante da
pergunta (`opens_at <= agora < closes_at`, sem fechamento antecipado): a mesma
linha do banco responde "aberta" às 12h e "fechada" às 18h, sem precisar de
alguém — ou de um agendador — para virar a chave no minuto exato.

A trava mora em `requireOpenRequestWindow` (`request_engine.cpp`) e é chamada
por `Api::createRequest` **antes** de qualquer validação de item. O "agora"
conferido é o do **relógio do sistema** (`time_utils::systemNowIso()`), nunca o
`createdAt` do payload — deixar o próprio pedido dizer que horas são anularia o
prazo. Esconder o botão no frontend é só conveniência. (Como todo app desktop,
o prazo vale contra o relógio da máquina: quem tem administrador do Windows
pode mudá-lo. A trava é operacional, não uma barreira contra fraude.)

Duas janelas não podem valer ao mesmo tempo (senão "a janela aberta agora"
teria duas respostas). Uma janela pode ser **encerrada antes do prazo**
(`closed_at`), o que fecha os pedidos na hora e devolve o período restante para
uma janela nova ocupar. Janela que já começou não pode ser apagada — só
encerrada —, porque o histórico dela é o que explica por que os pedidos daquele
período foram aceitos.

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

## Fornecedores e Prestadores de Serviços

Três telas, na ordem de uso — o **Catálogo** primeiro porque é o que se abre no
dia a dia; as outras duas são a manutenção do que alimenta ele:

```
SETOR (catálogo fixo de 4) ──> Especialidade (o "nicho", cadastrável)
                                     │
                                     └──< Empresa (N especialidades)
```

Os **setores** (Vendas, Contratos/Manutenções, Terceirizadas, Engenharia) são
fixos no código (`setorCatalog`, `companies_engine.cpp`), pelo mesmo critério do
`kFeatureCatalog` das permissões: quando a lista é a própria estrutura do
negócio, ela mora num lugar só e o banco apenas a respeita (via `CHECK`).

As **especialidades** são o que se cadastra dentro de cada setor, na tela de
**Setorização**, e são exatamente as que a tela de **Cadastro** oferece para
marcar numa empresa — quantas precisar, de setores diferentes inclusive. Nome
repetido é recusado **dentro do mesmo setor**, ignorando caixa, acento e espaço
sobrando (`folded()`): "Impermeabilizacao" e "Impermeabilização" como dois
nichos separados espalhariam as empresas em duas linhas, que é justamente o que
o Catálogo existe para evitar. O mesmo nome em setores **diferentes** é
permitido (Dedetização pode ser de Engenharia e de Terceirizadas).

O **Catálogo de empresas** agrupa por segmento, não por empresa: é a tela de
quem já tem uma demanda na mão. Cada empresa aparece com telefone e e-mail à
vista, e com as *outras* especialidades dela como contexto.

`empresas.cnpj_key` guarda só os alfanuméricos em maiúsculas e carrega o
`UNIQUE` (mesma ideia de `users.email_key`): "12.345.678/0001-99" e
"12345678000199" são o MESMO CNPJ, mas o texto digitado é preservado para
exibir. **Não** se confere dígito verificador — o CNPJ alfanumérico convive com
o numérico, e recusar um cadastro legítimo é pior que aceitar um dígito trocado.
O campo é opcional.

Excluir uma **empresa** solta as marcações dela (`ON DELETE CASCADE`) mas
preserva o catálogo; excluir uma **especialidade em uso** é recusado, dizendo em
quantas empresas ela está.

### Gestão SOS > Parceiros

A ficha da empresa tem a pergunta **"É empresa parceira?"** (Sim/Não). O Sim é o
que faz a empresa aparecer em **Gestão SOS › Parceiros**, e a marcação vive na
própria empresa — não há cadastro separado de parceiro, porque duas fichas da
mesma empresa divergiriam de telefone na primeira atualização, e é o telefone
que essa tela existe para entregar. Por isso ele vem em destaque ali: num
chamado emergencial, o passo seguinte a achar é ligar.

### Gestão SOS > Gerentes, Carteiras, Serviços e Comissões

```
Gerente ──< carteira (N condomínios) >── Condominio
                                              │
Servico (condomínio, gerente?, parceiro?, venda, porcentagem, mês) ──> comissão [calculada]
   │
   └──< fechamentoId >── Fechamento (totais denormalizados no momento do fechar)
```

**Gerentes** é cadastro simples; **Carteiras** mostra um card por gerente — o
clique abre TODOS os condomínios cadastrados, marcados por clique (mesmo
padrão de chip de Especialidades), e os marcados formam a carteira dele
(`gerente_condominios`, N:N).

**Serviços** é a planilha de vendas/comissões: cada linha liga um condomínio
(obrigatório) a um gerente e um parceiro (ambos opcionais, e o parceiro
precisa ser uma empresa marcada como parceira), com venda, porcentagem e um
mês de referência (`"YYYY-MM"`). O "ID" da planilha é `numero`
(`INTEGER PRIMARY KEY AUTOINCREMENT`) — nunca se repete, mesmo depois de
excluir uma linha, diferente de um rowid comum que reaproveitaria o maior
número apagado. `id` (TEXT) continua existindo como a chave técnica, mesmo
padrão do resto do app. Condomínio/gerente/parceiro são referenciados por id
E por nome denormalizado, **sem FK** (mesmo critério de
`requests.department_id`): é um lançamento financeiro, e excluir o cadastro de
origem anos depois não pode apagar nem quebrar a comissão já lançada. A
**comissão nunca é gravada** — é sempre `venda × porcentagem ÷ 100`, calculada
na leitura, mesmo critério do vencimento em Gestão de Prazos.

**Fechamento** trava (contra edição/exclusão) todos os serviços em aberto de
um mês e grava os totais no **Histórico de fechamentos**, denormalizados no
momento do fechar (como `renovacoes.prazo_dias_aplicado`) — como os serviços
fechados ficam imutáveis, o total nunca diverge do que ele resume. **Reabrir**
desfaz um fechamento feito por engano: solta os serviços de volta para edição
e remove o registro. **Configurações** hoje só guarda a porcentagem padrão de
comissão (`sos_config`, chave/valor), que pré-preenche o campo ao lançar um
novo serviço.

## Cadastro de Condomínios e Gestão de Prazos

Os condomínios administrados têm cadastro próprio (**Cadastro de
Condomínios**), e é essa lista que alimenta o seletor da **Gestão de Prazos**
— um serviço só é vinculado a um condomínio que exista ali. Além dos campos
operacionais (síndico, telefone, observações), o cadastro tem CNPJ, Código
(a "Identificação" de sistemas de origem), Nome fantasia e o endereço
detalhado (CEP, endereço, complemento, bairro, cidade, UF) — nenhum desses é
obrigatório. **Localização** é a região da cidade onde o condomínio fica:
resposta única entre um catálogo fixo (Centro, Leste, Oeste, Norte, Sul, Outra
cidade — `localizacaoCatalog`, `dates_engine.cpp`), mesmo critério do
`setorCatalog` de Fornecedores: é estrutura do negócio, não um cadastro à
parte, então mora no código e o banco só a respeita (via `CHECK`).

A Gestão de Prazos acompanha o vencimento dos serviços contratados por cada
condomínio:

```
Condominio ─┐                    ┌─ TipoServico (prazoDias, cor)
            ├─> ServicoCondominio┤
            │   (dataUltimaRenovacao)
            │        │
            │        └──> Renovacao (histórico: uma linha por renovação)
```

O **vencimento e a situação nunca são gravados**: saem calculados a cada
leitura de `dataUltimaRenovacao + TipoServico.prazoDias` contra o "hoje" que o
chamador passa — mesma disciplina de `nowIso` do resto do núcleo, que é o que
mantém o cálculo determinístico e testável. A situação é *em dia* (mais de 30
dias), *atenção* (0 a 30 dias) ou *vencido* (prazo estourado), e o texto da
pílula diz o número de dias: a cor reforça, nunca é a única informação.
`TipoServico.prazoDias` continua em dias no núcleo (é o que sustenta a régua de
30 dias acima); só a tela de Tipo de Serviço fala em **meses** com o usuário,
convertendo 1 mês = 30 dias na entrada/saída (`frontend/js/views/prazos.js`).

Cada **Renovar** grava uma linha nova em `renovacoes` e atualiza o vínculo, na
mesma transação — o razão nunca é sobrescrito, mesmo espírito de `movements`
para produtos. O `prazo_dias_aplicado` fica denormalizado em cada renovação:
mudar depois o prazo padrão de um tipo de serviço **não** reescreve o
histórico já gravado.

Excluir um condomínio leva junto seus vínculos e o histórico deles (`ON DELETE
CASCADE`). Já um **tipo de serviço em uso não pode ser excluído**: apagar em
silêncio o vínculo de um cliente porque o catálogo mudou seria perda de dado,
não limpeza.

As permissões **Condomínios** e **Gestão de Prazos** entram na mesma matriz
grupo → departamento → usuário das demais telas, e as quatro tabelas entram no
backup/restore.

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
for t in test_inventory_engine test_report_engine test_retrospect_engine test_api \
         test_time_utils test_portable_paths test_crypto test_auth_engine test_request_engine \
         test_dates_engine test_companies_engine test_managers_engine test_commissions_engine \
         test_suprimentos_engine test_purchases_engine; do
  g++ -std=c++17 -Iinclude -Ithird_party/sqlite -Ithird_party \
    src/*.cpp tests/$t.cpp /tmp/sqlite3.o -lpthread -ldl -o /tmp/$t && /tmp/$t
done
```

`test_portable_paths` tem um caso que cria um diretório somente-leitura e
espera falha de escrita: rodando os testes **como root**, esse caso falha
porque o root ignora a permissão do diretório. Rode como usuário comum.

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
