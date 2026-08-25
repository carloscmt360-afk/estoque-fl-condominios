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

// Migration 4 — correção manual do custo médio.
//
// O ajuste de inventário (applyCorrecao) sempre corrigiu SALDO (contagem
// física); o custo médio nunca era tocado por ele — unit_price num ajuste é
// só uma valoração derivada, reescrita a cada recomputeProduct (ver o
// comentário lá). new_avg_cost é um campo à parte, opcional: quando > 0 num
// lançamento de ajuste, vira o novo custo médio do produto a partir daquele
// ponto da linha do tempo; quando 0 (ou NULL, em linhas antigas), o ajuste
// continua não mexendo no custo, como sempre foi. Mesmo motivo de ser
// ALTER TABLE e não recriação que monthly_limit acima: nunca tocar nas
// linhas já gravadas em produção.
constexpr const char* kSchemaV4 = R"SQL(
ALTER TABLE movements ADD COLUMN new_avg_cost REAL;
)SQL";

// Migration 5 — SKU (Stock Keeping Unit) automático, formato CMT######.
//
// `sku` é ALTER TABLE (mesmo motivo das anteriores: nunca recriar a tabela
// de produção). Logo em seguida, no MESMO passo de migração, os produtos já
// cadastrados recebem SKU em ordem de criação (created_at, id) — é a
// "varredura" que atribui SKU a quem ainda não tem, sem tocar em quem já
// tivesse (não existe esse caso ainda nesta versão, mas a cláusula WHERE
// sku IS NULL faz o mesmo código servir de reconciliação seguro se rodar
// de novo, ex.: depois de restaurar um backup antigo sem SKU — ver
// backfillMissingSkus em inventory_engine.cpp, chamada toda vez que o app
// abre).
//
// O índice único é a trava de verdade contra duplicidade (item 7 do
// pedido): mesmo que um bug em algum caminho de código tente gravar um SKU
// repetido, o SQLite recusa o INSERT/UPDATE.
//
// `sku_seq` guarda o MAIOR número já usado (nunca a quantidade de linhas —
// produtos excluídos não fazem o contador retroceder, então um SKU
// excluído nunca é reaproveitado; ver nextSku no .cpp).
constexpr const char* kSchemaV5 = R"SQL(
ALTER TABLE products ADD COLUMN sku TEXT;

WITH need_sku AS (
  SELECT id, ROW_NUMBER() OVER (ORDER BY created_at, id) AS rn
  FROM products
  WHERE sku IS NULL OR sku = ''
)
UPDATE products SET sku = 'CMT' || substr('000000' || need_sku.rn, -6, 6)
FROM need_sku
WHERE products.id = need_sku.id;

CREATE UNIQUE INDEX idx_products_sku ON products(sku);

INSERT OR REPLACE INTO app_settings (key, value)
SELECT 'sku_seq', CAST(COALESCE(MAX(CAST(substr(sku, 4) AS INTEGER)), 0) AS TEXT) FROM products;
)SQL";

// Migration 6 — foto do produto.
//
// Guarda só o CAMINHO relativo (nunca o arquivo em si — item 15 do pedido
// de fotos: "não armazenar BLOB/base64/binário na tabela"); quem decodifica
// e grava o .webp em disco é o módulo Rust (bridge/src/images.rs), nunca o
// C++. NULL/"" = produto sem foto — sempre foi opcional e continua sendo,
// nenhuma linha existente muda de comportamento com este ALTER TABLE.
constexpr const char* kSchemaV6 = R"SQL(
ALTER TABLE products ADD COLUMN image_path TEXT;
ALTER TABLE products ADD COLUMN thumbnail_path TEXT;
)SQL";

// Migration 7 — janela de requisições.
//
// Uma linha = um período em que o sistema aceita pedidos. Fora de qualquer
// janela, criar requisição é recusado pelo núcleo (ver
// requireOpenRequestWindow em request_engine.cpp) — a trava não depende do
// frontend lembrar de esconder o botão.
//
// `closed_at` é o fechamento ANTECIPADO à mão: a janela morre nesse instante
// em vez de esperar `closes_at`. Nunca se apaga a linha ao encerrar — o
// histórico de quando cada janela existiu é o que explica por que um pedido
// pôde ou não ser feito numa certa data.
//
// Não há coluna de "aberta": estar aberta é uma resposta que depende da hora
// da PERGUNTA (opens_at <= agora < closes_at, sem closed_at), nunca um estado
// gravado que precisaria de alguém para virar a chave no minuto exato.
constexpr const char* kSchemaV7 = R"SQL(
CREATE TABLE request_windows (
  id TEXT PRIMARY KEY,
  opens_at  TEXT NOT NULL,
  closes_at TEXT NOT NULL,
  obs TEXT,
  created_at TEXT NOT NULL,
  created_by_user_id TEXT,
  created_by_name TEXT NOT NULL,
  closed_at TEXT,
  closed_by_user_id TEXT,
  closed_by_name TEXT
);

CREATE INDEX idx_request_windows_periodo ON request_windows(opens_at, closes_at);
)SQL";

// Migration 8 — Gestão de Datas: condomínios, tipos de serviço e os vínculos
// entre eles (com o histórico de renovações).
//
// `servicos_condominio.condominio_id` tem ON DELETE CASCADE: excluir um
// condomínio leva junto seus vínculos e o histórico deles (o dado só faz
// sentido enquanto o condomínio existe). `tipo_servico_id` NÃO tem cascade —
// a aplicação (dates_engine::deleteTipoServico) recusa excluir um tipo de
// serviço em uso, para nunca apagar em silêncio o vínculo de um cliente só
// porque o catálogo de serviços mudou.
//
// `renovacoes` é o razão de renovações (nunca se sobrescreve uma linha, só se
// acrescenta), no mesmo espírito de `movements` para produtos.
// `prazo_dias_aplicado` denormaliza o prazo do tipo de serviço no momento da
// renovação: se o prazo padrão mudar depois, o histórico não muda debaixo do
// usuário.
constexpr const char* kSchemaV8 = R"SQL(
CREATE TABLE condominios (
  id TEXT PRIMARY KEY, nome TEXT NOT NULL, endereco TEXT, sindico TEXT,
  telefone TEXT, observacoes TEXT, created_at TEXT NOT NULL
);

CREATE TABLE tipos_servico (
  id TEXT PRIMARY KEY, nome TEXT NOT NULL,
  prazo_dias INTEGER NOT NULL DEFAULT 0,
  cor TEXT NOT NULL DEFAULT '#2e6ba6',
  created_at TEXT NOT NULL
);

CREATE TABLE servicos_condominio (
  id TEXT PRIMARY KEY,
  condominio_id TEXT NOT NULL REFERENCES condominios(id) ON DELETE CASCADE,
  tipo_servico_id TEXT NOT NULL REFERENCES tipos_servico(id),
  data_ultima_renovacao TEXT NOT NULL,
  empresa_contratada TEXT, observacoes TEXT, created_at TEXT NOT NULL
);

CREATE INDEX idx_servicos_condominio_condominio ON servicos_condominio(condominio_id);
CREATE INDEX idx_servicos_condominio_tipo       ON servicos_condominio(tipo_servico_id);

CREATE TABLE renovacoes (
  id TEXT PRIMARY KEY,
  servico_condominio_id TEXT NOT NULL REFERENCES servicos_condominio(id) ON DELETE CASCADE,
  data_renovacao TEXT NOT NULL,
  empresa_contratada TEXT,
  prazo_dias_aplicado INTEGER NOT NULL,
  observacoes TEXT,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_renovacoes_servico ON renovacoes(servico_condominio_id, data_renovacao);
)SQL";

// Migration 9 — Fornecedores e Prestadores de Serviços.
//
// `especialidades.setor` NÃO tem tabela nem FK: os quatro setores (Vendas,
// Contratos/Manutenções, Terceirizadas, Engenharia) são catálogo fixo no
// código (setorCatalog, companies_engine.cpp), como kFeatureCatalog das
// permissões. O CHECK abaixo é o que impede uma linha apontar para um setor
// que não existe — a lista vive num lugar só, e o banco apenas a respeita.
//
// O UNIQUE de especialidades é (setor, nome): "Dedetização" pode existir em
// Engenharia e em Terceirizadas, mas não duas vezes na mesma aba. NOCASE
// porque ninguém digita duas vezes com a mesma caixa.
//
// `empresas.cnpj_key` guarda só os alfanuméricos em maiúsculas e é ele que
// carrega o UNIQUE — mesma ideia de users.email_key: "12.345.678/0001-99" e
// "12345678000199" são o MESMO CNPJ, mas o texto como o usuário digitou é
// preservado em `cnpj` para exibir. Aceita CNPJ alfanumérico (o formato novo)
// porque a chave não presume dígitos.
//
// `empresa_especialidades` é a ligação N:N — uma empresa marca quantas
// especialidades precisar, de setores diferentes inclusive. ON DELETE CASCADE
// só no lado da EMPRESA: excluir a empresa leva as marcações dela, mas excluir
// uma especialidade em uso é recusado pela aplicação (deleteEspecialidade), e
// não silenciosamente propagado.
constexpr const char* kSchemaV9 = R"SQL(
CREATE TABLE especialidades (
  id TEXT PRIMARY KEY,
  setor TEXT NOT NULL CHECK (setor IN ('vendas','contratos_manutencoes','terceirizadas','engenharia')),
  nome TEXT NOT NULL,
  created_at TEXT NOT NULL
);

CREATE UNIQUE INDEX idx_especialidades_setor_nome ON especialidades(setor, nome COLLATE NOCASE);

CREATE TABLE empresas (
  id TEXT PRIMARY KEY,
  nome TEXT NOT NULL,
  cnpj TEXT,        -- como digitado, para exibir
  cnpj_key TEXT,    -- só alfanuméricos em maiúsculas; carrega o UNIQUE
  endereco TEXT, cep TEXT, cidade TEXT, estado TEXT,
  telefone TEXT,
  emails TEXT,      -- um ou mais, separados por vírgula
  observacoes TEXT,
  created_at TEXT NOT NULL
);

CREATE UNIQUE INDEX idx_empresas_cnpj_key ON empresas(cnpj_key) WHERE cnpj_key IS NOT NULL;
CREATE INDEX idx_empresas_nome ON empresas(nome);

CREATE TABLE empresa_especialidades (
  empresa_id TEXT NOT NULL REFERENCES empresas(id) ON DELETE CASCADE,
  especialidade_id TEXT NOT NULL REFERENCES especialidades(id),
  PRIMARY KEY (empresa_id, especialidade_id)
);

CREATE INDEX idx_empresa_esp_especialidade ON empresa_especialidades(especialidade_id);
)SQL";

// Migration 10 — empresa parceira (o Sim/Não da ficha).
//
// Fica como coluna DA EMPRESA, e não como uma tabela de parceiros: a mesma
// empresa cadastrada duas vezes (uma como fornecedora, outra como parceira)
// divergiria de telefone na primeira atualização, e é justamente o telefone
// que a tela de Gestão SOS > Parceiros existe para entregar.
//
// ALTER TABLE, pelo mesmo motivo de monthly_limit e new_avg_cost: nunca
// recriar uma tabela que já tem linhas em produção. DEFAULT 0 = as empresas
// já cadastradas continuam não-parceiras, que é a resposta correta para quem
// nunca respondeu a pergunta.
constexpr const char* kSchemaV10 = R"SQL(
ALTER TABLE empresas ADD COLUMN parceira INTEGER NOT NULL DEFAULT 0;

CREATE INDEX idx_empresas_parceira ON empresas(parceira);
)SQL";

// Migration 11 — Gestão SOS: Gerentes e Carteiras.
//
// `gerentes` é cadastro simples, mesmo formato de `condominios` (v8).
// `gerente_condominios` é a ligação N:N: cada linha diz "este condomínio faz
// parte da carteira deste gerente". Diferente de empresa_especialidades (onde
// só o lado da empresa tem cascade, porque excluir uma especialidade em uso é
// recusado pela aplicação), aqui os dois lados têm ON DELETE CASCADE — a
// carteira é só uma associação, não um catálogo protegido, e não há razão de
// negócio para recusar excluir gerente ou condomínio por causa dela.
constexpr const char* kSchemaV11 = R"SQL(
CREATE TABLE gerentes (
  id TEXT PRIMARY KEY, nome TEXT NOT NULL, telefone TEXT, email TEXT,
  observacoes TEXT, created_at TEXT NOT NULL
);

CREATE INDEX idx_gerentes_nome ON gerentes(nome);

CREATE TABLE gerente_condominios (
  gerente_id TEXT NOT NULL REFERENCES gerentes(id) ON DELETE CASCADE,
  condominio_id TEXT NOT NULL REFERENCES condominios(id) ON DELETE CASCADE,
  PRIMARY KEY (gerente_id, condominio_id)
);

CREATE INDEX idx_gerente_cond_condominio ON gerente_condominios(condominio_id);
)SQL";

// Migration 12 — Gestão SOS: Serviços, Fechamentos e Configurações.
//
// `sos_servicos.numero` é o que a planilha mostra como "ID": INTEGER PRIMARY
// KEY AUTOINCREMENT, e não um TEXT gerado no cliente como o resto do app,
// justamente porque o pedido é numeração sequencial que NUNCA se repete —
// nem depois de excluir uma linha. Um rowid comum reaproveitaria o maior
// número apagado; AUTOINCREMENT (a palavra, não só INTEGER PRIMARY KEY)
// garante que não. `id` continua existindo como a chave técnica TEXT — mesmo
// padrão do resto do app — usada em edição/exclusão pela ponte.
//
// `condominio_id/nome`, `gerente_id/nome` e `parceiro_id/nome` seguem o
// critério de requests.department_id: SEM FK, com o nome denormalizado.
// Excluir um condomínio, gerente ou parceiro anos depois não pode apagar (ou
// quebrar) uma comissão já lançada — o histórico financeiro precisa
// sobreviver ao cadastro que o originou.
//
// `comissao` (venda × porcentagem ÷ 100) NÃO é coluna: é sempre calculada na
// leitura (ver commissions_engine.cpp), mesmo critério do vencimento em
// dates_engine — nunca fica desatualizada em relação a venda/porcentagem.
//
// `fechamento_id` é o que trava a linha: uma vez fechada (ver sos_fechamentos
// abaixo), a aplicação recusa editar ou excluir o serviço. ON DELETE SET NULL
// é uma rede de segurança (reabrirFechamento já solta explicitamente antes de
// excluir o fechamento), não o mecanismo principal.
//
// `sos_fechamentos` denormaliza os totais NO MOMENTO do fechamento (mesmo
// espírito de renovacoes.prazo_dias_aplicado): como os serviços fechados
// ficam imutáveis, o total nunca diverge do que ele resume.
//
// `sos_config` é chave/valor simples — hoje só a porcentagem padrão de
// comissão, sem precisar de uma tabela por configuração.
constexpr const char* kSchemaV12 = R"SQL(
CREATE TABLE sos_fechamentos (
  id TEXT PRIMARY KEY,
  mes_referencia TEXT NOT NULL,
  quantidade_servicos INTEGER NOT NULL DEFAULT 0,
  total_venda REAL NOT NULL DEFAULT 0,
  total_comissao REAL NOT NULL DEFAULT 0,
  observacoes TEXT,
  fechado_em TEXT NOT NULL,
  created_at TEXT NOT NULL
);

CREATE UNIQUE INDEX idx_sos_fechamentos_mes ON sos_fechamentos(mes_referencia);

CREATE TABLE sos_servicos (
  numero INTEGER PRIMARY KEY AUTOINCREMENT,
  id TEXT NOT NULL UNIQUE,
  codigo TEXT,
  condominio_id TEXT, condominio_nome TEXT NOT NULL,
  gerente_id TEXT, gerente_nome TEXT,
  parceiro_id TEXT, parceiro_nome TEXT,
  venda REAL NOT NULL DEFAULT 0,
  porcentagem REAL NOT NULL DEFAULT 0,
  data_referencia TEXT NOT NULL,
  fechamento_id TEXT REFERENCES sos_fechamentos(id) ON DELETE SET NULL,
  observacoes TEXT,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_sos_servicos_referencia ON sos_servicos(data_referencia);
CREATE INDEX idx_sos_servicos_fechamento ON sos_servicos(fechamento_id);

CREATE TABLE sos_config (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
)SQL";

// Migration 13 — Cadastro de Condomínios ganha os campos do sistema de
// origem (CNPJ, Código — "Identificação" lá — endereço detalhado) e a
// Localização (região da cidade, catálogo fixo — ver localizacaoCatalog em
// dates_engine.hpp). ALTER TABLE, nunca recriar: já existem condomínios
// cadastrados em produção.
constexpr const char* kSchemaV13 = R"SQL(
ALTER TABLE condominios ADD COLUMN nome_fantasia TEXT;
ALTER TABLE condominios ADD COLUMN cnpj TEXT;
ALTER TABLE condominios ADD COLUMN codigo TEXT;
ALTER TABLE condominios ADD COLUMN complemento TEXT;
ALTER TABLE condominios ADD COLUMN bairro TEXT;
ALTER TABLE condominios ADD COLUMN cidade TEXT;
ALTER TABLE condominios ADD COLUMN estado TEXT;
ALTER TABLE condominios ADD COLUMN cep TEXT;
ALTER TABLE condominios ADD COLUMN localizacao TEXT
  CHECK (localizacao IS NULL OR localizacao IN ('centro','leste','oeste','norte','sul','outra_cidade'));
)SQL";

// Migration 14 — todo fechamento permanece no histórico para sempre.
//
// Antes, "Reabrir" apagava a linha de sos_fechamentos; agora só solta os
// serviços (fechamento_id volta a NULL) e marca `reaberto_em` — a linha em si
// nunca é excluída. Consequência direta: o mesmo mês pode ser fechado mais de
// uma vez ao longo do tempo (fecha, reabre para corrigir um lançamento,
// fecha de novo), e cada fechamento é uma entrada permanente e imutável — os
// totais dela são os totais QUE FORAM fechados naquele momento, não
// recalculados depois. Por isso o UNIQUE de mes_referencia sai (ele impedia
// exatamente esse "fechar de novo"); um índice comum substitui, só para
// manter a consulta por mês rápida.
constexpr const char* kSchemaV14 = R"SQL(
DROP INDEX idx_sos_fechamentos_mes;
CREATE INDEX idx_sos_fechamentos_mes ON sos_fechamentos(mes_referencia);
ALTER TABLE sos_fechamentos ADD COLUMN reaberto_em TEXT;
)SQL";

// Migration 15 — empresa (Fornecedores e Prestadores) ganha Nome fantasia,
// mesmo campo que condominios já tem (schema v13). ALTER TABLE, nunca
// recriar: já existem empresas cadastradas em produção.
constexpr const char* kSchemaV15 = "ALTER TABLE empresas ADD COLUMN nome_fantasia TEXT;";

// Migration 16 — módulo Compras: Aquisições FL, Orçamentos e Acompanhamento
// de pagamentos.
//
//   Aquisicao (compra geral, ligada a um fornecedor) ──< Pagamento (uma NF) ──< Parcela
//   Orcamento (proposta para um condomínio, ligada a um fornecedor)
//
// `fornecedor_id/nome` (nas duas primeiras) e `condominio_id/nome` (em
// orçamentos) seguem o MESMO critério de sos_servicos (v12): sem FK, nome
// denormalizado. Excluir um fornecedor ou condomínio anos depois não pode
// apagar (ou quebrar) uma aquisição ou orçamento já lançado — é histórico
// financeiro, precisa sobreviver ao cadastro que o originou (ver
// purchases_engine.cpp::resolverNomeFornecedor/resolverNomeCondominio).
//
// `compras_pagamentos.aquisicao_id` JÁ TEM FK (sem ON DELETE): diferente dos
// dois acima, aqui a ligação é obrigatória e recente (não histórico antigo
// sobrevivendo a uma exclusão) — a aplicação recusa excluir uma aquisição com
// pagamentos lançados (ver deleteAquisicao), então a FK nunca dispara na
// prática; ela só é uma segunda trava contra um bug futuro que tentasse.
//
// `compras_parcelas.pagamento_id` tem ON DELETE CASCADE: a parcela só existe
// dentro de um pagamento (a NF), então excluir o pagamento leva as parcelas
// dele — mesmo critério de gerente_condominios (v11), uma associação e não um
// catálogo protegido.
constexpr const char* kSchemaV16 = R"SQL(
CREATE TABLE compras_aquisicoes (
  id TEXT PRIMARY KEY,
  fornecedor_id TEXT NOT NULL,
  fornecedor_nome TEXT NOT NULL,
  descricao TEXT NOT NULL,
  nota_fiscal TEXT,
  valor REAL NOT NULL DEFAULT 0,
  data_compra TEXT NOT NULL,
  observacoes TEXT,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_compras_aquisicoes_data       ON compras_aquisicoes(data_compra);
CREATE INDEX idx_compras_aquisicoes_fornecedor ON compras_aquisicoes(fornecedor_id);

CREATE TABLE compras_orcamentos (
  id TEXT PRIMARY KEY,
  condominio_id TEXT NOT NULL,
  condominio_nome TEXT NOT NULL,
  fornecedor_id TEXT NOT NULL,
  fornecedor_nome TEXT NOT NULL,
  descricao TEXT NOT NULL,
  valor REAL NOT NULL DEFAULT 0,
  data_orcamento TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pendente' CHECK (status IN ('pendente','aprovado','recusado')),
  observacoes TEXT,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_compras_orcamentos_condominio ON compras_orcamentos(condominio_id);
CREATE INDEX idx_compras_orcamentos_fornecedor ON compras_orcamentos(fornecedor_id);
CREATE INDEX idx_compras_orcamentos_status     ON compras_orcamentos(status);

CREATE TABLE compras_pagamentos (
  id TEXT PRIMARY KEY,
  aquisicao_id TEXT NOT NULL REFERENCES compras_aquisicoes(id),
  nota_fiscal TEXT NOT NULL,
  valor_total REAL NOT NULL DEFAULT 0,
  data_emissao TEXT NOT NULL,
  observacoes TEXT,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_compras_pagamentos_aquisicao ON compras_pagamentos(aquisicao_id);

CREATE TABLE compras_parcelas (
  id TEXT PRIMARY KEY,
  pagamento_id TEXT NOT NULL REFERENCES compras_pagamentos(id) ON DELETE CASCADE,
  numero INTEGER NOT NULL,
  valor REAL NOT NULL DEFAULT 0,
  vencimento TEXT NOT NULL,
  pago INTEGER NOT NULL DEFAULT 0,
  data_pagamento TEXT,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_compras_parcelas_pagamento   ON compras_parcelas(pagamento_id);
CREATE INDEX idx_compras_parcelas_vencimento  ON compras_parcelas(vencimento);
)SQL";

// Migration 17 — Gerentes ganha ID sequencial e Chave PIX; novo cadastro
// Suprimentos (equipe de campo).
//
// `gerentes.numero` é o mesmo problema do SKU de produtos (migration 5):
// retrofitar um "ID visível" numa tabela que já tem linhas em produção. Não
// dá para virar INTEGER PRIMARY KEY AUTOINCREMENT numa coluna nova de uma
// tabela existente — a solução é a mesma de lá, ALTER TABLE + backfill por
// ROW_NUMBER (ordem de criação) + índice único + um contador em
// app_settings (nextGerenteNumero em managers_engine.cpp), nunca a contagem
// de linhas (gerente excluído não pode fazer um número ser reciclado).
//
// `sos_suprimentos` é tabela NOVA — sem histórico de produção para preservar
// — então usa AUTOINCREMENT de verdade, mesmo critério de sos_servicos.numero
// (migration 12): `id` TEXT é a chave técnica, `numero` é o "ID" que a tela
// mostra. `categoria` é catálogo fixo (Gestor, Assistente, Auxiliar,
// Vistoriador predial) — mesmo critério de localizacao_condominio: é a
// própria estrutura da equipe, não algo que o usuário cadastra.
constexpr const char* kSchemaV17 = R"SQL(
ALTER TABLE gerentes ADD COLUMN numero INTEGER;
ALTER TABLE gerentes ADD COLUMN chave_pix TEXT;

WITH need_numero AS (
  SELECT id, ROW_NUMBER() OVER (ORDER BY created_at, id) AS rn
  FROM gerentes
  WHERE numero IS NULL
)
UPDATE gerentes SET numero = need_numero.rn
FROM need_numero
WHERE gerentes.id = need_numero.id;

CREATE UNIQUE INDEX idx_gerentes_numero ON gerentes(numero);

INSERT OR REPLACE INTO app_settings (key, value)
SELECT 'gerentes_numero_seq', CAST(COALESCE(MAX(numero), 0) AS TEXT) FROM gerentes;

CREATE TABLE sos_suprimentos (
  numero INTEGER PRIMARY KEY AUTOINCREMENT,
  id TEXT NOT NULL UNIQUE,
  nome TEXT NOT NULL,
  categoria TEXT NOT NULL CHECK (categoria IN ('gestor','assistente','auxiliar','vistoriador_predial')),
  telefone TEXT,
  email TEXT,
  chave_pix TEXT,
  observacoes TEXT,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_sos_suprimentos_nome ON sos_suprimentos(nome);
)SQL";

// Migration 18 — Gestão SOS: Delta Síndicos.
//
// Segunda planilha de comissões do módulo, irmã de sos_servicos (v12) e com
// a mesma arquitetura: `numero` INTEGER PRIMARY KEY AUTOINCREMENT é o "ID"
// que a tela mostra (nunca reciclado, nem depois de excluir uma linha), `id`
// TEXT é a chave técnica usada pela ponte, e `comissao` NÃO é coluna — é
// sempre venda × porcentagem ÷ 100, calculada na leitura.
//
// A diferença é a quem a comissão se refere: aqui é o SÍNDICO do condomínio,
// não gerente/parceiro. `sindico` é TEXTO gravado na própria linha (e não uma
// FK para condominios.sindico) pelo mesmo motivo de condominio_nome ao lado:
// é um lançamento financeiro, e o síndico do condomínio muda com a eleição —
// o que já foi lançado precisa continuar mostrando quem era o síndico NAQUELE
// mês, não quem é hoje. A tela pré-preenche a partir do cadastro do
// condomínio, mas o valor gravado é dali em diante independente.
//
// Sem fechamento aqui (diferente de sos_servicos): esta planilha não é
// travada por mês — se isso for preciso depois, entra como migração própria.
constexpr const char* kSchemaV18 = R"SQL(
CREATE TABLE sos_delta_sindicos (
  numero INTEGER PRIMARY KEY AUTOINCREMENT,
  id TEXT NOT NULL UNIQUE,
  condominio_id TEXT, condominio_nome TEXT NOT NULL,
  sindico TEXT,
  venda REAL NOT NULL DEFAULT 0,
  porcentagem REAL NOT NULL DEFAULT 0,
  data_referencia TEXT NOT NULL,
  observacoes TEXT,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_sos_delta_sindicos_referencia ON sos_delta_sindicos(data_referencia);
CREATE INDEX idx_sos_delta_sindicos_condominio ON sos_delta_sindicos(condominio_id);
)SQL";

// Migration 19 — Delta Síndicos ganha o GERENTE, e nasce o dashboard de
// fechamento.
//
// O gerente do lançamento é o que liga as duas metades do fechamento: o
// DESCONTO de cada gerente no dashboard é a soma do que ele paga em Delta
// Síndicos naquele mês (era o SOMASES da planilha). Sem FK e com o nome
// denormalizado, mesmo critério de condominio_nome ao lado.
//
// `sos_dashboards_fechamento` guarda o RETRATO do fechamento — o dashboard
// inteiro serializado, do jeito que foi apresentado, e não uma consulta a
// recalcular depois. É o mesmo princípio dos totais de sos_fechamentos
// (v12): os números que foram fechados naquele mês não podem mudar quando
// um serviço antigo for corrigido anos depois. Por isso a linha nunca é
// alterada nem excluída pela aplicação; gerar de novo o mesmo mês cria uma
// entrada NOVA, e o histórico guarda as duas.
//
// `dados_json` é um retrato inteiro em JSON (e não 40 colunas): o dashboard
// tem quatro painéis de formatos diferentes, cada um com número variável de
// linhas, e nada aqui é consultado por campo — é sempre lido inteiro para
// exibir/imprimir. Mesmo critério de app_settings para preferências.
constexpr const char* kSchemaV19 = R"SQL(
ALTER TABLE sos_delta_sindicos ADD COLUMN gerente_id TEXT;
ALTER TABLE sos_delta_sindicos ADD COLUMN gerente_nome TEXT;

CREATE TABLE sos_dashboards_fechamento (
  id TEXT PRIMARY KEY,
  mes_referencia TEXT NOT NULL,
  dados_json TEXT NOT NULL,
  observacoes TEXT,
  gerado_em TEXT NOT NULL,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_sos_dashboards_mes ON sos_dashboards_fechamento(mes_referencia);
)SQL";

// Migration 20 — Anexo da Nota Fiscal em cada Aquisição.
//
// Mesmo critério de products.image_path/thumbnail_path (migration 5): o
// C++ só grava o CAMINHO relativo que a camada Rust devolve depois de
// processar o arquivo (ver bridge/src/attachments.rs) — nunca decodifica
// nem gera bytes de imagem/PDF aqui. `anexo_tipo` distingue os dois formatos
// aceitos ('imagem' | 'pdf') porque a tela precisa saber se mostra um
// <img> ou um link/ícone de PDF, sem precisar inspecionar a extensão do
// arquivo salvo.
constexpr const char* kSchemaV20 = R"SQL(
ALTER TABLE compras_aquisicoes ADD COLUMN anexo_path TEXT;
ALTER TABLE compras_aquisicoes ADD COLUMN anexo_tipo TEXT;
)SQL";

// Migration 21 — Orçamentos vira um fluxo de cotação de verdade, com envio de
// e-mail (ver bridge/src/mailer.rs).
//
// O CRUD antigo (`compras_orcamentos`: 1 orçamento = 1 fornecedor = 1 valor)
// não tem onde guardar "várias empresas cotando o mesmo pedido" — é
// substituído por duas tabelas novas, não uma ALTER: uma ORDEM (a
// "ordem de serviço" pedida pelo usuário, presa a um condomínio) tem N
// PROPOSTAS (uma por empresa solicitada). Sem dado real em produção nessa
// tela ainda (módulo Compras é recente), a tabela antiga é descartada — não
// vale carregar dois modelos concorrentes.
//
// `compras_ordens_orcamento.status` só guarda os estados que são resultado
// de uma AÇÃO explícita (pendente/solicitado/enviado_cliente/aprovado) —
// "declinado" (25 dias sem resposta) nunca é gravado aqui: é sempre
// CALCULADO na leitura a partir de `data_solicitacao`/`reaberto_em` contra a
// data de hoje (mesmo critério de calcularStatus em dates_engine.cpp, ver
// ordemOrcamentoStatusEfetivo em purchases_engine.cpp) — assim "reativar"
// depois do declínio automático é só gravar `reaberto_em`, sem precisar
// reverter um status que a aplicação tivesse escrito.
//
// `compras_propostas_orcamento.valor` é NULL enquanto a proposta não voltou
// (a empresa foi só solicitada) — 0 seria ambíguo com "orçamento gratuito".
// `anexo_path`/`anexo_tipo` seguem o mesmo critério de
// compras_aquisicoes.anexo_* (migration 20): a Rust processa o arquivo (PDF
// como veio, imagem redimensionada pra A4), o C++ só guarda o caminho.
// `recomendada` é sempre uma só por ordem — a aplicação desmarca as outras
// ao marcar uma nova (ver marcarPropostaRecomendada), não precisa de UNIQUE
// parcial aqui porque SQLite não suporta índice condicional combinado com
// essa checagem de forma simples nesta versão do schema.
//
// `condominios.email` é o destinatário padrão do e-mail de "Enviar para o
// cliente" — não existia campo de e-mail no condomínio até aqui (só
// telefone/síndico).
constexpr const char* kSchemaV21 = R"SQL(
DROP INDEX IF EXISTS idx_compras_orcamentos_condominio;
DROP INDEX IF EXISTS idx_compras_orcamentos_fornecedor;
DROP INDEX IF EXISTS idx_compras_orcamentos_status;
DROP TABLE IF EXISTS compras_orcamentos;

ALTER TABLE condominios ADD COLUMN email TEXT;

CREATE TABLE compras_ordens_orcamento (
  numero INTEGER PRIMARY KEY AUTOINCREMENT,
  id TEXT NOT NULL UNIQUE,
  condominio_id TEXT, condominio_nome TEXT NOT NULL,
  descricao TEXT NOT NULL,
  observacoes TEXT,
  status TEXT NOT NULL DEFAULT 'pendente'
    CHECK (status IN ('pendente','solicitado','enviado_cliente','aprovado')),
  proposta_recomendada_id TEXT,
  proposta_aprovada_id TEXT,
  data_solicitacao TEXT,
  data_envio_cliente TEXT,
  data_aprovacao TEXT,
  reaberto_em TEXT,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_ordens_orcamento_condominio ON compras_ordens_orcamento(condominio_id);
CREATE INDEX idx_ordens_orcamento_status     ON compras_ordens_orcamento(status);

CREATE TABLE compras_propostas_orcamento (
  id TEXT PRIMARY KEY,
  ordem_id TEXT NOT NULL REFERENCES compras_ordens_orcamento(id) ON DELETE CASCADE,
  empresa_id TEXT NOT NULL, empresa_nome TEXT NOT NULL,
  valor REAL,
  anexo_path TEXT, anexo_tipo TEXT,
  email_enviado_em TEXT,
  recomendada INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);

CREATE INDEX idx_propostas_orcamento_ordem ON compras_propostas_orcamento(ordem_id);
)SQL";

// `condominios.ativo` — Sim/Não respondido no cadastro. Todo condomínio já
// existente vira "ativo" (default 1) na migração; nada muda de comportamento
// pra quem nunca tocar no campo.
constexpr const char* kSchemaV22 = R"SQL(
ALTER TABLE condominios ADD COLUMN ativo INTEGER NOT NULL DEFAULT 1;
)SQL";

// `numero` (nº do endereço) faltava em condomínios E empresas — só existia
// "Endereço" (rua) e "Complemento", sem campo pro número da casa/prédio.
// `complemento`/`bairro` também faltavam em empresas (condomínios já tinham).
constexpr const char* kSchemaV23 = R"SQL(
ALTER TABLE condominios ADD COLUMN numero TEXT;
ALTER TABLE empresas ADD COLUMN numero TEXT;
ALTER TABLE empresas ADD COLUMN complemento TEXT;
ALTER TABLE empresas ADD COLUMN bairro TEXT;
)SQL";

// Rastro de quem corrigiu os itens de uma requisição em nome do solicitante
// (quantidade errada, item esquecido) — sem isto a edição ficaria invisível
// na linha do tempo do pedido.
constexpr const char* kSchemaV24 = R"SQL(
ALTER TABLE requests ADD COLUMN edited_at TEXT;
ALTER TABLE requests ADD COLUMN edited_by_user_id TEXT;
ALTER TABLE requests ADD COLUMN edited_by_name TEXT;
)SQL";

// Pago/Data de pagamento em Serviços: um serviço só entra no fechamento e na
// distribuição de comissão (Dashboard de Fechamento) depois de marcado como
// pago — venda lançada sem o cliente ter pago ainda não gera comissão pra
// ninguém. `pago` nasce 0 (padrão de fábrica: nada é considerado pago até
// alguém marcar) para não inflar retroativamente o histórico já lançado.
constexpr const char* kSchemaV25 = R"SQL(
ALTER TABLE sos_servicos ADD COLUMN pago INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sos_servicos ADD COLUMN data_pagamento TEXT;
)SQL";

// Marca quais condomínios são atendidos pela Delta como síndica — é essa
// marcação que faz Delta Síndicos deixar de ser lançamento manual e virar
// puxado automaticamente de Serviços (ver listDeltaSindicos em
// commissions_engine.cpp).
constexpr const char* kSchemaV26 = R"SQL(
ALTER TABLE condominios ADD COLUMN delta_sindica INTEGER NOT NULL DEFAULT 0;
)SQL";

// `sos_pagamentos` guarda a lista de pagamentos (Gerentes/Suprimentos/Delta)
// programada e depois fechada para um mês — mesmo princípio de retrato em
// JSON de sos_dashboards_fechamento (v19), montada a partir do Dashboard de
// Fechamento já salvo daquele mês.
//
// Diferente dos outros fechamentos do sistema (que nunca são alterados —
// gerar de novo cria uma entrada nova), este é editável DEPOIS de fechado:
// é comum precisar corrigir uma Chave PIX ou um valor autorizado depois do
// fato, e criar uma segunda entrada pro mesmo mês só confundiria "qual
// pagamento vale". Por isso um mês só tem UM registro (índice único
// abaixo) e `atualizarPagamento` regrava esse mesmo registro.
constexpr const char* kSchemaV27 = R"SQL(
CREATE TABLE sos_pagamentos (
  id TEXT PRIMARY KEY,
  mes_referencia TEXT NOT NULL,
  dados_json TEXT NOT NULL,
  observacoes TEXT,
  fechado INTEGER NOT NULL DEFAULT 0,
  gerado_em TEXT NOT NULL,
  fechado_em TEXT,
  created_at TEXT NOT NULL
);

CREATE UNIQUE INDEX idx_sos_pagamentos_mes ON sos_pagamentos(mes_referencia);
)SQL";

// Mapa de Orçamentos (impresso pra o cliente) ficava só com Empresa/Valor —
// pouca informação pra decidir. Estes três campos são só da PROPOSTA (não do
// cadastro da empresa, que já tem CNPJ próprio — ver companies_engine.hpp):
// cada cotação pode vir com escopo/forma de pagamento/validade diferentes,
// então não fazem sentido "resolvidos na hora" de um cadastro fixo. Pedidos
// junto do anexo (ver setPropostaDetalhes em purchases_engine.cpp) porque é
// o momento em que o usuário está com a proposta em mãos.
constexpr const char* kSchemaV28 = R"SQL(
ALTER TABLE compras_propostas_orcamento ADD COLUMN escopo TEXT;
ALTER TABLE compras_propostas_orcamento ADD COLUMN forma_pagamento TEXT;
ALTER TABLE compras_propostas_orcamento ADD COLUMN validade TEXT;
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
  int version = 0;
  {
    // Escopo próprio: o Statement precisa finalizar (destruir) ANTES de
    // qualquer DDL rodar. Um DROP INDEX com esta consulta ainda viva na mesma
    // conexão falha com "database table is locked" mesmo sendo uma PRAGMA
    // sem relação nenhuma com a tabela alterada — CREATE/ALTER TABLE toleram
    // isso, mas DROP INDEX não.
    Statement st(db_, "PRAGMA user_version;");
    if (st.step()) version = static_cast<int>(st.columnDouble(0));
  }

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
  if (version < 4) {
    execute(kSchemaV4);
    execute("PRAGMA user_version = 4;");
  }
  if (version < 5) {
    execute(kSchemaV5);
    execute("PRAGMA user_version = 5;");
  }
  if (version < 6) {
    execute(kSchemaV6);
    execute("PRAGMA user_version = 6;");
  }
  if (version < 7) {
    execute(kSchemaV7);
    execute("PRAGMA user_version = 7;");
  }
  if (version < 8) {
    execute(kSchemaV8);
    execute("PRAGMA user_version = 8;");
  }
  if (version < 9) {
    execute(kSchemaV9);
    execute("PRAGMA user_version = 9;");
  }
  if (version < 10) {
    execute(kSchemaV10);
    execute("PRAGMA user_version = 10;");
  }
  if (version < 11) {
    execute(kSchemaV11);
    execute("PRAGMA user_version = 11;");
  }
  if (version < 12) {
    execute(kSchemaV12);
    execute("PRAGMA user_version = 12;");
  }
  if (version < 13) {
    execute(kSchemaV13);
    execute("PRAGMA user_version = 13;");
  }
  if (version < 14) {
    execute(kSchemaV14);
    execute("PRAGMA user_version = 14;");
  }
  if (version < 15) {
    execute(kSchemaV15);
    execute("PRAGMA user_version = 15;");
  }
  if (version < 16) {
    execute(kSchemaV16);
    execute("PRAGMA user_version = 16;");
  }
  if (version < 17) {
    execute(kSchemaV17);
    execute("PRAGMA user_version = 17;");
  }
  if (version < 18) {
    execute(kSchemaV18);
    execute("PRAGMA user_version = 18;");
  }
  if (version < 19) {
    execute(kSchemaV19);
    execute("PRAGMA user_version = 19;");
  }
  if (version < 20) {
    execute(kSchemaV20);
    execute("PRAGMA user_version = 20;");
  }
  if (version < 21) {
    execute(kSchemaV21);
    execute("PRAGMA user_version = 21;");
  }
  if (version < 22) {
    execute(kSchemaV22);
    execute("PRAGMA user_version = 22;");
  }
  if (version < 23) {
    execute(kSchemaV23);
    execute("PRAGMA user_version = 23;");
  }
  if (version < 24) {
    execute(kSchemaV24);
    execute("PRAGMA user_version = 24;");
  }
  if (version < 25) {
    execute(kSchemaV25);
    execute("PRAGMA user_version = 25;");
  }
  if (version < 26) {
    execute(kSchemaV26);
    execute("PRAGMA user_version = 26;");
  }
  if (version < 27) {
    execute(kSchemaV27);
    execute("PRAGMA user_version = 27;");
  }
  if (version < 28) {
    execute(kSchemaV28);
    execute("PRAGMA user_version = 28;");
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
