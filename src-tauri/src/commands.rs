// Comandos Tauri: cada um só faz lock() no estado e delega para a ponte
// cxx — nenhuma regra de negócio mora aqui (isso é papel do core-cpp).
use crate::dto::{DepartmentInput, ProductInput};
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
pub fn list_products(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_products_json())
}

#[tauri::command]
pub fn create_product(state: State<AppState>, product: ProductInput) -> Result<String, String> {
    with_session(&state, |s| s.create_product(product.into()))
}

#[tauri::command]
pub fn update_product(state: State<AppState>, product: ProductInput) -> Result<String, String> {
    with_session(&state, |s| s.update_product(product.into()))
}

#[tauri::command]
pub fn delete_product(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_product(&id))
}

#[tauri::command]
pub fn list_departments(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_departments_json())
}

#[tauri::command]
pub fn create_department(state: State<AppState>, department: DepartmentInput) -> Result<String, String> {
    with_session(&state, |s| s.create_department(department.into()))
}

#[tauri::command]
pub fn update_department(state: State<AppState>, department: DepartmentInput) -> Result<String, String> {
    with_session(&state, |s| s.update_department(department.into()))
}

#[tauri::command]
pub fn delete_department(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_department(&id))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EntradaInput {
    pub movement_id: String,
    pub product_id: String,
    pub qty: f64,
    pub unit_price: f64,
    #[serde(default)]
    pub supplier: String,
    #[serde(default)]
    pub nf: String,
    pub date: String,
    #[serde(default)]
    pub obs: String,
    pub created_at: String,
}

#[tauri::command]
pub fn apply_entrada(state: State<AppState>, input: EntradaInput) -> Result<String, String> {
    with_session(&state, |s| {
        s.apply_entrada(
            &input.movement_id,
            &input.product_id,
            input.qty,
            input.unit_price,
            &input.supplier,
            &input.nf,
            &input.date,
            &input.obs,
            &input.created_at,
        )
    })
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SaidaInput {
    pub movement_id: String,
    pub product_id: String,
    pub qty: f64,
    pub department_id: String,
    pub date: String,
    #[serde(default)]
    pub obs: String,
    #[serde(default)]
    pub requester: String,
    pub created_at: String,
}

#[tauri::command]
pub fn apply_saida(state: State<AppState>, input: SaidaInput) -> Result<String, String> {
    with_session(&state, |s| {
        s.apply_saida(
            &input.movement_id,
            &input.product_id,
            input.qty,
            &input.department_id,
            &input.date,
            &input.obs,
            &input.requester,
            &input.created_at,
        )
    })
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CorrecaoInput {
    pub movement_id: String,
    pub product_id: String,
    pub qty_real: f64,
    pub motivo: String,
    pub date: String,
    pub created_at: String,
}

#[tauri::command]
pub fn apply_correcao(state: State<AppState>, input: CorrecaoInput) -> Result<String, String> {
    with_session(&state, |s| {
        s.apply_correcao(&input.movement_id, &input.product_id, input.qty_real, &input.motivo, &input.date, &input.created_at)
    })
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReportInput {
    pub year: i32,
    pub month0: i32,
    #[serde(default)]
    pub dept_filter: String,
    pub window_months: i32,
    pub now_iso: String,
}

#[tauri::command]
pub fn compute_report(state: State<AppState>, input: ReportInput) -> Result<String, String> {
    with_session(&state, |s| {
        s.compute_report_json(input.year, input.month0, &input.dept_filter, input.window_months, &input.now_iso)
    })
}

#[tauri::command]
pub fn backup(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.backup_json())
}

#[tauri::command]
pub fn restore_backup(state: State<AppState>, payload: String) -> Result<(), String> {
    with_session(&state, |s| s.restore_from_json(&payload))
}
