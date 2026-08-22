// Ponte cxx entre o núcleo de negócio em C++ (core-cpp/estoque::Api) e o
// Rust (Tauri e core-cli). Compartilhada pelos dois binários: nenhum deles
// duplica a declaração da ponte.
//
// Critério híbrido: DTOs tipados (shared struct) para a ENTRADA de
// operações de escrita (segurança de tipo onde um erro é caro); todo o
// resto (listas, resultado de lançamentos, relatório, backup) trafega como
// String JSON — o relatório mensal é aninhado demais para valer a pena
// manter 3 camadas de struct em sincronia a cada novo campo.
pub mod images;
pub mod attachments;
pub mod mailer;

#[cxx::bridge(namespace = "estoque::shim")]
pub mod ffi {
    struct ProductDto {
        id: String,
        name: String,
        unit: String,
        min_stock: f64,
        category: String,
        qty: f64,
        avg_cost: f64,
        created_at: String,
    }

    struct DepartmentDto {
        id: String,
        name: String,
        encarregado: String,
        monthly_limit: f64, // teto mensal de gasto do setor; 0 = sem limite
        created_at: String,
    }

    // Cadastro de usuário. `password` vazia num update significa "não mexer
    // na senha" — nunca "apagar a senha". A senha em si não é guardada em
    // lugar nenhum: o C++ deriva o PBKDF2 dela e descarta (ver crypto.hpp).
    struct UserDto {
        id: String,
        name: String,
        email: String,
        role: String, // "superadmin" | "usuario"
        department_id: String,
        active: bool,
        created_at: String,
        password: String,
    }

    // Campos editáveis de um lançamento já gravado. `type` e `product_id`
    // `localizacao` é a região da cidade (catálogo fixo — ver
    // localizacaoCatalog em dates_engine.hpp), "" = não informado.
    struct CondominioDto {
        id: String,
        nome: String,
        nome_fantasia: String,
        cnpj: String,
        codigo: String,
        endereco: String,
        numero: String,
        complemento: String,
        bairro: String,
        cidade: String,
        estado: String,
        cep: String,
        localizacao: String,
        sindico: String,
        telefone: String,
        email: String,
        observacoes: String,
        ativo: bool,
        // Delta é a síndica deste condomínio? Ver Condominio::deltaSindica em
        // dates_engine.hpp / listDeltaSindicos em commissions_engine.cpp.
        delta_sindica: bool,
        created_at: String,
    }

    // Prazo em dias que define o vencimento do serviço (ver dates_engine.hpp);
    // `cor` é só identificação visual (hex "#RRGGBB"), sem regra de negócio.
    struct TipoServicoDto {
        id: String,
        nome: String,
        prazo_dias: i32,
        cor: String,
        created_at: String,
    }

    // não estão aqui de propósito: são imutáveis (ver MovementPatch em
    // core-cpp/include/estoque/inventory_engine.hpp). Cada tipo usa só o
    // subconjunto que faz sentido para ele — o C++ ignora e limpa o resto.
    struct MovementPatchDto {
        id: String,
        qty: f64,        // entrada/saída
        qty_real: f64,   // ajuste: quantidade contada
        unit_price: f64, // entrada/saída
        supplier: String,
        nf: String,
        department_id: String,
        requester: String,
        obs: String,
        date: String,
    }

    unsafe extern "C++" {
        include!("shim.hpp");

        type Session;

        fn open_session(db_path: &str) -> Result<UniquePtr<Session>>;

        // ---- sessão ----
        // Toda função abaixo (fora as três de sessão) exige um usuário logado
        // e confere a permissão dele do lado C++ — ver api.hpp. Os erros de
        // autorização chegam como Err com a mensagem prefixada por "[auth]"
        // (derruba para a tela de login) ou "[forbidden]" (mostra um aviso).
        fn login(
            self: Pin<&mut Session>,
            email: &str,
            password: &str,
            now_iso: &str,
        ) -> Result<String>;
        fn logout(self: Pin<&mut Session>) -> Result<()>;
        fn current_session_json(self: Pin<&mut Session>) -> Result<String>;
        fn change_own_password(
            self: Pin<&mut Session>,
            current_password: &str,
            new_password: &str,
        ) -> Result<()>;

        // Acesso total sem login, para uso FORA da interface (core-cli e
        // testes). Deliberadamente não registrada como comando Tauri em
        // src-tauri/src/main.rs: a fronteira de confiança é aquela lista, e o
        // frontend não tem como chamar o que não está lá.
        fn login_as_service(self: Pin<&mut Session>, label: &str) -> Result<()>;

        // Só superadministrador pode trocar/apagar a logo da FL — chamado
        // antes de mexer no arquivo em commands.rs (ver assertPodeEditarLogoFl
        // em core-cpp/src/api.cpp) porque upload/delete/get_app_logo não
        // passam pela sessão C++ como as outras operações.
        fn assert_pode_editar_logo_fl(self: Pin<&mut Session>) -> Result<()>;

        fn list_products_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_product(self: Pin<&mut Session>, p: ProductDto) -> Result<String>;
        fn update_product(self: Pin<&mut Session>, p: ProductDto) -> Result<String>;
        fn set_product_image(
            self: Pin<&mut Session>,
            product_id: &str,
            image_path: &str,
            thumbnail_path: &str,
        ) -> Result<String>;
        fn clear_product_image(self: Pin<&mut Session>, product_id: &str) -> Result<String>;
        fn delete_product(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn list_departments_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_department(self: Pin<&mut Session>, d: DepartmentDto) -> Result<String>;
        fn update_department(self: Pin<&mut Session>, d: DepartmentDto) -> Result<String>;
        fn delete_department(self: Pin<&mut Session>, id: &str) -> Result<()>;

        #[allow(clippy::too_many_arguments)]
        fn apply_entrada(
            self: Pin<&mut Session>,
            movement_id: &str,
            product_id: &str,
            qty: f64,
            unit_price: f64,
            supplier: &str,
            nf: &str,
            date: &str,
            obs: &str,
            created_at: &str,
        ) -> Result<String>;

        #[allow(clippy::too_many_arguments)]
        fn apply_saida(
            self: Pin<&mut Session>,
            movement_id: &str,
            product_id: &str,
            qty: f64,
            department_id: &str,
            date: &str,
            obs: &str,
            requester: &str,
            created_at: &str,
        ) -> Result<String>;

        #[allow(clippy::too_many_arguments)]
        fn apply_correcao(
            self: Pin<&mut Session>,
            movement_id: &str,
            product_id: &str,
            qty_real: f64,
            motivo: &str,
            date: &str,
            created_at: &str,
            new_avg_cost: f64,
        ) -> Result<String>;

        fn list_movements_json(self: Pin<&mut Session>) -> Result<String>;
        fn update_movement(self: Pin<&mut Session>, p: MovementPatchDto) -> Result<String>;
        fn delete_movement(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn compute_report_json(
            self: Pin<&mut Session>,
            year: i32,
            month0: i32,
            dept_filter: &str,
            window_months: i32,
            now_iso: &str,
        ) -> Result<String>;

        fn compute_retrospect_json(
            self: Pin<&mut Session>,
            year: i32,
            source: &str,
            now_iso: &str,
        ) -> Result<String>;

        fn save_budget_params(
            self: Pin<&mut Session>,
            meta_reducao: f64,
            ipca: f64,
            piso_mensal: f64,
        ) -> Result<()>;

        fn import_dept_cost_history(self: Pin<&mut Session>, payload: &str) -> Result<i32>;

        // ---- usuários (só superadmin) ----
        fn list_users_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_user(self: Pin<&mut Session>, u: UserDto) -> Result<String>;
        fn update_user(self: Pin<&mut Session>, u: UserDto) -> Result<String>;
        fn delete_user(self: Pin<&mut Session>, id: &str) -> Result<()>;
        fn reset_user_password(self: Pin<&mut Session>, id: &str, new_password: &str) -> Result<()>;

        // ---- grupos de permissão (só superadmin) ----
        // A matriz CRUD é 7 funções × 4 ações; mantê-la como struct tipada
        // significaria mexer na ponte a cada função nova. Vai como JSON, pelo
        // mesmo critério já usado no relatório (ver o cabeçalho deste arquivo).
        fn list_permissions_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_permission_group(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_permission_group(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_permission_group(self: Pin<&mut Session>, id: &str) -> Result<()>;
        fn set_department_permission_group(
            self: Pin<&mut Session>,
            department_id: &str,
            group_id: &str,
        ) -> Result<()>;

        // ---- requisições de material ----
        fn list_requests_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_request(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        // Substitui os itens de uma requisição ainda aberta — só quem valida
        // requisições pode usar (corrige quantidade errada, acrescenta item
        // esquecido pelo solicitante).
        fn update_request_items(
            self: Pin<&mut Session>,
            id: &str,
            payload: &str,
            now_iso: &str,
        ) -> Result<String>;
        fn approve_request(
            self: Pin<&mut Session>,
            id: &str,
            note: &str,
            now_iso: &str,
        ) -> Result<String>;
        fn reject_request(
            self: Pin<&mut Session>,
            id: &str,
            note: &str,
            now_iso: &str,
        ) -> Result<String>;
        fn cancel_request(
            self: Pin<&mut Session>,
            id: &str,
            note: &str,
            now_iso: &str,
        ) -> Result<String>;
        // `movement_id_prefix` vira o id das saídas geradas (<prefixo>_1, _2…):
        // como todo id do app, é o chamador que gera, nunca o núcleo.
        fn deliver_request(
            self: Pin<&mut Session>,
            id: &str,
            now_iso: &str,
            movement_id_prefix: &str,
        ) -> Result<String>;
        // Posição de estoque com a reserva das requisições em aberto descontada.
        fn stock_availability_json(self: Pin<&mut Session>) -> Result<String>;

        // ---- janela de requisições ----
        // Nenhum destes recebe `now_iso`: o prazo é conferido contra o relógio
        // do sistema lá no C++. Deixar a data vir daqui seria deixar o cliente
        // dizer se o próprio prazo dele já venceu.
        fn request_window_status_json(self: Pin<&mut Session>) -> Result<String>;
        fn list_request_windows_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_request_window(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn close_request_window_now(self: Pin<&mut Session>, id: &str) -> Result<String>;
        fn delete_request_window(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn backup_json(self: Pin<&mut Session>) -> Result<String>;
        fn restore_from_json(self: Pin<&mut Session>, payload: &str) -> Result<()>;

        // ---- gestão de prazos ----
        fn list_condominios_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_condominio(self: Pin<&mut Session>, c: CondominioDto) -> Result<String>;
        fn update_condominio(self: Pin<&mut Session>, c: CondominioDto) -> Result<String>;
        fn delete_condominio(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn list_tipos_servico_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_tipo_servico(self: Pin<&mut Session>, t: TipoServicoDto) -> Result<String>;
        fn update_tipo_servico(self: Pin<&mut Session>, t: TipoServicoDto) -> Result<String>;
        fn delete_tipo_servico(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn list_servicos_condominio_json(self: Pin<&mut Session>, now_iso: &str) -> Result<String>;
        fn create_servico_condominio(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_servico_condominio(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_servico_condominio(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn renovar_servico(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn list_renovacoes_json(
            self: Pin<&mut Session>,
            servico_condominio_filter: &str,
        ) -> Result<String>;

        // ---- fornecedores e prestadores de serviços ----
        // Tudo JSON: a empresa carrega uma lista de especialidades (N:N), que
        // uma shared struct não representa sem virar três camadas a manter em
        // sincronia — mesmo critério do relatório (ver o cabeçalho acima).
        fn list_setorizacao_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_especialidade(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_especialidade(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_especialidade(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn list_empresas_json(self: Pin<&mut Session>) -> Result<String>;
        fn list_parceiros_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_empresa(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_empresa(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_empresa(self: Pin<&mut Session>, id: &str) -> Result<()>;

        // ---- gestão sos: gerentes e carteiras ----
        fn list_gerentes_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_gerente(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_gerente(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_gerente(self: Pin<&mut Session>, id: &str) -> Result<()>;

        // ---- gestão sos: serviços e fechamentos ----
        fn list_servicos_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_servico(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_servico(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_servico(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn list_fechamentos_json(self: Pin<&mut Session>) -> Result<String>;
        fn fechar_mes(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn reabrir_fechamento(self: Pin<&mut Session>, id: &str) -> Result<()>;

        fn get_sos_config_json(self: Pin<&mut Session>) -> Result<String>;
        fn set_sos_config(self: Pin<&mut Session>, payload: &str) -> Result<()>;

        // ---- gestão sos: delta síndicos ----
        fn list_delta_sindicos_json(self: Pin<&mut Session>) -> Result<String>;

        // ---- gestão sos: dashboard de fechamento ----
        fn montar_dashboard(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn salvar_dashboard(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn list_dashboards_json(self: Pin<&mut Session>) -> Result<String>;

        // ---- gestão sos: suprimentos ----
        fn list_suprimentos_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_suprimento(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_suprimento(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_suprimento(self: Pin<&mut Session>, id: &str) -> Result<()>;

        // ---- compras: aquisições, orçamentos e pagamentos ----
        fn list_aquisicoes_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_aquisicao(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_aquisicao(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_aquisicao(self: Pin<&mut Session>, id: &str) -> Result<()>;
        fn set_aquisicao_anexo(
            self: Pin<&mut Session>,
            aquisicao_id: &str,
            anexo_path: &str,
            anexo_tipo: &str,
        ) -> Result<String>;
        fn clear_aquisicao_anexo(self: Pin<&mut Session>, aquisicao_id: &str) -> Result<String>;

        fn list_ordens_orcamento_json(self: Pin<&mut Session>, now_iso: &str) -> Result<String>;
        fn create_ordem_orcamento(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_ordem_orcamento_info(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_ordem_orcamento(self: Pin<&mut Session>, id: &str) -> Result<()>;
        fn solicitar_orcamento_para_empresas(
            self: Pin<&mut Session>,
            payload: &str,
            now_iso: &str,
        ) -> Result<String>;
        fn reenviar_solicitacao_proposta(
            self: Pin<&mut Session>,
            proposta_id: &str,
            now_iso: &str,
        ) -> Result<String>;
        fn set_proposta_valor(self: Pin<&mut Session>, proposta_id: &str, valor: f64) -> Result<String>;
        fn set_proposta_anexo(
            self: Pin<&mut Session>,
            proposta_id: &str,
            anexo_path: &str,
            anexo_tipo: &str,
        ) -> Result<String>;
        fn clear_proposta_anexo(self: Pin<&mut Session>, proposta_id: &str) -> Result<String>;
        fn marcar_proposta_recomendada(
            self: Pin<&mut Session>,
            ordem_id: &str,
            proposta_id: &str,
        ) -> Result<String>;
        fn desmarcar_proposta_recomendada(self: Pin<&mut Session>, ordem_id: &str) -> Result<String>;
        fn enviar_orcamento_para_cliente(
            self: Pin<&mut Session>,
            payload: &str,
            now_iso: &str,
        ) -> Result<String>;
        fn aprovar_proposta_orcamento(
            self: Pin<&mut Session>,
            ordem_id: &str,
            proposta_id: &str,
            now_iso: &str,
        ) -> Result<String>;
        fn reativar_ordem_orcamento(self: Pin<&mut Session>, ordem_id: &str, now_iso: &str) -> Result<String>;

        fn get_email_config_json(self: Pin<&mut Session>) -> Result<String>;
        fn set_email_config(self: Pin<&mut Session>, payload: &str) -> Result<()>;
        // Só de uso interno do comando Tauri que efetivamente envia e-mail
        // (inclui a senha, diferente de get_email_config_json) — não tem
        // wrapper em frontend/js/api.js.
        fn get_email_config_internal_json(self: Pin<&mut Session>) -> Result<String>;

        fn list_pagamentos_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_pagamento(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn update_pagamento(self: Pin<&mut Session>, payload: &str) -> Result<String>;
        fn delete_pagamento(self: Pin<&mut Session>, id: &str) -> Result<()>;
        fn marcar_parcela(self: Pin<&mut Session>, payload: &str) -> Result<String>;

        // Resolução do armazenamento portátil (dados/ ao lado do executável).
        // A versão testável/injetável (resolveDataDir(exeDir)) tem cobertura
        // via doctest em core-cpp/tests/test_portable_paths.cpp; aqui só
        // expomos a conveniência de produção (auto-detecta o exe atual).
        fn resolve_data_dir_default() -> Result<String>;
    }
}

// cxx não sabe se um tipo opaco C++ é thread-safe, então Session nasce
// !Send/!Sync por padrão — o que impede AppState (Mutex<Option<UniquePtr<Session>>>)
// de satisfazer o bound `Send + Sync` que tauri::State exige.
//
// Send é seguro de afirmar aqui porque todo acesso a Session passa por
// Pin<&mut Session> atrás do Mutex único em AppState (src-tauri/src/main.rs):
// nunca há duas threads chamando um método ao mesmo tempo, só uso sequencial
// possivelmente em threads diferentes do pool do Tauri — exatamente o caso
// coberto pelo SQLITE_THREADSAFE=1 (modo serializado) já configurado no
// vendored sqlite3.c. Não implementamos Sync: como todo método usa `&mut`
// (nunca `&`), nunca existe acesso compartilhado a uma mesma Session; o
// Mutex<T> já é Sync automaticamente para qualquer T: Send.
unsafe impl Send for ffi::Session {}
