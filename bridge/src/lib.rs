// Ponte cxx entre o núcleo de negócio em C++ (core-cpp/estoque::Api) e o
// Rust (Tauri e core-cli). Compartilhada pelos dois binários: nenhum deles
// duplica a declaração da ponte.
//
// Critério híbrido: DTOs tipados (shared struct) para a ENTRADA de
// operações de escrita (segurança de tipo onde um erro é caro); todo o
// resto (listas, resultado de lançamentos, relatório, backup) trafega como
// String JSON — o relatório mensal é aninhado demais para valer a pena
// manter 3 camadas de struct em sincronia a cada novo campo.
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

        fn list_products_json(self: Pin<&mut Session>) -> Result<String>;
        fn create_product(self: Pin<&mut Session>, p: ProductDto) -> Result<String>;
        fn update_product(self: Pin<&mut Session>, p: ProductDto) -> Result<String>;
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

        fn apply_correcao(
            self: Pin<&mut Session>,
            movement_id: &str,
            product_id: &str,
            qty_real: f64,
            motivo: &str,
            date: &str,
            created_at: &str,
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

        fn backup_json(self: Pin<&mut Session>) -> Result<String>;
        fn restore_from_json(self: Pin<&mut Session>, payload: &str) -> Result<()>;

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
