// Comandos Tauri: cada um só faz lock() no estado e delega para a ponte
// cxx — nenhuma regra de negócio mora aqui (isso é papel do core-cpp).
use crate::dto::{DepartmentInput, MovementPatchInput, ProductInput, UserInput};
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

// ------------------------------------------------------------------ sessão
//
// Note o que NÃO está aqui nem em main.rs: `login_as_service`, a sessão de
// acesso total sem senha que existe na ponte para o core-cli. Comando não
// registrado é comando que o frontend não alcança — é essa lista que define
// a fronteira de confiança do app.

/// Os comandos com mais de um argumento recebem uma struct única, e não
/// parâmetros soltos — mesmo padrão de EntradaInput/SaidaInput abaixo. Assim o
/// nome de cada campo é fixado pelo `rename_all = "camelCase"` do serde, em vez
/// de depender da conversão automática de nomes de argumento do Tauri.
#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LoginInput {
    pub email: String,
    pub password: String,
    pub now_iso: String,
}

#[tauri::command]
pub fn login(state: State<AppState>, input: LoginInput) -> Result<String, String> {
    with_session(&state, |s| s.login(&input.email, &input.password, &input.now_iso))
}

#[tauri::command]
pub fn logout(state: State<AppState>) -> Result<(), String> {
    with_session(&state, |s| s.logout())
}

#[tauri::command]
pub fn current_session(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.current_session_json())
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ChangePasswordInput {
    pub current_password: String,
    pub new_password: String,
}

#[tauri::command]
pub fn change_own_password(state: State<AppState>, input: ChangePasswordInput) -> Result<(), String> {
    with_session(&state, |s| s.change_own_password(&input.current_password, &input.new_password))
}

// ---------------------------------------------------------------- usuários

#[tauri::command]
pub fn list_users(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_users_json())
}

#[tauri::command]
pub fn create_user(state: State<AppState>, user: UserInput) -> Result<String, String> {
    with_session(&state, |s| s.create_user(user.into()))
}

#[tauri::command]
pub fn update_user(state: State<AppState>, user: UserInput) -> Result<String, String> {
    with_session(&state, |s| s.update_user(user.into()))
}

#[tauri::command]
pub fn delete_user(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_user(&id))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ResetPasswordInput {
    pub id: String,
    pub new_password: String,
}

#[tauri::command]
pub fn reset_user_password(state: State<AppState>, input: ResetPasswordInput) -> Result<(), String> {
    with_session(&state, |s| s.reset_user_password(&input.id, &input.new_password))
}

// -------------------------------------------------------------- permissões

#[tauri::command]
pub fn list_permissions(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_permissions_json())
}

#[tauri::command]
pub fn create_permission_group(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_permission_group(&payload))
}

#[tauri::command]
pub fn update_permission_group(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_permission_group(&payload))
}

#[tauri::command]
pub fn delete_permission_group(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_permission_group(&id))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DepartmentGroupInput {
    pub department_id: String,
    /// Vazio = o departamento fica sem grupo (e sem nenhuma permissão herdada).
    #[serde(default)]
    pub group_id: String,
}

#[tauri::command]
pub fn set_department_permission_group(
    state: State<AppState>,
    input: DepartmentGroupInput,
) -> Result<(), String> {
    with_session(&state, |s| s.set_department_permission_group(&input.department_id, &input.group_id))
}

// ------------------------------------------------------------- requisições

#[tauri::command]
pub fn list_requests(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_requests_json())
}

#[tauri::command]
pub fn create_request(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_request(&payload))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RequestDecisionInput {
    pub id: String,
    #[serde(default)]
    pub note: String,
    pub now_iso: String,
}

#[tauri::command]
pub fn approve_request(state: State<AppState>, input: RequestDecisionInput) -> Result<String, String> {
    with_session(&state, |s| s.approve_request(&input.id, &input.note, &input.now_iso))
}

#[tauri::command]
pub fn reject_request(state: State<AppState>, input: RequestDecisionInput) -> Result<String, String> {
    with_session(&state, |s| s.reject_request(&input.id, &input.note, &input.now_iso))
}

#[tauri::command]
pub fn cancel_request(state: State<AppState>, input: RequestDecisionInput) -> Result<String, String> {
    with_session(&state, |s| s.cancel_request(&input.id, &input.note, &input.now_iso))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeliverRequestInput {
    pub id: String,
    pub now_iso: String,
    /// Prefixo dos ids das saídas geradas (<prefixo>_1, _2…) — como todo id do
    /// app, quem gera é o chamador, nunca o núcleo.
    pub movement_id_prefix: String,
}

#[tauri::command]
pub fn deliver_request(state: State<AppState>, input: DeliverRequestInput) -> Result<String, String> {
    with_session(&state, |s| s.deliver_request(&input.id, &input.now_iso, &input.movement_id_prefix))
}

#[tauri::command]
pub fn stock_availability(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.stock_availability_json())
}

// ---------------------------------------------------------------- produtos

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

#[tauri::command]
pub fn list_movements(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_movements_json())
}

#[tauri::command]
pub fn update_movement(state: State<AppState>, patch: MovementPatchInput) -> Result<String, String> {
    with_session(&state, |s| s.update_movement(patch.into()))
}

#[tauri::command]
pub fn delete_movement(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_movement(&id))
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

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RetrospectInput {
    pub year: i32,
    /// "auto" (planilha onde houver, movimentações no resto) ou "ledger".
    #[serde(default)]
    pub source: String,
    pub now_iso: String,
}

#[tauri::command]
pub fn compute_retrospect(state: State<AppState>, input: RetrospectInput) -> Result<String, String> {
    with_session(&state, |s| s.compute_retrospect_json(input.year, &input.source, &input.now_iso))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BudgetParamsInput {
    pub meta_reducao: f64,
    pub ipca: f64,
    pub piso_mensal: f64,
}

#[tauri::command]
pub fn save_budget_params(state: State<AppState>, input: BudgetParamsInput) -> Result<(), String> {
    with_session(&state, |s| s.save_budget_params(input.meta_reducao, input.ipca, input.piso_mensal))
}

#[tauri::command]
pub fn import_dept_cost_history(state: State<AppState>, payload: String) -> Result<i32, String> {
    with_session(&state, |s| s.import_dept_cost_history(&payload))
}

#[tauri::command]
pub fn backup(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.backup_json())
}

#[tauri::command]
pub fn restore_backup(state: State<AppState>, payload: String) -> Result<(), String> {
    with_session(&state, |s| s.restore_from_json(&payload))
}
