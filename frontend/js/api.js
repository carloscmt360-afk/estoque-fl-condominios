// Ponte única com o backend: usa window.__TAURI__.core.invoke quando disponível
// (rodando de verdade dentro do app), ou um mock alimentado por fixtures quando
// aberto direto num navegador comum — é o que permite revisar/ajustar o layout
// inteiro sem precisar compilar Rust/C++/Tauri.
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
