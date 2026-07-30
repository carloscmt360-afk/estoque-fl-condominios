#pragma once
// Camada de adaptação entre o núcleo de domínio (core-cpp/, que usa só
// std::string/std::vector — nunca conhece `cxx`) e a ponte cxx (que exige
// tipos `rust::String`/opacos na fronteira). core-cpp continua 100%
// testável sozinho (doctest) sem nunca incluir <rust/cxx.h>; só este shim
// conhece os dois lados.
#include <memory>

#include "estoque/api.hpp"
#include "rust/cxx.h"

namespace estoque::shim {

// Declaração adiantada, não inclusão do header gerado (bridge/src/lib.rs.h):
// esse header É QUEM inclui este arquivo (via include!("shim.hpp") na ponte)
// — incluí-lo de volta aqui criaria uma dependência circular que o guarda de
// inclusão neutralizaria silenciosamente, deixando ProductDto/DepartmentDto
// indefinidos. Declaração adiantada basta para parâmetro por valor em
// DECLARAÇÃO de função; o tipo completo só é necessário em shim.cpp, que
// inclui o header gerado normalmente (sem circularidade, é outra unidade de
// tradução).
struct ProductDto;
struct DepartmentDto;

// Nomes dos métodos em snake_case: `cxx` casa por igualdade literal de nome
// com a declaração Rust em lib.rs (sem nenhuma conversão camelCase<->snake_case).
class Session {
 public:
  explicit Session(const std::string& dbPath);

  rust::String list_products_json();
  rust::String create_product(ProductDto p);
  rust::String update_product(ProductDto p);
  void delete_product(rust::Str id);

  rust::String list_departments_json();
  rust::String create_department(DepartmentDto d);
  rust::String update_department(DepartmentDto d);
  void delete_department(rust::Str id);

  rust::String apply_entrada(rust::Str movement_id, rust::Str product_id, double qty, double unit_price,
                              rust::Str supplier, rust::Str nf, rust::Str date, rust::Str obs,
                              rust::Str created_at);
  rust::String apply_saida(rust::Str movement_id, rust::Str product_id, double qty, rust::Str department_id,
                            rust::Str date, rust::Str obs, rust::Str requester, rust::Str created_at);
  rust::String apply_correcao(rust::Str movement_id, rust::Str product_id, double qty_real, rust::Str motivo,
                               rust::Str date, rust::Str created_at);

  rust::String compute_report_json(int year, int month0, rust::Str dept_filter, int window_months,
                                    rust::Str now_iso);

  rust::String backup_json();
  void restore_from_json(rust::Str payload);

 private:
  estoque::Api api_;
};

std::unique_ptr<Session> open_session(rust::Str db_path);
rust::String resolve_data_dir_default();

}  // namespace estoque::shim
