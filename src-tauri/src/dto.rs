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
    pub created_at: String,
}

impl From<DepartmentInput> for ffi::DepartmentDto {
    fn from(d: DepartmentInput) -> Self {
        ffi::DepartmentDto { id: d.id, name: d.name, encarregado: d.encarregado, created_at: d.created_at }
    }
}
