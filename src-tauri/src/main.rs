// Casca Tauri: nenhuma lógica de negócio aqui. Resolve o caminho portátil
// (dados/ ao lado do executável) no startup, abre a sessão via a ponte cxx,
// e expõe comandos finos que só fazem lock()+delegam (ver commands.rs).
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod dto;

use bridge::ffi;
use std::sync::Mutex;
use tauri::menu::{Menu, MenuItem};
use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
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
        adotar_dados_de_instalacao_anterior(&data_dir);
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

/// Primeira abertura depois de o produto ter sido renomeado: o instalador põe
/// o programa numa pasta nova, e como o app é portátil (banco/imagens/anexos
/// ficam ao lado do executável), a instalação nova nasceria vazia com todos os
/// dados presos na pasta antiga. Isto traz os dados de lá — copiando, nunca
/// movendo. Ver bridge/src/migracao.rs.
///
/// Falhar aqui NUNCA pode impedir o app de abrir: os dados antigos continuam
/// onde sempre estiveram, e o usuário consegue abrir a instalação anterior ou
/// copiar a pasta à mão. Por isso o erro só vai pro log, não vira init_error.
fn adotar_dados_de_instalacao_anterior(data_dir: &str) {
    let base = match std::path::Path::new(data_dir).parent() {
        Some(b) => b.to_path_buf(),
        None => return,
    };
    match bridge::migracao::migrar_instalacao_anterior(&base) {
        Ok(Some(m)) => eprintln!(
            "Dados adotados da instalação anterior em '{}' ({} arquivo(s)). \
             A pasta antiga foi mantida intacta.",
            m.origem.display(),
            m.arquivos_copiados
        ),
        Ok(None) => {}
        Err(e) => eprintln!("Não foi possível trazer os dados da instalação anterior: {e}"),
    }
}

/// Traz a janela de volta da bandeja: reexibe, restaura se estava minimizada
/// e põe na frente. Os três passos são necessários — só `show()` devolve uma
/// janela escondida ATRÁS das outras, e uma janela minimizada antes de ser
/// escondida continua minimizada ao reaparecer.
fn mostrar_janela(app: &tauri::AppHandle) {
    if let Some(win) = app.get_webview_window("main") {
        let _ = win.show();
        let _ = win.unminimize();
        let _ = win.set_focus();
    }
}

/// Ícone na bandeja do sistema, com menu de contexto (botão direito):
/// "Abrir" e "Encerrar". Clique esquerdo simples também reabre a janela,
/// que é o gesto que a maioria dos usuários tenta primeiro.
fn instalar_bandeja(app: &tauri::AppHandle) -> tauri::Result<()> {
    let abrir = MenuItem::with_id(app, "abrir", "Abrir Gestão de Suprimentos", true, None::<&str>)?;
    let encerrar = MenuItem::with_id(app, "encerrar", "Encerrar", true, None::<&str>)?;
    let menu = Menu::with_items(app, &[&abrir, &encerrar])?;

    let mut construtor = TrayIconBuilder::with_id("bandeja");
    // Sem ícone o item da bandeja fica invisível, mas derrubar o app inteiro
    // por causa disso seria pior: se o contexto não trouxer o ícone, a
    // bandeja é criada mesmo assim (no Windows ele vem sempre do icon.ico).
    if let Some(icone) = app.default_window_icon() {
        construtor = construtor.icon(icone.clone());
    }
    construtor
        .tooltip("Gestão de Suprimentos - FL")
        .menu(&menu)
        // false: o clique esquerdo é tratado por nós (reabrir a janela) em vez
        // de abrir o menu, que é o comportamento padrão do Windows.
        .show_menu_on_left_click(false)
        .on_menu_event(|app, event| match event.id.as_ref() {
            "abrir" => mostrar_janela(app),
            // exit() encerra de verdade — é a única saída do app junto com o
            // botão "Encerrar programa" da barra lateral.
            "encerrar" => app.exit(0),
            _ => {}
        })
        .on_tray_icon_event(|tray, event| {
            if let TrayIconEvent::Click { button: MouseButton::Left, button_state: MouseButtonState::Up, .. } = event {
                mostrar_janela(tray.app_handle());
            }
        })
        .build(app)?;
    Ok(())
}

fn main() {
    tauri::Builder::default()
        .manage(AppState::empty())
        .setup(|app| {
            let state: tauri::State<AppState> = app.state();
            init_session(&state);
            instalar_bandeja(app.handle())?;
            Ok(())
        })
        // O X da janela ESCONDE em vez de encerrar: o app fica vivo na bandeja
        // (é assim que ele continua "aberto" sem ocupar a barra de tarefas).
        // Encerrar de verdade só pelo menu da bandeja ou pelo botão
        // "Encerrar programa" dentro do app.
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::app_status,
            commands::retry_init,
            commands::encerrar_app,
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
            commands::update_request_items,
            commands::approve_request,
            commands::reject_request,
            commands::cancel_request,
            commands::deliver_request,
            commands::stock_availability,
            commands::request_window_status,
            commands::list_request_windows,
            commands::create_request_window,
            commands::close_request_window_now,
            commands::delete_request_window,
            commands::list_products,
            commands::create_product,
            commands::update_product,
            commands::delete_product,
            commands::upload_product_image,
            commands::delete_product_image,
            commands::read_product_image,
            commands::upload_app_logo,
            commands::delete_app_logo,
            commands::get_app_logo,
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
            commands::list_condominios,
            commands::create_condominio,
            commands::update_condominio,
            commands::delete_condominio,
            commands::iniciar_aviso_previo,
            commands::cancelar_aviso_previo,
            commands::list_tipos_servico,
            commands::create_tipo_servico,
            commands::update_tipo_servico,
            commands::delete_tipo_servico,
            commands::list_servicos_condominio,
            commands::create_servico_condominio,
            commands::update_servico_condominio,
            commands::delete_servico_condominio,
            commands::renovar_servico,
            commands::list_renovacoes,
            commands::list_setorizacao,
            commands::create_especialidade,
            commands::update_especialidade,
            commands::delete_especialidade,
            commands::list_empresas,
            commands::list_parceiros,
            commands::create_empresa,
            commands::update_empresa,
            commands::delete_empresa,
            commands::list_gerentes,
            commands::create_gerente,
            commands::update_gerente,
            commands::delete_gerente,
            commands::list_servicos,
            commands::create_servico,
            commands::update_servico,
            commands::delete_servico,
            commands::list_fechamentos,
            commands::fechar_mes,
            commands::reabrir_fechamento,
            commands::get_sos_config,
            commands::set_sos_config,
            commands::list_delta_sindicos,
            commands::montar_dashboard,
            commands::salvar_dashboard,
            commands::list_dashboards,
            commands::montar_pagamento_sos,
            commands::salvar_pagamento_sos,
            commands::list_pagamentos_sos,
            commands::list_suprimentos,
            commands::create_suprimento,
            commands::update_suprimento,
            commands::delete_suprimento,
            commands::list_aquisicoes,
            commands::create_aquisicao,
            commands::update_aquisicao,
            commands::delete_aquisicao,
            commands::upload_aquisicao_attachment,
            commands::delete_aquisicao_attachment,
            commands::read_aquisicao_attachment,
            commands::list_ordens_orcamento,
            commands::create_ordem_orcamento,
            commands::update_ordem_orcamento_info,
            commands::delete_ordem_orcamento,
            commands::solicitar_orcamento_para_empresas,
            commands::reenviar_solicitacao_proposta,
            commands::set_proposta_valor,
            commands::upload_proposta_attachment,
            commands::set_proposta_detalhes,
            commands::delete_proposta_attachment,
            commands::read_proposta_attachment,
            commands::marcar_proposta_recomendada,
            commands::desmarcar_proposta_recomendada,
            commands::enviar_orcamento_para_cliente,
            commands::aprovar_proposta_orcamento,
            commands::reativar_ordem_orcamento,
            commands::get_email_config,
            commands::set_email_config,
            commands::send_test_email,
            commands::abrir_email_outlook,
            commands::list_pagamentos,
            commands::create_pagamento,
            commands::update_pagamento,
            commands::delete_pagamento,
            commands::marcar_parcela,
        ])
        .run(tauri::generate_context!())
        .expect("erro ao iniciar a Gestão de Suprimentos - FL");
}
