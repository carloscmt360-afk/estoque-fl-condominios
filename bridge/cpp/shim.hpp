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
struct MovementPatchDto;
struct UserDto;

// Nomes dos métodos em snake_case: `cxx` casa por igualdade literal de nome
// com a declaração Rust em lib.rs (sem nenhuma conversão camelCase<->snake_case).
class Session {
 public:
  explicit Session(const std::string& dbPath);

  rust::String login(rust::Str email, rust::Str password, rust::Str now_iso);
  void logout();
  rust::String current_session_json();
  void change_own_password(rust::Str current_password, rust::Str new_password);
  void login_as_service(rust::Str label);

  rust::String list_users_json();
  rust::String create_user(UserDto u);
  rust::String update_user(UserDto u);
  void delete_user(rust::Str id);
  void reset_user_password(rust::Str id, rust::Str new_password);

  rust::String list_permissions_json();
  rust::String create_permission_group(rust::Str payload);
  rust::String update_permission_group(rust::Str payload);
  void delete_permission_group(rust::Str id);
  void set_department_permission_group(rust::Str department_id, rust::Str group_id);

  rust::String list_requests_json();
  rust::String create_request(rust::Str payload);
  rust::String approve_request(rust::Str id, rust::Str note, rust::Str now_iso);
  rust::String reject_request(rust::Str id, rust::Str note, rust::Str now_iso);
  rust::String cancel_request(rust::Str id, rust::Str note, rust::Str now_iso);
  rust::String deliver_request(rust::Str id, rust::Str now_iso, rust::Str movement_id_prefix);
  rust::String stock_availability_json();

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

  rust::String list_movements_json();
  rust::String update_movement(MovementPatchDto p);
  void delete_movement(rust::Str id);

  rust::String compute_report_json(int year, int month0, rust::Str dept_filter, int window_months,
                                    rust::Str now_iso);

  rust::String compute_retrospect_json(int year, rust::Str source, rust::Str now_iso);
  void save_budget_params(double meta_reducao, double ipca, double piso_mensal);
  int import_dept_cost_history(rust::Str payload);

  rust::String backup_json();
  void restore_from_json(rust::Str payload);

 private:
  estoque::Api api_;
};

std::unique_ptr<Session> open_session(rust::Str db_path);
rust::String resolve_data_dir_default();

}  // namespace estoque::shim
