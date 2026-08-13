// Casca Tauri: nenhuma lógica de negócio aqui. Resolve o caminho portátil
// (dados/ ao lado do executável) no startup, abre a sessão via a ponte cxx,
// e expõe comandos finos que só fazem lock()+delegam (ver commands.rs).
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod dto;

use bridge::ffi;
use std::sync::Mutex;
use tauri::Manager;

/// Estado da aplicação: ou há uma sessão pronta, ou há um erro de
/// inicialização (tipicamente a pasta `dados/` não pôde ser criada) que o
/// frontend mostra como tela de erro com botão "Tentar novamente" — nunca
/// um crash silencioso, nunca um fallback para outro local escondido
/// (quebraria a garantia de portabilidade).
pub struct AppState {
    pub session: Mutex<Option<cxx::UniquePtr<ffi::Session>>>,
    pub init_error: Mutex<Option<String>>,
}

impl AppState {
    fn empty() -> Self {
        AppState { session: Mutex::new(None), init_error: Mutex::new(None) }
    }
}

/// Resolve dados/ + abre o SQLite; popula `state.session` ou `state.init_error`.
/// Chamado no startup e de novo pelo comando `retry_init` (o botão "Tentar
/// novamente" da tela de erro).
pub fn init_session(state: &AppState) {
    let result = ffi::resolve_data_dir_default().and_then(|data_dir| {
        let db_path = format!("{data_dir}/estoque.db");
        ffi::open_session(&db_path)
    });

    match result {
        Ok(session) => {
            *state.session.lock().unwrap() = Some(session);
            *state.init_error.lock().unwrap() = None;
        }
        Err(e) => {
            *state.session.lock().unwrap() = None;
            *state.init_error.lock().unwrap() = Some(e.what().to_string());
        }
    }
}

fn main() {
    tauri::Builder::default()
        .manage(AppState::empty())
        .setup(|app| {
            let state: tauri::State<AppState> = app.state();
            init_session(&state);
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::app_status,
            commands::retry_init,
            // Sessão. `login_as_service` (a ponte tem) NÃO entra aqui de
            // propósito: é acesso total sem senha, só para o core-cli.
            commands::login,
            commands::logout,
            commands::current_session,
            commands::change_own_password,
            commands::list_users,
            commands::create_user,
            commands::update_user,
            commands::delete_user,
            commands::reset_user_password,
            commands::list_permissions,
            commands::create_permission_group,
            commands::update_permission_group,
            commands::delete_permission_group,
            commands::set_department_permission_group,
            commands::list_requests,
            commands::create_request,
            commands::approve_request,
            commands::reject_request,
            commands::cancel_request,
            commands::deliver_request,
            commands::stock_availability,
            commands::list_products,
            commands::create_product,
            commands::update_product,
            commands::delete_product,
            commands::list_departments,
            commands::create_department,
            commands::update_department,
            commands::delete_department,
            commands::apply_entrada,
            commands::apply_saida,
            commands::apply_correcao,
            commands::list_movements,
            commands::update_movement,
            commands::delete_movement,
            commands::compute_report,
            commands::compute_retrospect,
            commands::save_budget_params,
            commands::import_dept_cost_history,
            commands::backup,
            commands::restore_backup,
        ])
        .run(tauri::generate_context!())
        .expect("erro ao iniciar o Estoque FL Condomínios");
}
