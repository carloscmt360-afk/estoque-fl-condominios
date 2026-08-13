// Ponte única com o backend: usa window.__TAURI__.core.invoke quando disponível
// (rodando de verdade dentro do app), ou um mock alimentado por fixtures quando
// aberto direto num navegador comum — é o que permite revisar/ajustar o layout
// inteiro sem precisar compilar Rust/C++/Tauri.
const TAURI = typeof window !== 'undefined' && window.__TAURI__ && window.__TAURI__.core;

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
  approveRequest: async (id, note) =>
    JSON.parse(await call('approve_request', { input: { id, note, nowIso: nowIso() } })),
  rejectRequest: async (id, note) =>
    JSON.parse(await call('reject_request', { input: { id, note, nowIso: nowIso() } })),
  cancelRequest: async (id, note) =>
    JSON.parse(await call('cancel_request', { input: { id, note, nowIso: nowIso() } })),
  deliverRequest: async (id, movementIdPrefix) =>
    JSON.parse(await call('deliver_request', { input: { id, nowIso: nowIso(), movementIdPrefix } })),
  stockAvailability: async () => JSON.parse(await call('stock_availability')),

  listProducts: async () => JSON.parse(await call('list_products')),
  createProduct: async (product) => JSON.parse(await call('create_product', { product })),
  updateProduct: async (product) => JSON.parse(await call('update_product', { product })),
  deleteProduct: (id) => call('delete_product', { id }),

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
};
