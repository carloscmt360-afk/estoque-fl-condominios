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
struct CondominioDto;
struct TipoServicoDto;

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
  // Lança se quem está logado não puder trocar a logo da FL (só
  // superadministrador) — ver Api::assertPodeEditarLogoFl.
  void assert_pode_editar_logo_fl();

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
  rust::String update_request_items(rust::Str id, rust::Str payload, rust::Str now_iso);
  rust::String approve_request(rust::Str id, rust::Str note, rust::Str now_iso);
  rust::String reject_request(rust::Str id, rust::Str note, rust::Str now_iso);
  rust::String cancel_request(rust::Str id, rust::Str note, rust::Str now_iso);
  rust::String deliver_request(rust::Str id, rust::Str now_iso, rust::Str movement_id_prefix);
  rust::String stock_availability_json();

  // Janela de requisições. Sem `now_iso` em nenhum: o prazo é conferido
  // contra o relógio do sistema no C++ — uma data vinda daqui deixaria o
  // próprio cliente decidir se o prazo dele já venceu.
  rust::String request_window_status_json();
  rust::String list_request_windows_json();
  rust::String create_request_window(rust::Str payload);
  rust::String close_request_window_now(rust::Str id);
  void delete_request_window(rust::Str id);

  rust::String list_products_json();
  rust::String create_product(ProductDto p);
  rust::String update_product(ProductDto p);
  // image_path/thumbnail_path já vêm prontos (o módulo Rust de imagens é
  // quem decodifica/redimensiona/grava o .webp e resolve o caminho relativo
  // — este método só grava a referência no banco). clear_product_image
  // não mexe em arquivo nenhum, só na referência (ver o comentário de
  // inventory_engine::clearProductImage).
  rust::String set_product_image(rust::Str product_id, rust::Str image_path, rust::Str thumbnail_path);
  rust::String clear_product_image(rust::Str product_id);
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
                               rust::Str date, rust::Str created_at, double new_avg_cost);

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

  // ------------------------------------------------------ gestão de prazos
  rust::String list_condominios_json();
  rust::String create_condominio(CondominioDto c);
  rust::String update_condominio(CondominioDto c);
  void delete_condominio(rust::Str id);

  rust::String list_tipos_servico_json();
  rust::String create_tipo_servico(TipoServicoDto t);
  rust::String update_tipo_servico(TipoServicoDto t);
  void delete_tipo_servico(rust::Str id);

  rust::String list_servicos_condominio_json(rust::Str now_iso);
  rust::String create_servico_condominio(rust::Str payload);
  rust::String update_servico_condominio(rust::Str payload);
  void delete_servico_condominio(rust::Str id);

  rust::String renovar_servico(rust::Str payload);
  rust::String list_renovacoes_json(rust::Str servico_condominio_filter);

  // ----------------------------- fornecedores e prestadores de serviços
  // Tudo em JSON: a empresa carrega uma lista de especialidades (N:N), e uma
  // shared struct do cxx não representa lista de String sem virar três
  // camadas para manter em sincronia — mesmo critério do relatório mensal
  // (ver o cabeçalho de bridge/src/lib.rs).
  rust::String list_setorizacao_json();
  rust::String create_especialidade(rust::Str payload);
  rust::String update_especialidade(rust::Str payload);
  void delete_especialidade(rust::Str id);

  rust::String list_empresas_json();
  rust::String list_parceiros_json();
  rust::String create_empresa(rust::Str payload);
  rust::String update_empresa(rust::Str payload);
  void delete_empresa(rust::Str id);

  // ----------------------------- gestão sos: gerentes e carteiras
  rust::String list_gerentes_json();
  rust::String create_gerente(rust::Str payload);
  rust::String update_gerente(rust::Str payload);
  void delete_gerente(rust::Str id);

  // ----------------------------- gestão sos: serviços e fechamentos
  rust::String list_servicos_json();
  rust::String create_servico(rust::Str payload);
  rust::String update_servico(rust::Str payload);
  void delete_servico(rust::Str id);

  rust::String list_fechamentos_json();
  rust::String fechar_mes(rust::Str payload);
  void reabrir_fechamento(rust::Str id);

  rust::String get_sos_config_json();
  void set_sos_config(rust::Str payload);

  // ----------------------------------- gestão sos: delta síndicos
  // Puxado automaticamente de Serviços — não existe mais create/update/delete.
  rust::String list_delta_sindicos_json();

  // ----------------------------------- gestão sos: dashboard de fechamento
  rust::String montar_dashboard(rust::Str payload);
  rust::String salvar_dashboard(rust::Str payload);
  rust::String list_dashboards_json();
  rust::String montar_pagamento_sos(rust::Str payload);
  rust::String salvar_pagamento_sos(rust::Str payload);
  rust::String list_pagamentos_sos_json();

  // ----------------------------------- gestão sos: suprimentos
  rust::String list_suprimentos_json();
  rust::String create_suprimento(rust::Str payload);
  rust::String update_suprimento(rust::Str payload);
  void delete_suprimento(rust::Str id);

  // ------------------------------------------------------- compras
  rust::String list_aquisicoes_json();
  rust::String create_aquisicao(rust::Str payload);
  rust::String update_aquisicao(rust::Str payload);
  void delete_aquisicao(rust::Str id);
  rust::String set_aquisicao_anexo(rust::Str aquisicao_id, rust::Str anexo_path, rust::Str anexo_tipo);
  rust::String clear_aquisicao_anexo(rust::Str aquisicao_id);

  // Orçamentos: fluxo de cotação — ver Api::solicitarOrcamentoParaEmpresas
  // (api.hpp) para o porquê de "solicitar"/"enviarParaCliente"/"aprovar"
  // devolverem JSON com um array "emails" além da ordem atualizada: o C++
  // compõe o conteúdo, mas quem manda de verdade é o comando Tauri chamador
  // (src-tauri/src/commands.rs), iterando esse array com bridge::mailer.
  rust::String list_ordens_orcamento_json(rust::Str now_iso);
  rust::String create_ordem_orcamento(rust::Str payload);
  rust::String update_ordem_orcamento_info(rust::Str payload);
  void delete_ordem_orcamento(rust::Str id);
  rust::String solicitar_orcamento_para_empresas(rust::Str payload, rust::Str now_iso);
  rust::String reenviar_solicitacao_proposta(rust::Str proposta_id, rust::Str now_iso);
  rust::String set_proposta_valor(rust::Str proposta_id, double valor);
  rust::String set_proposta_anexo(rust::Str proposta_id, rust::Str anexo_path, rust::Str anexo_tipo);
  rust::String clear_proposta_anexo(rust::Str proposta_id);
  rust::String marcar_proposta_recomendada(rust::Str ordem_id, rust::Str proposta_id);
  rust::String desmarcar_proposta_recomendada(rust::Str ordem_id);
  rust::String enviar_orcamento_para_cliente(rust::Str payload, rust::Str now_iso);
  rust::String aprovar_proposta_orcamento(rust::Str ordem_id, rust::Str proposta_id, rust::Str now_iso);
  rust::String reativar_ordem_orcamento(rust::Str ordem_id, rust::Str now_iso);

  rust::String get_email_config_json();
  void set_email_config(rust::Str payload);
  rust::String get_email_config_internal_json();

  rust::String list_pagamentos_json();
  rust::String create_pagamento(rust::Str payload);
  rust::String update_pagamento(rust::Str payload);
  void delete_pagamento(rust::Str id);
  rust::String marcar_parcela(rust::Str payload);

 private:
  estoque::Api api_;
};

std::unique_ptr<Session> open_session(rust::Str db_path);
rust::String resolve_data_dir_default();

}  // namespace estoque::shim
