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
