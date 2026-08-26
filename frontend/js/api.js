// Ponte única com o backend: usa window.__TAURI__.core.invoke quando disponível
// (rodando de verdade dentro do app), ou um mock alimentado por fixtures quando
// aberto direto num navegador comum — é o que permite revisar/ajustar o layout
// inteiro sem precisar compilar Rust/C++/Tauri.
const TAURI = typeof window !== 'undefined' && window.__TAURI__ && window.__TAURI__.core;

// Só o app instalado tem bandeja/janela para encerrar; no navegador (modo de
// revisão de layout) o botão "Encerrar programa" nem aparece.
export const rodandoNoApp = Boolean(TAURI);

let mockHandler = null;
export function useMock(handler) {
  mockHandler = handler;
}

/* Erros de autorização vêm do C++ com um prefixo que diz o que fazer com eles
   (ver api.hpp): "[auth]" = não há sessão válida, a tela tem que voltar para o
   login; "[forbidden]" = há sessão, mas sem direito àquilo. Concentrar isso
   aqui evita que cada view tenha que se lembrar de testar. */
export function isAuthError(e) {
  return String(e && e.message ? e.message : e).includes('[auth]');
}
/* Mensagem sem o prefixo técnico — é o que se mostra ao usuário. */
export function errorText(e) {
  return String(e && e.message ? e.message : e).replace(/\[(auth|forbidden)\]\s*/g, '');
}

async function call(cmd, args) {
  try {
    if (TAURI) return await window.__TAURI__.core.invoke(cmd, args);
    if (!mockHandler) throw new Error('Nenhum backend disponível (nem Tauri, nem mock configurado).');
    return await mockHandler(cmd, args);
  } catch (e) {
    // Sessão perdida (app reaberto, usuário desativado, backup restaurado por
    // cima): avisa o app inteiro de uma vez em vez de cada view descobrir
    // sozinha. `current_session` fica de fora — ela responde "null" nesse caso,
    // que é a resposta esperada, não um erro.
    if (cmd !== 'current_session' && isAuthError(e)) {
      document.dispatchEvent(new CustomEvent('estoque:sessao-perdida'));
    }
    throw e;
  }
}

const nowIso = () => new Date().toISOString();

export const api = {
  appStatus: () => call('app_status'),
  retryInit: () => call('retry_init'),

  // ---- sessão ----
  login: async (email, password) =>
    JSON.parse(await call('login', { input: { email, password, nowIso: nowIso() } })),
  logout: () => call('logout'),
  currentSession: async () => JSON.parse(await call('current_session')),
  changeOwnPassword: (currentPassword, newPassword) =>
    call('change_own_password', { input: { currentPassword, newPassword } }),

  // ---- usuários ----
  listUsers: async () => JSON.parse(await call('list_users')),
  createUser: async (user) => JSON.parse(await call('create_user', { user })),
  updateUser: async (user) => JSON.parse(await call('update_user', { user })),
  deleteUser: (id) => call('delete_user', { id }),
  resetUserPassword: (id, newPassword) => call('reset_user_password', { input: { id, newPassword } }),

  // ---- permissões ----
  listPermissions: async () => JSON.parse(await call('list_permissions')),
  createPermissionGroup: async (group) =>
    JSON.parse(await call('create_permission_group', { payload: JSON.stringify(group) })),
  updatePermissionGroup: async (group) =>
    JSON.parse(await call('update_permission_group', { payload: JSON.stringify(group) })),
  deletePermissionGroup: (id) => call('delete_permission_group', { id }),
  setDepartmentPermissionGroup: (departmentId, groupId) =>
    call('set_department_permission_group', { input: { departmentId, groupId } }),

  // ---- requisições ----
  listRequests: async () => JSON.parse(await call('list_requests')),
  createRequest: async (request) =>
    JSON.parse(await call('create_request', { payload: JSON.stringify(request) })),
  // Substitui os itens de uma requisição ainda aberta — corrige quantidade
  // errada ou acrescenta algo que o solicitante esqueceu de registrar.
  updateRequestItems: async (id, items) =>
    JSON.parse(await call('update_request_items', { input: { id, payload: JSON.stringify({ items }), nowIso: nowIso() } })),
  approveRequest: async (id, note) =>
    JSON.parse(await call('approve_request', { input: { id, note, nowIso: nowIso() } })),
  rejectRequest: async (id, note) =>
    JSON.parse(await call('reject_request', { input: { id, note, nowIso: nowIso() } })),
  cancelRequest: async (id, note) =>
    JSON.parse(await call('cancel_request', { input: { id, note, nowIso: nowIso() } })),
  deliverRequest: async (id, movementIdPrefix) =>
    JSON.parse(await call('deliver_request', { input: { id, nowIso: nowIso(), movementIdPrefix } })),

  // Janela de requisições. Nenhum manda data: quem confere o prazo é o
  // relógio do sistema, no C++ (ver requireOpenRequestWindow).
  requestWindowStatus: async () => JSON.parse(await call('request_window_status')),
  listRequestWindows: async () => JSON.parse(await call('list_request_windows')),
  createRequestWindow: async (window) =>
    JSON.parse(await call('create_request_window', { payload: JSON.stringify(window) })),
  closeRequestWindowNow: async (id) => JSON.parse(await call('close_request_window_now', { id })),
  deleteRequestWindow: (id) => call('delete_request_window', { id }),
  stockAvailability: async () => JSON.parse(await call('stock_availability')),

  listProducts: async () => JSON.parse(await call('list_products')),
  createProduct: async (product) => JSON.parse(await call('create_product', { product })),
  updateProduct: async (product) => JSON.parse(await call('update_product', { product })),
  // sku vazio ("") é válido: produto sem SKU ainda (nunca deveria acontecer
  // em produção, mas o comando trata graciosamente — só pula a limpeza de
  // arquivo se não houver pasta pra apagar).
  deleteProduct: (id, sku) => call('delete_product', { id, sku: sku || '' }),
  uploadProductImage: async (input) => JSON.parse(await call('upload_product_image', { input })),
  deleteProductImage: async (input) => JSON.parse(await call('delete_product_image', { input })),
  readProductImage: (relativePath) => call('read_product_image', { relativePath }),

  // ---- logo da FL (barra superior dos relatórios) ----
  uploadAppLogo: (fileBase64) => call('upload_app_logo', { fileBase64 }),
  deleteAppLogo: () => call('delete_app_logo'),
  getAppLogo: () => call('get_app_logo'),

  listDepartments: async () => JSON.parse(await call('list_departments')),
  createDepartment: async (department) => JSON.parse(await call('create_department', { department })),
  updateDepartment: async (department) => JSON.parse(await call('update_department', { department })),
  deleteDepartment: (id) => call('delete_department', { id }),

  applyEntrada: async (input) => JSON.parse(await call('apply_entrada', { input })),
  applySaida: async (input) => JSON.parse(await call('apply_saida', { input })),
  applyCorrecao: async (input) => JSON.parse(await call('apply_correcao', { input })),

  listMovements: async () => JSON.parse(await call('list_movements')),
  updateMovement: async (patch) => JSON.parse(await call('update_movement', { patch })),
  deleteMovement: (id) => call('delete_movement', { id }),

  computeReport: async (input) => JSON.parse(await call('compute_report', { input })),
  computeRetrospect: async (input) => JSON.parse(await call('compute_retrospect', { input })),
  saveBudgetParams: (input) => call('save_budget_params', { input }),
  importDeptCostHistory: (payload) => call('import_dept_cost_history', { payload }),

  backup: async () => JSON.parse(await call('backup')),
  restoreBackup: (payload) => call('restore_backup', { payload }),

  // ---- gestão de prazos ----
  // nowIso: a cada listagem, aviso prévio vencido (ver iniciarAvisoPrevio) é
  // aplicado de vez pelo backend antes de montar a lista.
  listCondominios: async () => JSON.parse(await call('list_condominios', { input: { nowIso: nowIso() } })),
  createCondominio: async (condominio) => JSON.parse(await call('create_condominio', { condominio })),
  updateCondominio: async (condominio) => JSON.parse(await call('update_condominio', { condominio })),
  deleteCondominio: (id) => call('delete_condominio', { id }),
  iniciarAvisoPrevio: async (condominioId, ate, novoCodigo) =>
    JSON.parse(await call('iniciar_aviso_previo', { input: { condominioId, ate, novoCodigo } })),
  cancelarAvisoPrevio: async (condominioId) =>
    JSON.parse(await call('cancelar_aviso_previo', { condominioId })),

  listTiposServico: async () => JSON.parse(await call('list_tipos_servico')),
  createTipoServico: async (tipo) => JSON.parse(await call('create_tipo_servico', { tipo })),
  updateTipoServico: async (tipo) => JSON.parse(await call('update_tipo_servico', { tipo })),
  deleteTipoServico: (id) => call('delete_tipo_servico', { id }),

  listServicosCondominio: async () =>
    JSON.parse(await call('list_servicos_condominio', { input: { nowIso: nowIso() } })),
  createServicoCondominio: async (vinculo) =>
    JSON.parse(await call('create_servico_condominio', { payload: JSON.stringify({ ...vinculo, nowIso: nowIso() }) })),
  updateServicoCondominio: async (vinculo) =>
    JSON.parse(await call('update_servico_condominio', { payload: JSON.stringify({ ...vinculo, nowIso: nowIso() }) })),
  deleteServicoCondominio: (id) => call('delete_servico_condominio', { id }),

  renovarServico: async (renovacao) =>
    JSON.parse(await call('renovar_servico', { payload: JSON.stringify({ ...renovacao, nowIso: nowIso() }) })),
  listRenovacoes: async (servicoCondominioFilter) =>
    JSON.parse(await call('list_renovacoes', { input: { servicoCondominioFilter: servicoCondominioFilter || '' } })),

  // ---- fornecedores e prestadores de serviços ----
  listSetorizacao: async () => JSON.parse(await call('list_setorizacao')),
  createEspecialidade: async (esp) =>
    JSON.parse(await call('create_especialidade', { payload: JSON.stringify(esp) })),
  updateEspecialidade: async (esp) =>
    JSON.parse(await call('update_especialidade', { payload: JSON.stringify(esp) })),
  deleteEspecialidade: (id) => call('delete_especialidade', { id }),

  listEmpresas: async () => JSON.parse(await call('list_empresas')),
  // Só as parceiras — a filtragem é do SQL, não da tela (ver api.hpp).
  listParceiros: async () => JSON.parse(await call('list_parceiros')),
  createEmpresa: async (empresa) =>
    JSON.parse(await call('create_empresa', { payload: JSON.stringify(empresa) })),
  updateEmpresa: async (empresa) =>
    JSON.parse(await call('update_empresa', { payload: JSON.stringify(empresa) })),
  deleteEmpresa: (id) => call('delete_empresa', { id }),

  // ---- gestão sos: gerentes e carteiras ----
  listGerentes: async () => JSON.parse(await call('list_gerentes')),
  createGerente: async (gerente) =>
    JSON.parse(await call('create_gerente', { payload: JSON.stringify(gerente) })),
  updateGerente: async (gerente) =>
    JSON.parse(await call('update_gerente', { payload: JSON.stringify(gerente) })),
  deleteGerente: (id) => call('delete_gerente', { id }),

  // ---- gestão sos: serviços e fechamentos ----
  listServicos: async () => JSON.parse(await call('list_servicos')),
  createServico: async (servico) =>
    JSON.parse(await call('create_servico', { payload: JSON.stringify(servico) })),
  updateServico: async (servico) =>
    JSON.parse(await call('update_servico', { payload: JSON.stringify(servico) })),
  deleteServico: (id) => call('delete_servico', { id }),

  listFechamentos: async () => JSON.parse(await call('list_fechamentos')),
  fecharMes: async (fechamento) =>
    JSON.parse(await call('fechar_mes', { payload: JSON.stringify(fechamento) })),
  reabrirFechamento: (id) => call('reabrir_fechamento', { id }),

  getSosConfig: async () => JSON.parse(await call('get_sos_config')),
  setSosConfig: (config) => call('set_sos_config', { payload: JSON.stringify(config) }),

  // ---- gestão sos: delta síndicos ----
  // Puxado automaticamente de Serviços (condomínios marcados com
  // deltaSindica=true) — não existe mais create/update/delete aqui.
  listDeltaSindicos: async () => JSON.parse(await call('list_delta_sindicos')),

  // ---- gestão sos: dashboard de fechamento ----
  // montarDashboard NUNCA grava — é a prévia calculada a cada mudança no
  // formulário de Fechamento. Só salvarDashboard grava, e nunca substitui
  // (gerar de novo o mesmo mês acrescenta ao histórico).
  montarDashboard: async (entrada) =>
    JSON.parse(await call('montar_dashboard', { payload: JSON.stringify(entrada) })),
  salvarDashboard: async (snap) =>
    JSON.parse(await call('salvar_dashboard', { payload: JSON.stringify(snap) })),
  listDashboards: async () => JSON.parse(await call('list_dashboards')),

  // ---- gestão sos: pagamentos (Programar Pagamento / Histórico) ----
  // Se o mês já tem pagamento salvo, montarPagamentoSos devolve ELE (modo
  // edição) em vez de uma proposta nova — nunca gera duas propostas
  // divergentes pro mesmo mês. salvarPagamentoSos fecha OU corrige o mesmo
  // registro (nunca duplica).
  montarPagamentoSos: async (mesReferencia) =>
    JSON.parse(await call('montar_pagamento_sos', { payload: JSON.stringify({ mesReferencia }) })),
  salvarPagamentoSos: async (pagamento) =>
    JSON.parse(await call('salvar_pagamento_sos', { payload: JSON.stringify(pagamento) })),
  listPagamentosSos: async () => JSON.parse(await call('list_pagamentos_sos')),

  // ---- gestão sos: suprimentos ----
  listSuprimentos: async () => JSON.parse(await call('list_suprimentos')),
  createSuprimento: async (suprimento) =>
    JSON.parse(await call('create_suprimento', { payload: JSON.stringify(suprimento) })),
  updateSuprimento: async (suprimento) =>
    JSON.parse(await call('update_suprimento', { payload: JSON.stringify(suprimento) })),
  deleteSuprimento: (id) => call('delete_suprimento', { id }),

  // ---- compras: aquisições, orçamentos e pagamentos ----
  listAquisicoes: async () => JSON.parse(await call('list_aquisicoes')),
  createAquisicao: async (aquisicao) =>
    JSON.parse(await call('create_aquisicao', { payload: JSON.stringify(aquisicao) })),
  updateAquisicao: async (aquisicao) =>
    JSON.parse(await call('update_aquisicao', { payload: JSON.stringify(aquisicao) })),
  deleteAquisicao: (id) => call('delete_aquisicao', { id }),
  uploadAquisicaoAttachment: async (input) => JSON.parse(await call('upload_aquisicao_attachment', { input })),
  deleteAquisicaoAttachment: async (aquisicaoId) =>
    JSON.parse(await call('delete_aquisicao_attachment', { aquisicaoId })),
  readAquisicaoAttachment: (relativePath) => call('read_aquisicao_attachment', { relativePath }),

  // Orçamentos: fluxo de cotação (ordem 1:N propostas) — ver
  // views/orcamentos.js. As três ações que disparam e-mail
  // (solicitarOrcamentoParaEmpresas/enviarOrcamentoParaCliente/
  // aprovarPropostaOrcamento) devolvem a ordem com uma chave "emailErros" a
  // mais quando algum envio falhou (a ação em si sempre é aplicada, mesmo
  // que o e-mail falhe — ver enviar_emails_compostos em
  // src-tauri/src/commands.rs).
  listOrdensOrcamento: async () => JSON.parse(await call('list_ordens_orcamento', { nowIso: nowIso() })),
  createOrdemOrcamento: async (ordem) =>
    JSON.parse(await call('create_ordem_orcamento', { payload: JSON.stringify(ordem) })),
  updateOrdemOrcamentoInfo: async (dados) =>
    JSON.parse(await call('update_ordem_orcamento_info', { payload: JSON.stringify(dados) })),
  deleteOrdemOrcamento: (id) => call('delete_ordem_orcamento', { id }),
  solicitarOrcamentoParaEmpresas: async (ordemId, empresas) =>
    JSON.parse(await call('solicitar_orcamento_para_empresas', {
      input: { payload: JSON.stringify({ ordemId, empresas }), nowIso: nowIso() },
    })),
  reenviarSolicitacaoProposta: async (propostaId) =>
    JSON.parse(await call('reenviar_solicitacao_proposta', { input: { propostaId, nowIso: nowIso() } })),
  setPropostaValor: async (propostaId, valor) =>
    JSON.parse(await call('set_proposta_valor', { input: { propostaId, valor } })),
  uploadPropostaAttachment: async (input) => JSON.parse(await call('upload_proposta_attachment', { input })),
  setPropostaDetalhes: async (propostaId, escopo, formaPagamento, validade) =>
    JSON.parse(await call('set_proposta_detalhes', { input: { propostaId, escopo, formaPagamento, validade } })),
  deletePropostaAttachment: async (ordemId, propostaId) =>
    JSON.parse(await call('delete_proposta_attachment', { input: { ordemId, propostaId } })),
  readPropostaAttachment: (relativePath) => call('read_proposta_attachment', { relativePath }),
  marcarPropostaRecomendada: async (ordemId, propostaId) =>
    JSON.parse(await call('marcar_proposta_recomendada', { input: { ordemId, propostaId } })),
  desmarcarPropostaRecomendada: async (ordemId) =>
    JSON.parse(await call('desmarcar_proposta_recomendada', { ordemId })),
  enviarOrcamentoParaCliente: async (ordemId, destinatarioEmail, mensagemExtra, propostaIds) =>
    JSON.parse(await call('enviar_orcamento_para_cliente', {
      input: { payload: JSON.stringify({ ordemId, destinatarioEmail, mensagemExtra, propostaIds }), nowIso: nowIso() },
    })),
  aprovarPropostaOrcamento: async (ordemId, propostaId) =>
    JSON.parse(await call('aprovar_proposta_orcamento', { input: { ordemId, propostaId, nowIso: nowIso() } })),
  reativarOrdemOrcamento: async (ordemId) =>
    JSON.parse(await call('reativar_ordem_orcamento', { input: { ordemId, nowIso: nowIso() } })),

  getEmailConfig: async () => JSON.parse(await call('get_email_config')),
  setEmailConfig: (config) => call('set_email_config', { payload: JSON.stringify(config) }),
  // Testa a config JÁ SALVA (chame setEmailConfig antes) mandando um e-mail
  // de verdade pro endereço informado — existe pra não precisar montar uma
  // ordem de orçamento inteira só pra saber se host/porta/TLS estão certos.
  sendTestEmail: (to) => call('send_test_email', { to }),
  // Alternativa ao envio automático por SMTP: abre o Outlook (ou o cliente
  // de e-mail padrão do Windows) já com destinatário/assunto/corpo prontos,
  // pra revisar e clicar Enviar à mão — não suporta anexo automático
  // (limitação do protocolo mailto:, não deste app).
  abrirEmailOutlook: (to, subject, body) => call('abrir_email_outlook', { to, subject, body }),

  listPagamentos: async () => JSON.parse(await call('list_pagamentos')),
  createPagamento: async (pagamento) =>
    JSON.parse(await call('create_pagamento', { payload: JSON.stringify(pagamento) })),
  updatePagamento: async (pagamento) =>
    JSON.parse(await call('update_pagamento', { payload: JSON.stringify(pagamento) })),
  deletePagamento: (id) => call('delete_pagamento', { id }),
  marcarParcela: async (dados) =>
    JSON.parse(await call('marcar_parcela', { payload: JSON.stringify(dados) })),

  // Fecha o programa de vez (o X da janela só o envia para a bandeja).
  encerrarApp: () => call('encerrar_app'),
};
