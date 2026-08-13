// DTOs desserializáveis do JSON que o frontend envia via invoke(). Não dá
// para usar bridge::ffi::ProductDto/DepartmentDto diretamente como parâmetro
// de comando Tauri — são structs geradas pelo `cxx` e não implementam
// `serde::Deserialize`. Esta é a camada fina de conversão: JSON camelCase
// (mesmo formato usado no resto da API) -> struct Rust -> DTO da ponte cxx.
use bridge::ffi;
use serde::Deserialize;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProductInput {
    pub id: String,
    pub name: String,
    pub unit: String,
    #[serde(default)]
    pub min_stock: f64,
    #[serde(default)]
    pub category: String,
    #[serde(default)]
    pub qty: f64,
    #[serde(default)]
    pub avg_cost: f64,
    pub created_at: String,
}

impl From<ProductInput> for ffi::ProductDto {
    fn from(p: ProductInput) -> Self {
        ffi::ProductDto {
            id: p.id,
            name: p.name,
            unit: p.unit,
            min_stock: p.min_stock,
            category: p.category,
            qty: p.qty,
            avg_cost: p.avg_cost,
            created_at: p.created_at,
        }
    }
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DepartmentInput {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub encarregado: String,
    /// Teto mensal de gasto do setor. `default` porque o formulário pode não
    /// mandar o campo (0 = sem limite, que é o comportamento anterior).
    #[serde(default)]
    pub monthly_limit: f64,
    pub created_at: String,
}

impl From<DepartmentInput> for ffi::DepartmentDto {
    fn from(d: DepartmentInput) -> Self {
        ffi::DepartmentDto {
            id: d.id,
            name: d.name,
            encarregado: d.encarregado,
            monthly_limit: d.monthly_limit,
            created_at: d.created_at,
        }
    }
}

/// Cadastro de usuário vindo do formulário. `password` vazia num update
/// significa "não mexer na senha" (o formulário de edição não pede a senha).
#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UserInput {
    pub id: String,
    pub name: String,
    pub email: String,
    pub role: String,
    #[serde(default)]
    pub department_id: String,
    #[serde(default = "default_true")]
    pub active: bool,
    #[serde(default)]
    pub created_at: String,
    #[serde(default)]
    pub password: String,
}

fn default_true() -> bool {
    true
}

impl From<UserInput> for ffi::UserDto {
    fn from(u: UserInput) -> Self {
        ffi::UserDto {
            id: u.id,
            name: u.name,
            email: u.email,
            role: u.role,
            department_id: u.department_id,
            active: u.active,
            created_at: u.created_at,
            password: u.password,
        }
    }
}

/// Edição de um lançamento existente. Todo campo opcional tem `default`: o
/// formulário só manda o subconjunto que o tipo do lançamento usa (uma
/// entrada não tem departamento, um ajuste não tem fornecedor).
#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct MovementPatchInput {
    pub id: String,
    #[serde(default)]
    pub qty: f64,
    #[serde(default)]
    pub qty_real: f64,
    #[serde(default)]
    pub unit_price: f64,
    #[serde(default)]
    pub supplier: String,
    #[serde(default)]
    pub nf: String,
    #[serde(default)]
    pub department_id: String,
    #[serde(default)]
    pub requester: String,
    #[serde(default)]
    pub obs: String,
    #[serde(default)]
    pub date: String,
}

impl From<MovementPatchInput> for ffi::MovementPatchDto {
    fn from(m: MovementPatchInput) -> Self {
        ffi::MovementPatchDto {
            id: m.id,
            qty: m.qty,
            qty_real: m.qty_real,
            unit_price: m.unit_price,
            supplier: m.supplier,
            nf: m.nf,
            department_id: m.department_id,
            requester: m.requester,
            obs: m.obs,
            date: m.date,
        }
    }
}
