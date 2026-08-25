// Comandos Tauri: cada um só faz lock() no estado e delega para a ponte
// cxx — nenhuma regra de negócio mora aqui (isso é papel do core-cpp).
use crate::dto::{
    CondominioInput, DepartmentInput, MovementPatchInput, ProductInput, TipoServicoInput, UserInput,
};
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
pub struct UpdateRequestItemsInput {
    pub id: String,
    pub payload: String,
    pub now_iso: String,
}

#[tauri::command]
pub fn update_request_items(state: State<AppState>, input: UpdateRequestItemsInput) -> Result<String, String> {
    with_session(&state, |s| s.update_request_items(&input.id, &input.payload, &input.now_iso))
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

// --------------------------------------------------- janela de requisições

#[tauri::command]
pub fn request_window_status(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.request_window_status_json())
}

#[tauri::command]
pub fn list_request_windows(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_request_windows_json())
}

#[tauri::command]
pub fn create_request_window(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_request_window(&payload))
}

#[tauri::command]
pub fn close_request_window_now(state: State<AppState>, id: String) -> Result<String, String> {
    with_session(&state, |s| s.close_request_window_now(&id))
}

#[tauri::command]
pub fn delete_request_window(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_request_window(&id))
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
pub fn delete_product(state: State<AppState>, id: String, sku: String) -> Result<(), String> {
    // Exclusão de produto é sempre definitiva neste app (não existe
    // soft-delete/lixeira) — então a pasta de imagens some junto, sem
    // deixar arquivo órfão (item 12 do pedido de fotos). Apaga o arquivo
    // ANTES do registro: se a exclusão do arquivo falhar, o produto (e a
    // foto) continuam existindo dos dois lados, nunca um banco "sem foto"
    // apontando pro nada nem um arquivo solto sem dono.
    if !sku.is_empty() {
        let data_dir = resolve_data_dir()?;
        bridge::images::delete_product_images(&data_dir, &sku)?;
    }
    with_session(&state, |s| s.delete_product(&id))
}

/// Pasta "dados/" resolvida de novo a cada chamada — é idempotente e barata
/// (só cria a pasta se ainda não existir); evita guardar mais um caminho em
/// AppState só pra isto. bridge::images:: deriva "imagens/produtos/" a
/// partir dela (ver images_root em bridge/src/images.rs).
fn resolve_data_dir() -> Result<std::path::PathBuf, String> {
    bridge::ffi::resolve_data_dir_default()
        .map(std::path::PathBuf::from)
        .map_err(|e| e.what().to_string())
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UploadImageInput {
    pub product_id: String,
    pub sku: String,
    /// Bytes do arquivo original (JPG/PNG/WebP) codificados em base64 —
    /// invoke() do Tauri trafega JSON, não binário bruto.
    pub file_base64: String,
}

#[tauri::command]
pub fn upload_product_image(state: State<AppState>, input: UploadImageInput) -> Result<String, String> {
    let bytes = bridge::images::decode_base64(&input.file_base64)?;
    let data_dir = resolve_data_dir()?;
    let saved = bridge::images::save_product_image(&data_dir, &input.sku, &bytes)?;
    with_session(&state, |s| {
        s.set_product_image(&input.product_id, &saved.image_path, &saved.thumbnail_path)
    })
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeleteImageInput {
    pub product_id: String,
    pub sku: String,
}

#[tauri::command]
pub fn delete_product_image(state: State<AppState>, input: DeleteImageInput) -> Result<String, String> {
    let data_dir = resolve_data_dir()?;
    bridge::images::delete_product_images(&data_dir, &input.sku)?;
    with_session(&state, |s| s.clear_product_image(&input.product_id))
}

/// Lê uma imagem já salva (foto ou miniatura) e devolve como `data:` URL —
/// mais simples que configurar o protocolo de asset do Tauri para uma
/// pasta portátil cujo caminho só existe em tempo de execução (ver o
/// comentário de encode_base64 em bridge/src/images.rs).
#[tauri::command]
pub fn read_product_image(relative_path: String) -> Result<String, String> {
    let data_dir = resolve_data_dir()?;
    let bytes = bridge::images::read_image_bytes(&data_dir, &relative_path)?;
    Ok(format!("data:image/webp;base64,{}", bridge::images::encode_base64(&bytes)))
}

// --------------------------------------------------- anexo de NF (Aquisições)

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UploadAquisicaoAttachmentInput {
    pub aquisicao_id: String,
    /// Bytes do arquivo original (JPG/PNG/WebP ou PDF) codificados em base64
    /// — invoke() do Tauri trafega JSON, não binário bruto.
    pub file_base64: String,
}

#[tauri::command]
pub fn upload_aquisicao_attachment(
    state: State<AppState>,
    input: UploadAquisicaoAttachmentInput,
) -> Result<String, String> {
    let bytes = bridge::images::decode_base64(&input.file_base64)?;
    let data_dir = resolve_data_dir()?;
    // Troca de anexo: apaga o que já existia ANTES de gravar o novo (mesma
    // ordem de upload_product_image em relação a delete_product_images) —
    // save_aquisicao_attachment já faz essa limpeza internamente, então não
    // duplica aqui.
    let saved = bridge::attachments::save_aquisicao_attachment(&data_dir, &input.aquisicao_id, &bytes)?;
    with_session(&state, |s| {
        s.set_aquisicao_anexo(&input.aquisicao_id, &saved.path, &saved.kind)
    })
}

#[tauri::command]
pub fn delete_aquisicao_attachment(state: State<AppState>, aquisicao_id: String) -> Result<String, String> {
    let data_dir = resolve_data_dir()?;
    bridge::attachments::delete_aquisicao_attachment(&data_dir, &aquisicao_id)?;
    with_session(&state, |s| s.clear_aquisicao_anexo(&aquisicao_id))
}

/// Lê o anexo já salvo e devolve como `data:` URL — imagem vira
/// `data:image/webp`, PDF vira `data:application/pdf` (o navegador abre um
/// PDF em data: URL normalmente numa nova aba/visualizador embutido).
#[tauri::command]
pub fn read_aquisicao_attachment(relative_path: String) -> Result<String, String> {
    let data_dir = resolve_data_dir()?;
    let bytes = bridge::attachments::read_attachment_bytes(&data_dir, &relative_path)?;
    let mime = if relative_path.ends_with(".pdf") { "application/pdf" } else { "image/webp" };
    Ok(format!("data:{};base64,{}", mime, bridge::images::encode_base64(&bytes)))
}

// -------------------------------------------------------- logo da FL

/// Grava a logo da FL (mostrada sempre na barra superior dos relatórios
/// impressos) e devolve como `data:` URL já pronta pra prévia — não precisa
/// de uma segunda chamada pra reler do disco.
// Trocar/apagar é restrito (ver assert_pode_editar_logo_fl — só
// superadministrador); ver não é: qualquer usuário logado enxerga a logo já
// definida, só não pode mexer nela (get_app_logo abaixo não chama isto).
#[tauri::command]
pub fn upload_app_logo(state: State<AppState>, file_base64: String) -> Result<String, String> {
    with_session(&state, |s| s.assert_pode_editar_logo_fl())?;
    let bytes = bridge::images::decode_base64(&file_base64)?;
    let data_dir = resolve_data_dir()?;
    let saved = bridge::images::save_app_logo(&data_dir, &bytes)?;
    Ok(format!("data:image/webp;base64,{}", bridge::images::encode_base64(&saved)))
}

#[tauri::command]
pub fn delete_app_logo(state: State<AppState>) -> Result<(), String> {
    with_session(&state, |s| s.assert_pode_editar_logo_fl())?;
    let data_dir = resolve_data_dir()?;
    bridge::images::delete_app_logo(&data_dir)
}

/// `None` (null pro frontend) é o estado normal antes do usuário escolher
/// uma logo — não é erro.
#[tauri::command]
pub fn get_app_logo() -> Result<Option<String>, String> {
    let data_dir = resolve_data_dir()?;
    let bytes = bridge::images::read_app_logo_bytes(&data_dir)?;
    Ok(bytes.map(|b| format!("data:image/webp;base64,{}", bridge::images::encode_base64(&b))))
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
    /// Correção manual do custo médio, opcional. 0 (padrão) = ajuste não mexe
    /// no custo — ver o comentário de Movement::newAvgCost no core-cpp.
    #[serde(default)]
    pub new_avg_cost: f64,
}

#[tauri::command]
pub fn apply_correcao(state: State<AppState>, input: CorrecaoInput) -> Result<String, String> {
    with_session(&state, |s| {
        s.apply_correcao(
            &input.movement_id,
            &input.product_id,
            input.qty_real,
            &input.motivo,
            &input.date,
            &input.created_at,
            input.new_avg_cost,
        )
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

// ------------------------------------------------------ gestão de prazos

#[tauri::command]
pub fn list_condominios(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_condominios_json())
}

#[tauri::command]
pub fn create_condominio(state: State<AppState>, condominio: CondominioInput) -> Result<String, String> {
    with_session(&state, |s| s.create_condominio(condominio.into()))
}

#[tauri::command]
pub fn update_condominio(state: State<AppState>, condominio: CondominioInput) -> Result<String, String> {
    with_session(&state, |s| s.update_condominio(condominio.into()))
}

#[tauri::command]
pub fn delete_condominio(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_condominio(&id))
}

#[tauri::command]
pub fn list_tipos_servico(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_tipos_servico_json())
}

#[tauri::command]
pub fn create_tipo_servico(state: State<AppState>, tipo: TipoServicoInput) -> Result<String, String> {
    with_session(&state, |s| s.create_tipo_servico(tipo.into()))
}

#[tauri::command]
pub fn update_tipo_servico(state: State<AppState>, tipo: TipoServicoInput) -> Result<String, String> {
    with_session(&state, |s| s.update_tipo_servico(tipo.into()))
}

#[tauri::command]
pub fn delete_tipo_servico(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_tipo_servico(&id))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ListServicosCondominioInput {
    pub now_iso: String,
}

#[tauri::command]
pub fn list_servicos_condominio(
    state: State<AppState>,
    input: ListServicosCondominioInput,
) -> Result<String, String> {
    with_session(&state, |s| s.list_servicos_condominio_json(&input.now_iso))
}

#[tauri::command]
pub fn create_servico_condominio(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_servico_condominio(&payload))
}

#[tauri::command]
pub fn update_servico_condominio(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_servico_condominio(&payload))
}

#[tauri::command]
pub fn delete_servico_condominio(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_servico_condominio(&id))
}

#[tauri::command]
pub fn renovar_servico(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.renovar_servico(&payload))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ListRenovacoesInput {
    #[serde(default)]
    pub servico_condominio_filter: String,
}

#[tauri::command]
pub fn list_renovacoes(state: State<AppState>, input: ListRenovacoesInput) -> Result<String, String> {
    with_session(&state, |s| s.list_renovacoes_json(&input.servico_condominio_filter))
}

// ----------------------------- fornecedores e prestadores de serviços

#[tauri::command]
pub fn list_setorizacao(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_setorizacao_json())
}

#[tauri::command]
pub fn create_especialidade(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_especialidade(&payload))
}

#[tauri::command]
pub fn update_especialidade(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_especialidade(&payload))
}

#[tauri::command]
pub fn delete_especialidade(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_especialidade(&id))
}

#[tauri::command]
pub fn list_empresas(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_empresas_json())
}

/// Só as empresas marcadas como parceiras — a tela de Gestão SOS > Parceiros.
#[tauri::command]
pub fn list_parceiros(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_parceiros_json())
}

#[tauri::command]
pub fn create_empresa(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_empresa(&payload))
}

#[tauri::command]
pub fn update_empresa(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_empresa(&payload))
}

#[tauri::command]
pub fn delete_empresa(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_empresa(&id))
}

#[tauri::command]
pub fn list_gerentes(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_gerentes_json())
}

#[tauri::command]
pub fn create_gerente(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_gerente(&payload))
}

#[tauri::command]
pub fn update_gerente(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_gerente(&payload))
}

#[tauri::command]
pub fn delete_gerente(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_gerente(&id))
}

#[tauri::command]
pub fn list_servicos(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_servicos_json())
}

#[tauri::command]
pub fn create_servico(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_servico(&payload))
}

#[tauri::command]
pub fn update_servico(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_servico(&payload))
}

#[tauri::command]
pub fn delete_servico(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_servico(&id))
}

#[tauri::command]
pub fn list_fechamentos(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_fechamentos_json())
}

#[tauri::command]
pub fn fechar_mes(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.fechar_mes(&payload))
}

#[tauri::command]
pub fn reabrir_fechamento(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.reabrir_fechamento(&id))
}

#[tauri::command]
pub fn get_sos_config(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.get_sos_config_json())
}

#[tauri::command]
pub fn set_sos_config(state: State<AppState>, payload: String) -> Result<(), String> {
    with_session(&state, |s| s.set_sos_config(&payload))
}

#[tauri::command]
pub fn list_delta_sindicos(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_delta_sindicos_json())
}

#[tauri::command]
pub fn montar_dashboard(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.montar_dashboard(&payload))
}

#[tauri::command]
pub fn salvar_dashboard(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.salvar_dashboard(&payload))
}

#[tauri::command]
pub fn list_dashboards(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_dashboards_json())
}

#[tauri::command]
pub fn montar_pagamento_sos(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.montar_pagamento_sos(&payload))
}

#[tauri::command]
pub fn salvar_pagamento_sos(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.salvar_pagamento_sos(&payload))
}

#[tauri::command]
pub fn list_pagamentos_sos(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_pagamentos_sos_json())
}

#[tauri::command]
pub fn list_suprimentos(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_suprimentos_json())
}

#[tauri::command]
pub fn create_suprimento(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_suprimento(&payload))
}

#[tauri::command]
pub fn update_suprimento(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_suprimento(&payload))
}

#[tauri::command]
pub fn delete_suprimento(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_suprimento(&id))
}

#[tauri::command]
pub fn list_aquisicoes(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_aquisicoes_json())
}

#[tauri::command]
pub fn create_aquisicao(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_aquisicao(&payload))
}

#[tauri::command]
pub fn update_aquisicao(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_aquisicao(&payload))
}

#[tauri::command]
pub fn delete_aquisicao(state: State<AppState>, id: String) -> Result<(), String> {
    // Mesma ordem de delete_product em relação às imagens: apaga o arquivo
    // do anexo ANTES do registro, pra nunca sobrar um arquivo órfão sem
    // dono nem um banco "sem anexo" apontando pra um arquivo que já não
    // existe dos dois lados ao mesmo tempo.
    let data_dir = resolve_data_dir()?;
    bridge::attachments::delete_aquisicao_attachment(&data_dir, &id)?;
    with_session(&state, |s| s.delete_aquisicao(&id))
}

// ------------------------------------------- orçamentos (fluxo de cotação)
//
// "solicitar"/"enviarParaCliente"/"aprovar" são as três ações que disparam
// e-mail (ver o comentário de Api::solicitarOrcamentoParaEmpresas em
// core-cpp/include/estoque/api.hpp): a Api já fez a mudança de estado e
// devolve o JSON da ordem com um array "emails" a mais — é
// enviar_emails_compostos, abaixo, quem de fato manda cada um pelo
// bridge::mailer e devolve o mesmo JSON sem essa chave (trocada por
// "emailErros" só se algo falhar). A ação em si nunca é desfeita por uma
// falha de e-mail — o que já foi salvo no banco continua salvo.

#[derive(serde::Deserialize)]
struct EmailToSendDto {
    to: String,
    subject: String,
    #[serde(rename = "bodyHtml")]
    body_html: String,
    #[serde(rename = "attachmentPaths", default)]
    attachment_paths: Vec<String>,
}

/// Config SMTP salva, COM a senha de verdade — só para uso interno deste
/// arquivo (ver get_email_config_internal_json, que não tem wrapper em
/// frontend/js/api.js).
fn smtp_config(state: &State<AppState>) -> Result<bridge::mailer::SmtpConfig, String> {
    let json_str = with_session(state, |s| s.get_email_config_internal_json())?;
    let v: serde_json::Value = serde_json::from_str(&json_str).map_err(|e| e.to_string())?;
    Ok(bridge::mailer::SmtpConfig {
        host: v["host"].as_str().unwrap_or("").to_string(),
        port: v["port"].as_str().and_then(|p| p.parse().ok()).unwrap_or(587),
        username: v["username"].as_str().unwrap_or("").to_string(),
        password: v["password"].as_str().unwrap_or("").to_string(),
        from_email: v["fromEmail"].as_str().unwrap_or("").to_string(),
        from_name: v["fromName"].as_str().unwrap_or("FL Condomínios").to_string(),
        use_tls: v["useTls"].as_bool().unwrap_or(true),
    })
}

fn enviar_emails_compostos(state: &State<AppState>, ordem_json: String) -> Result<String, String> {
    let mut value: serde_json::Value = serde_json::from_str(&ordem_json).map_err(|e| e.to_string())?;
    let emails_val = value.get("emails").cloned().unwrap_or(serde_json::Value::Array(vec![]));
    let emails: Vec<EmailToSendDto> = serde_json::from_value(emails_val).unwrap_or_default();
    if let Some(obj) = value.as_object_mut() {
        obj.remove("emails");
    }

    if !emails.is_empty() {
        let config = smtp_config(state)?;
        let data_dir = resolve_data_dir()?;
        let mut falhas = Vec::new();
        for e in &emails {
            let email = bridge::mailer::EmailToSend {
                to: e.to.clone(),
                subject: e.subject.clone(),
                body_html: e.body_html.clone(),
                attachment_paths: e.attachment_paths.clone(),
            };
            if let Err(err) = bridge::mailer::send_email(&config, &data_dir, &email) {
                falhas.push(format!("{}: {}", e.to, err));
            }
        }
        if !falhas.is_empty() {
            if let Some(obj) = value.as_object_mut() {
                obj.insert(
                    "emailErros".to_string(),
                    serde_json::Value::Array(falhas.into_iter().map(serde_json::Value::String).collect()),
                );
            }
        }
    }

    serde_json::to_string(&value).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn list_ordens_orcamento(state: State<AppState>, now_iso: String) -> Result<String, String> {
    with_session(&state, |s| s.list_ordens_orcamento_json(&now_iso))
}

#[tauri::command]
pub fn create_ordem_orcamento(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_ordem_orcamento(&payload))
}

#[tauri::command]
pub fn update_ordem_orcamento_info(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_ordem_orcamento_info(&payload))
}

#[tauri::command]
pub fn delete_ordem_orcamento(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_ordem_orcamento(&id))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SolicitarOrcamentoInput {
    pub payload: String,
    pub now_iso: String,
}

#[tauri::command]
pub fn solicitar_orcamento_para_empresas(
    state: State<AppState>,
    input: SolicitarOrcamentoInput,
) -> Result<String, String> {
    let ordem_json =
        with_session(&state, |s| s.solicitar_orcamento_para_empresas(&input.payload, &input.now_iso))?;
    enviar_emails_compostos(&state, ordem_json)
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReenviarSolicitacaoInput {
    pub proposta_id: String,
    pub now_iso: String,
}

#[tauri::command]
pub fn reenviar_solicitacao_proposta(
    state: State<AppState>,
    input: ReenviarSolicitacaoInput,
) -> Result<String, String> {
    let ordem_json =
        with_session(&state, |s| s.reenviar_solicitacao_proposta(&input.proposta_id, &input.now_iso))?;
    enviar_emails_compostos(&state, ordem_json)
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SetPropostaValorInput {
    pub proposta_id: String,
    pub valor: f64,
}

#[tauri::command]
pub fn set_proposta_valor(state: State<AppState>, input: SetPropostaValorInput) -> Result<String, String> {
    with_session(&state, |s| s.set_proposta_valor(&input.proposta_id, input.valor))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UploadPropostaAttachmentInput {
    pub ordem_id: String,
    pub proposta_id: String,
    /// Bytes do arquivo original (JPG/PNG/WebP ou PDF) em base64 — mesmo
    /// critério de UploadAquisicaoAttachmentInput.
    pub file_base64: String,
    // Escopo/forma de pagamento/validade — pedidos na mesma tela do anexo
    // (ver modalDetalhesProposta em orcamentos.js), salvos junto num só
    // comando pra não deixar o anexo gravado sem os detalhes se a segunda
    // chamada falhasse.
    #[serde(default)]
    pub escopo: String,
    #[serde(default)]
    pub forma_pagamento: String,
    #[serde(default)]
    pub validade: String,
}

#[tauri::command]
pub fn upload_proposta_attachment(
    state: State<AppState>,
    input: UploadPropostaAttachmentInput,
) -> Result<String, String> {
    let bytes = bridge::images::decode_base64(&input.file_base64)?;
    let data_dir = resolve_data_dir()?;
    let saved =
        bridge::attachments::save_proposta_attachment(&data_dir, &input.ordem_id, &input.proposta_id, &bytes)?;
    with_session(&state, |s| s.set_proposta_anexo(&input.proposta_id, &saved.path, &saved.kind))?;
    with_session(&state, |s| {
        s.set_proposta_detalhes(&input.proposta_id, &input.escopo, &input.forma_pagamento, &input.validade)
    })
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeletePropostaAttachmentInput {
    pub ordem_id: String,
    pub proposta_id: String,
}

#[tauri::command]
pub fn delete_proposta_attachment(
    state: State<AppState>,
    input: DeletePropostaAttachmentInput,
) -> Result<String, String> {
    let data_dir = resolve_data_dir()?;
    bridge::attachments::delete_proposta_attachment(&data_dir, &input.ordem_id, &input.proposta_id)?;
    with_session(&state, |s| s.clear_proposta_anexo(&input.proposta_id))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SetPropostaDetalhesInput {
    pub proposta_id: String,
    pub escopo: String,
    pub forma_pagamento: String,
    pub validade: String,
}

// Editar depois, sem mexer no anexo — upload_proposta_attachment já grava
// os detalhes na hora de anexar; este comando é só pra corrigir escopo/forma
// de pagamento/validade de uma proposta que já tem anexo.
#[tauri::command]
pub fn set_proposta_detalhes(state: State<AppState>, input: SetPropostaDetalhesInput) -> Result<String, String> {
    with_session(&state, |s| {
        s.set_proposta_detalhes(&input.proposta_id, &input.escopo, &input.forma_pagamento, &input.validade)
    })
}

#[tauri::command]
pub fn read_proposta_attachment(relative_path: String) -> Result<String, String> {
    let data_dir = resolve_data_dir()?;
    let bytes = bridge::attachments::read_proposta_attachment_bytes(&data_dir, &relative_path)?;
    let mime = if relative_path.ends_with(".pdf") { "application/pdf" } else { "image/webp" };
    Ok(format!("data:{};base64,{}", mime, bridge::images::encode_base64(&bytes)))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MarcarPropostaRecomendadaInput {
    pub ordem_id: String,
    pub proposta_id: String,
}

#[tauri::command]
pub fn marcar_proposta_recomendada(
    state: State<AppState>,
    input: MarcarPropostaRecomendadaInput,
) -> Result<String, String> {
    with_session(&state, |s| s.marcar_proposta_recomendada(&input.ordem_id, &input.proposta_id))
}

#[tauri::command]
pub fn desmarcar_proposta_recomendada(state: State<AppState>, ordem_id: String) -> Result<String, String> {
    with_session(&state, |s| s.desmarcar_proposta_recomendada(&ordem_id))
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EnviarOrcamentoClienteInput {
    pub payload: String,
    pub now_iso: String,
}

#[tauri::command]
pub fn enviar_orcamento_para_cliente(
    state: State<AppState>,
    input: EnviarOrcamentoClienteInput,
) -> Result<String, String> {
    let ordem_json = with_session(&state, |s| s.enviar_orcamento_para_cliente(&input.payload, &input.now_iso))?;
    enviar_emails_compostos(&state, ordem_json)
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AprovarPropostaInput {
    pub ordem_id: String,
    pub proposta_id: String,
    pub now_iso: String,
}

#[tauri::command]
pub fn aprovar_proposta_orcamento(state: State<AppState>, input: AprovarPropostaInput) -> Result<String, String> {
    let ordem_json = with_session(&state, |s| {
        s.aprovar_proposta_orcamento(&input.ordem_id, &input.proposta_id, &input.now_iso)
    })?;
    enviar_emails_compostos(&state, ordem_json)
}

#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReativarOrdemInput {
    pub ordem_id: String,
    pub now_iso: String,
}

#[tauri::command]
pub fn reativar_ordem_orcamento(state: State<AppState>, input: ReativarOrdemInput) -> Result<String, String> {
    with_session(&state, |s| s.reativar_ordem_orcamento(&input.ordem_id, &input.now_iso))
}

// -------------------------------------------------------- configuração de e-mail

#[tauri::command]
pub fn get_email_config(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.get_email_config_json())
}

#[tauri::command]
pub fn set_email_config(state: State<AppState>, payload: String) -> Result<(), String> {
    with_session(&state, |s| s.set_email_config(&payload))
}

/// Manda um e-mail de teste com a config SALVA (chama Salvar antes de
/// Testar) — existe pra não precisar ir até Orçamentos e montar uma ordem
/// inteira só pra descobrir se host/porta/TLS estão certos.
#[tauri::command]
pub fn send_test_email(state: State<AppState>, to: String) -> Result<(), String> {
    let config = smtp_config(&state)?;
    let data_dir = resolve_data_dir()?;
    let email = bridge::mailer::EmailToSend {
        to,
        subject: "Teste de configuração de e-mail — Estoque FL".to_string(),
        body_html: "<p>Se esta mensagem chegou, a configuração de SMTP em \
            <b>Compras &gt; Orçamentos &gt; ⚙ E-mail</b> está funcionando.</p>".to_string(),
        attachment_paths: vec![],
    };
    bridge::mailer::send_email(&config, &data_dir, &email)
}

#[tauri::command]
pub fn list_pagamentos(state: State<AppState>) -> Result<String, String> {
    with_session(&state, |s| s.list_pagamentos_json())
}

#[tauri::command]
pub fn create_pagamento(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.create_pagamento(&payload))
}

#[tauri::command]
pub fn update_pagamento(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.update_pagamento(&payload))
}

#[tauri::command]
pub fn delete_pagamento(state: State<AppState>, id: String) -> Result<(), String> {
    with_session(&state, |s| s.delete_pagamento(&id))
}

#[tauri::command]
pub fn marcar_parcela(state: State<AppState>, payload: String) -> Result<String, String> {
    with_session(&state, |s| s.marcar_parcela(&payload))
}

/// Encerra o programa de verdade (o botão "Encerrar programa" da barra
/// lateral). Precisa existir como comando porque fechar a janela pelo X
/// apenas a esconde na bandeja — ver o on_window_event em main.rs.
#[tauri::command]
pub fn encerrar_app(app: tauri::AppHandle) {
    app.exit(0);
}

/// Só os caracteres "não reservados" da RFC 3986 passam direto — todo o
/// resto (espaço, acento, quebra de linha, `&`, `=`...) vira %XX. Itera por
/// BYTE (não char) de propósito: um caractere acentuado em UTF-8 é mais de
/// um byte, e cada byte dele tem que virar seu próprio %XX — é assim que um
/// mailto: com acento chega íntegro no cliente de e-mail.
fn percent_encode(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for b in s.as_bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => out.push(*b as char),
            _ => out.push_str(&format!("%{:02X}", b)),
        }
    }
    out
}

/// O cadastro de e-mails aceita "um ou mais, separados por vírgula" (ver
/// placeholder de #empEmails no index.html), e é natural digitar com espaço
/// depois da vírgula ("a@x.com, b@y.com"). Esse espaço cru dentro do
/// destinatário de uma URI mailto: é inválido — o Windows não reporta erro
/// nenhum, só não abre nada. enviar_emails_compostos/parse_mailboxes
/// (bridge/src/mailer.rs) já faz esse trim para o envio por SMTP; esta função
/// faz o mesmo para a URI.
fn normalizar_destinatarios_mailto(to: &str) -> String {
    to.split(',').map(str::trim).filter(|s| !s.is_empty()).collect::<Vec<_>>().join(",")
}

/// Abre o cliente de e-mail padrão do Windows (Outlook, na máquina do
/// usuário) com destinatário/assunto/corpo já preenchidos, pronto pra
/// revisar e clicar Enviar — alternativa ao envio automático por SMTP
/// (ver enviar_emails_compostos), pra quem prefere não configurar
/// servidor/senha, ou quando precisa anexar o PDF à mão (mailto: não
/// suporta anexo — limitação do protocolo, não deste app).
#[tauri::command]
pub fn abrir_email_outlook(to: String, subject: String, body: String) -> Result<(), String> {
    let uri = format!(
        "mailto:{}?subject={}&body={}",
        normalizar_destinatarios_mailto(&to),
        percent_encode(&subject),
        percent_encode(&body)
    );
    // `open::that` chama ShellExecuteW no Windows — o mesmo mecanismo que o
    // próprio Explorer usa por baixo pra despachar uma URI pro handler
    // registrado (mailto: -> cliente de e-mail padrão). Trocou de
    // `Command::new("explorer").arg(uri)` porque essa abordagem dependia de
    // explorer.exe estar alcançável no PATH do processo — falhou em teste
    // real (nada abria, sem erro nenhum) — enquanto ShellExecuteW é a API
    // correta e documentada da Microsoft pra isso, sem essa dependência.
    open::that(&uri).map_err(|e| format!("não foi possível abrir o cliente de e-mail: {e}"))
}

#[cfg(test)]
mod mailto_tests {
    use super::*;

    #[test]
    fn normaliza_espaco_apos_virgula() {
        assert_eq!(normalizar_destinatarios_mailto("a@x.com, b@y.com"), "a@x.com,b@y.com");
    }

    #[test]
    fn normaliza_espacos_extras_e_entradas_vazias() {
        assert_eq!(normalizar_destinatarios_mailto("  a@x.com , , b@y.com  "), "a@x.com,b@y.com");
    }

    #[test]
    fn mantem_um_unico_destinatario_sem_virgula() {
        assert_eq!(normalizar_destinatarios_mailto("a@x.com"), "a@x.com");
    }

    #[test]
    fn uri_montada_nao_contem_espaco_cru_no_destinatario() {
        let uri = format!(
            "mailto:{}?subject={}&body={}",
            normalizar_destinatarios_mailto("a@x.com, b@y.com"),
            percent_encode("Assunto de teste"),
            percent_encode("Corpo do e-mail")
        );
        let destino = uri.split('?').next().unwrap();
        assert!(!destino.contains(' '), "destinatário não pode ter espaço cru: {destino}");
    }
}
