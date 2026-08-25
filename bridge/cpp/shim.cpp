#include "shim.hpp"

#include <nlohmann/json.hpp>

// Aqui (uma unidade de tradução separada de lib.rs.h, não incluída por ele)
// não há circularidade: podemos incluir o header gerado normalmente para
// obter a definição completa de ProductDto/DepartmentDto.
#include "bridge/src/lib.rs.h"
#include "estoque/portable_paths.hpp"

namespace estoque::shim {

using json = nlohmann::json;

namespace {

Product toProduct(const ProductDto& d) {
  Product p;
  p.id = std::string(d.id);
  p.name = std::string(d.name);
  p.unit = std::string(d.unit);
  p.minStock = d.min_stock;
  p.category = std::string(d.category);
  p.qty = d.qty;
  p.avgCost = d.avg_cost;
  p.createdAt = std::string(d.created_at);
  return p;
}

MovementPatch toMovementPatch(const MovementPatchDto& d) {
  MovementPatch p;
  p.id = std::string(d.id);
  p.qty = d.qty;
  p.qtyReal = d.qty_real;
  p.unitPrice = d.unit_price;
  p.supplier = std::string(d.supplier);
  p.nf = std::string(d.nf);
  p.departmentId = std::string(d.department_id);
  p.requester = std::string(d.requester);
  p.obs = std::string(d.obs);
  p.date = std::string(d.date);
  return p;
}

UserInput toUserInput(const UserDto& d) {
  UserInput u;
  u.id = std::string(d.id);
  u.name = std::string(d.name);
  u.email = std::string(d.email);
  u.role = std::string(d.role);
  u.departmentId = std::string(d.department_id);
  u.active = d.active;
  u.createdAt = std::string(d.created_at);
  u.password = std::string(d.password);
  return u;
}

Condominio toCondominio(const CondominioDto& d) {
  Condominio c;
  c.id = std::string(d.id);
  c.nome = std::string(d.nome);
  c.nomeFantasia = std::string(d.nome_fantasia);
  c.cnpj = std::string(d.cnpj);
  c.codigo = std::string(d.codigo);
  c.endereco = std::string(d.endereco);
  c.numero = std::string(d.numero);
  c.complemento = std::string(d.complemento);
  c.bairro = std::string(d.bairro);
  c.cidade = std::string(d.cidade);
  c.estado = std::string(d.estado);
  c.cep = std::string(d.cep);
  c.localizacao = std::string(d.localizacao);
  c.sindico = std::string(d.sindico);
  c.telefone = std::string(d.telefone);
  c.email = std::string(d.email);
  c.observacoes = std::string(d.observacoes);
  c.ativo = d.ativo;
  c.deltaSindica = d.delta_sindica;
  c.createdAt = std::string(d.created_at);
  return c;
}

TipoServico toTipoServico(const TipoServicoDto& d) {
  TipoServico t;
  t.id = std::string(d.id);
  t.nome = std::string(d.nome);
  t.prazoDias = d.prazo_dias;
  t.cor = std::string(d.cor);
  t.createdAt = std::string(d.created_at);
  return t;
}

json condominioToJson(const Condominio& c) {
  json j;
  j["id"] = c.id;
  j["nome"] = c.nome;
  j["nomeFantasia"] = c.nomeFantasia;
  j["cnpj"] = c.cnpj;
  j["codigo"] = c.codigo;
  j["endereco"] = c.endereco;
  j["numero"] = c.numero;
  j["complemento"] = c.complemento;
  j["bairro"] = c.bairro;
  j["cidade"] = c.cidade;
  j["estado"] = c.estado;
  j["cep"] = c.cep;
  j["localizacao"] = c.localizacao;
  j["localizacaoLabel"] = localizacaoLabel(c.localizacao);
  j["sindico"] = c.sindico;
  j["telefone"] = c.telefone;
  j["email"] = c.email;
  j["observacoes"] = c.observacoes;
  j["ativo"] = c.ativo;
  j["deltaSindica"] = c.deltaSindica;
  j["createdAt"] = c.createdAt;
  return j;
}

json tipoServicoToJson(const TipoServico& t) {
  json j;
  j["id"] = t.id;
  j["nome"] = t.nome;
  j["prazoDias"] = t.prazoDias;
  j["cor"] = t.cor;
  j["createdAt"] = t.createdAt;
  return j;
}

Department toDepartment(const DepartmentDto& d) {
  Department dep;
  dep.id = std::string(d.id);
  dep.name = std::string(d.name);
  dep.encarregado = std::string(d.encarregado);
  dep.monthlyLimit = d.monthly_limit;
  dep.createdAt = std::string(d.created_at);
  return dep;
}

json productToJson(const Product& p) {
  json j;
  j["id"] = p.id;
  j["name"] = p.name;
  j["unit"] = p.unit;
  j["minStock"] = p.minStock;
  j["category"] = p.category;
  j["qty"] = p.qty;
  j["avgCost"] = p.avgCost;
  j["createdAt"] = p.createdAt;
  j["sku"] = p.sku;
  j["imagePath"] = p.imagePath;
  j["thumbnailPath"] = p.thumbnailPath;
  return j;
}

json departmentToJson(const Department& d) {
  json j;
  j["id"] = d.id;
  j["name"] = d.name;
  j["encarregado"] = d.encarregado;
  j["monthlyLimit"] = d.monthlyLimit;
  j["createdAt"] = d.createdAt;
  return j;
}

json movementToJson(const Movement& m) {
  json j;
  j["id"] = m.id;
  j["type"] = movementTypeToString(m.type);
  j["productId"] = m.productId;
  j["qty"] = m.qty;
  j["unitPrice"] = m.unitPrice;
  j["supplier"] = m.supplier;
  j["nf"] = m.nf;
  j["departmentId"] = m.departmentId;
  j["recipient"] = m.recipient;
  j["encarregado"] = m.encarregado;
  j["requester"] = m.requester;
  j["obs"] = m.obs;
  j["date"] = m.date;
  j["resultingQty"] = m.resultingQty;
  j["resultingAvgCost"] = m.resultingAvgCost;
  j["createdAt"] = m.createdAt;
  j["newAvgCost"] = m.newAvgCost;
  return j;
}

}  // namespace

Session::Session(const std::string& dbPath) : api_(dbPath) {}

// ------------------------------------------------------------------ sessão

rust::String Session::login(rust::Str email, rust::Str password, rust::Str now_iso) {
  return rust::String(api_.login(std::string(email), std::string(password), std::string(now_iso)));
}

void Session::logout() { api_.logout(); }

rust::String Session::current_session_json() { return rust::String(api_.currentSessionJson()); }

void Session::change_own_password(rust::Str current_password, rust::Str new_password) {
  api_.changeOwnPassword(std::string(current_password), std::string(new_password));
}

void Session::login_as_service(rust::Str label) { api_.loginAsService(std::string(label)); }

void Session::assert_pode_editar_logo_fl() { api_.assertPodeEditarLogoFl(); }

// ---------------------------------------------------------------- usuários

rust::String Session::list_users_json() { return rust::String(api_.listUsersJson()); }

rust::String Session::create_user(UserDto u) { return rust::String(api_.createUser(toUserInput(u))); }

rust::String Session::update_user(UserDto u) { return rust::String(api_.updateUser(toUserInput(u))); }

void Session::delete_user(rust::Str id) { api_.deleteUser(std::string(id)); }

void Session::reset_user_password(rust::Str id, rust::Str new_password) {
  api_.resetUserPassword(std::string(id), std::string(new_password));
}

// -------------------------------------------------------------- permissões

rust::String Session::list_permissions_json() { return rust::String(api_.listPermissionsJson()); }

rust::String Session::create_permission_group(rust::Str payload) {
  return rust::String(api_.createPermissionGroup(std::string(payload)));
}

rust::String Session::update_permission_group(rust::Str payload) {
  return rust::String(api_.updatePermissionGroup(std::string(payload)));
}

void Session::delete_permission_group(rust::Str id) { api_.deletePermissionGroup(std::string(id)); }

void Session::set_department_permission_group(rust::Str department_id, rust::Str group_id) {
  api_.setDepartmentPermissionGroup(std::string(department_id), std::string(group_id));
}

// ------------------------------------------------------------- requisições

rust::String Session::list_requests_json() { return rust::String(api_.listRequestsJson()); }

rust::String Session::create_request(rust::Str payload) {
  return rust::String(api_.createRequest(std::string(payload)));
}

rust::String Session::update_request_items(rust::Str id, rust::Str payload, rust::Str now_iso) {
  return rust::String(api_.updateRequestItems(std::string(id), std::string(payload), std::string(now_iso)));
}

rust::String Session::approve_request(rust::Str id, rust::Str note, rust::Str now_iso) {
  return rust::String(api_.approveRequest(std::string(id), std::string(note), std::string(now_iso)));
}

rust::String Session::reject_request(rust::Str id, rust::Str note, rust::Str now_iso) {
  return rust::String(api_.rejectRequest(std::string(id), std::string(note), std::string(now_iso)));
}

rust::String Session::cancel_request(rust::Str id, rust::Str note, rust::Str now_iso) {
  return rust::String(api_.cancelRequest(std::string(id), std::string(note), std::string(now_iso)));
}

rust::String Session::deliver_request(rust::Str id, rust::Str now_iso, rust::Str movement_id_prefix) {
  return rust::String(
      api_.deliverRequest(std::string(id), std::string(now_iso), std::string(movement_id_prefix)));
}

rust::String Session::stock_availability_json() { return rust::String(api_.stockAvailabilityJson()); }

// --------------------------------------------------- janela de requisições

rust::String Session::request_window_status_json() {
  return rust::String(api_.requestWindowStatusJson());
}

rust::String Session::list_request_windows_json() {
  return rust::String(api_.listRequestWindowsJson());
}

rust::String Session::create_request_window(rust::Str payload) {
  return rust::String(api_.createRequestWindow(std::string(payload)));
}

rust::String Session::close_request_window_now(rust::Str id) {
  return rust::String(api_.closeRequestWindowNow(std::string(id)));
}

void Session::delete_request_window(rust::Str id) { api_.deleteRequestWindow(std::string(id)); }

// ---------------------------------------------------------------- produtos

rust::String Session::list_products_json() {
  json arr = json::array();
  for (auto& p : api_.listProducts()) arr.push_back(productToJson(p));
  return rust::String(arr.dump());
}

rust::String Session::create_product(ProductDto p) {
  return rust::String(productToJson(api_.createProduct(toProduct(p))).dump());
}

rust::String Session::update_product(ProductDto p) {
  return rust::String(productToJson(api_.updateProduct(toProduct(p))).dump());
}

rust::String Session::set_product_image(rust::Str product_id, rust::Str image_path, rust::Str thumbnail_path) {
  auto p = api_.setProductImage(std::string(product_id), std::string(image_path), std::string(thumbnail_path));
  return rust::String(productToJson(p).dump());
}

rust::String Session::clear_product_image(rust::Str product_id) {
  return rust::String(productToJson(api_.clearProductImage(std::string(product_id))).dump());
}

void Session::delete_product(rust::Str id) { api_.deleteProduct(std::string(id)); }

rust::String Session::list_departments_json() {
  json arr = json::array();
  for (auto& d : api_.listDepartments()) arr.push_back(departmentToJson(d));
  return rust::String(arr.dump());
}

rust::String Session::create_department(DepartmentDto d) {
  return rust::String(departmentToJson(api_.createDepartment(toDepartment(d))).dump());
}

rust::String Session::update_department(DepartmentDto d) {
  return rust::String(departmentToJson(api_.updateDepartment(toDepartment(d))).dump());
}

void Session::delete_department(rust::Str id) { api_.deleteDepartment(std::string(id)); }

rust::String Session::apply_entrada(rust::Str movement_id, rust::Str product_id, double qty,
                                     double unit_price, rust::Str supplier, rust::Str nf, rust::Str date,
                                     rust::Str obs, rust::Str created_at) {
  auto m = api_.applyEntrada(std::string(movement_id), std::string(product_id), qty, unit_price,
                             std::string(supplier), std::string(nf), std::string(date), std::string(obs),
                             std::string(created_at));
  return rust::String(movementToJson(m).dump());
}

rust::String Session::apply_saida(rust::Str movement_id, rust::Str product_id, double qty,
                                   rust::Str department_id, rust::Str date, rust::Str obs,
                                   rust::Str requester, rust::Str created_at) {
  auto m = api_.applySaida(std::string(movement_id), std::string(product_id), qty,
                           std::string(department_id), std::string(date), std::string(obs),
                           std::string(requester), std::string(created_at));
  return rust::String(movementToJson(m).dump());
}

rust::String Session::apply_correcao(rust::Str movement_id, rust::Str product_id, double qty_real,
                                      rust::Str motivo, rust::Str date, rust::Str created_at,
                                      double new_avg_cost) {
  auto m = api_.applyCorrecao(std::string(movement_id), std::string(product_id), qty_real,
                              std::string(motivo), std::string(date), std::string(created_at), new_avg_cost);
  return rust::String(movementToJson(m).dump());
}

rust::String Session::list_movements_json() {
  json arr = json::array();
  for (auto& m : api_.listMovements()) arr.push_back(movementToJson(m));
  return rust::String(arr.dump());
}

rust::String Session::update_movement(MovementPatchDto p) {
  return rust::String(movementToJson(api_.updateMovement(toMovementPatch(p))).dump());
}

void Session::delete_movement(rust::Str id) { api_.deleteMovement(std::string(id)); }

rust::String Session::compute_report_json(int year, int month0, rust::Str dept_filter, int window_months,
                                           rust::Str now_iso) {
  return rust::String(api_.computeReportJson(year, month0, std::string(dept_filter), window_months,
                                              std::string(now_iso)));
}

rust::String Session::compute_retrospect_json(int year, rust::Str source, rust::Str now_iso) {
  return rust::String(api_.computeRetrospectJson(year, std::string(source), std::string(now_iso)));
}

void Session::save_budget_params(double meta_reducao, double ipca, double piso_mensal) {
  api_.saveBudgetParams(meta_reducao, ipca, piso_mensal);
}

int Session::import_dept_cost_history(rust::Str payload) {
  return api_.importDeptCostHistory(std::string(payload));
}

rust::String Session::backup_json() { return rust::String(api_.backupJson()); }

void Session::restore_from_json(rust::Str payload) { api_.restoreFromJson(std::string(payload)); }

// ------------------------------------------------------ gestão de prazos

rust::String Session::list_condominios_json() {
  json arr = json::array();
  for (auto& c : api_.listCondominios()) arr.push_back(condominioToJson(c));
  return rust::String(arr.dump());
}

rust::String Session::create_condominio(CondominioDto c) {
  return rust::String(condominioToJson(api_.createCondominio(toCondominio(c))).dump());
}

rust::String Session::update_condominio(CondominioDto c) {
  return rust::String(condominioToJson(api_.updateCondominio(toCondominio(c))).dump());
}

void Session::delete_condominio(rust::Str id) { api_.deleteCondominio(std::string(id)); }

rust::String Session::list_tipos_servico_json() {
  json arr = json::array();
  for (auto& t : api_.listTiposServico()) arr.push_back(tipoServicoToJson(t));
  return rust::String(arr.dump());
}

rust::String Session::create_tipo_servico(TipoServicoDto t) {
  return rust::String(tipoServicoToJson(api_.createTipoServico(toTipoServico(t))).dump());
}

rust::String Session::update_tipo_servico(TipoServicoDto t) {
  return rust::String(tipoServicoToJson(api_.updateTipoServico(toTipoServico(t))).dump());
}

void Session::delete_tipo_servico(rust::Str id) { api_.deleteTipoServico(std::string(id)); }

rust::String Session::list_servicos_condominio_json(rust::Str now_iso) {
  return rust::String(api_.listServicosCondominioJson(std::string(now_iso)));
}

rust::String Session::create_servico_condominio(rust::Str payload) {
  return rust::String(api_.createServicoCondominio(std::string(payload)));
}

rust::String Session::update_servico_condominio(rust::Str payload) {
  return rust::String(api_.updateServicoCondominio(std::string(payload)));
}

void Session::delete_servico_condominio(rust::Str id) { api_.deleteServicoCondominio(std::string(id)); }

rust::String Session::renovar_servico(rust::Str payload) {
  return rust::String(api_.renovarServico(std::string(payload)));
}

rust::String Session::list_renovacoes_json(rust::Str servico_condominio_filter) {
  return rust::String(api_.listRenovacoesJson(std::string(servico_condominio_filter)));
}

// ----------------------------- fornecedores e prestadores de serviços

rust::String Session::list_setorizacao_json() { return rust::String(api_.listSetorizacaoJson()); }

rust::String Session::create_especialidade(rust::Str payload) {
  return rust::String(api_.createEspecialidade(std::string(payload)));
}

rust::String Session::update_especialidade(rust::Str payload) {
  return rust::String(api_.updateEspecialidade(std::string(payload)));
}

void Session::delete_especialidade(rust::Str id) { api_.deleteEspecialidade(std::string(id)); }

rust::String Session::list_empresas_json() { return rust::String(api_.listEmpresasJson()); }

rust::String Session::list_parceiros_json() { return rust::String(api_.listParceirosJson()); }

rust::String Session::create_empresa(rust::Str payload) {
  return rust::String(api_.createEmpresa(std::string(payload)));
}

rust::String Session::update_empresa(rust::Str payload) {
  return rust::String(api_.updateEmpresa(std::string(payload)));
}

void Session::delete_empresa(rust::Str id) { api_.deleteEmpresa(std::string(id)); }

rust::String Session::list_gerentes_json() { return rust::String(api_.listGerentesJson()); }

rust::String Session::create_gerente(rust::Str payload) {
  return rust::String(api_.createGerente(std::string(payload)));
}

rust::String Session::update_gerente(rust::Str payload) {
  return rust::String(api_.updateGerente(std::string(payload)));
}

void Session::delete_gerente(rust::Str id) { api_.deleteGerente(std::string(id)); }

rust::String Session::list_servicos_json() { return rust::String(api_.listServicosJson()); }

rust::String Session::create_servico(rust::Str payload) {
  return rust::String(api_.createServico(std::string(payload)));
}

rust::String Session::update_servico(rust::Str payload) {
  return rust::String(api_.updateServico(std::string(payload)));
}

void Session::delete_servico(rust::Str id) { api_.deleteServico(std::string(id)); }

rust::String Session::list_fechamentos_json() { return rust::String(api_.listFechamentosJson()); }

rust::String Session::fechar_mes(rust::Str payload) {
  return rust::String(api_.fecharMes(std::string(payload)));
}

void Session::reabrir_fechamento(rust::Str id) { api_.reabrirFechamento(std::string(id)); }

rust::String Session::get_sos_config_json() { return rust::String(api_.getSosConfigJson()); }

void Session::set_sos_config(rust::Str payload) { api_.setSosConfig(std::string(payload)); }

rust::String Session::list_delta_sindicos_json() { return rust::String(api_.listDeltaSindicosJson()); }

rust::String Session::montar_dashboard(rust::Str payload) {
  return rust::String(api_.montarDashboard(std::string(payload)));
}

rust::String Session::salvar_dashboard(rust::Str payload) {
  return rust::String(api_.salvarDashboard(std::string(payload)));
}

rust::String Session::list_dashboards_json() { return rust::String(api_.listDashboardsJson()); }

rust::String Session::montar_pagamento_sos(rust::Str payload) {
  return rust::String(api_.montarPagamentoSos(std::string(payload)));
}

rust::String Session::salvar_pagamento_sos(rust::Str payload) {
  return rust::String(api_.salvarPagamentoSos(std::string(payload)));
}

rust::String Session::list_pagamentos_sos_json() { return rust::String(api_.listPagamentosSosJson()); }

rust::String Session::list_suprimentos_json() { return rust::String(api_.listSuprimentosJson()); }

rust::String Session::create_suprimento(rust::Str payload) {
  return rust::String(api_.createSuprimento(std::string(payload)));
}

rust::String Session::update_suprimento(rust::Str payload) {
  return rust::String(api_.updateSuprimento(std::string(payload)));
}

void Session::delete_suprimento(rust::Str id) { api_.deleteSuprimento(std::string(id)); }

rust::String Session::list_aquisicoes_json() { return rust::String(api_.listAquisicoesJson()); }

rust::String Session::create_aquisicao(rust::Str payload) {
  return rust::String(api_.createAquisicao(std::string(payload)));
}

rust::String Session::update_aquisicao(rust::Str payload) {
  return rust::String(api_.updateAquisicao(std::string(payload)));
}

void Session::delete_aquisicao(rust::Str id) { api_.deleteAquisicao(std::string(id)); }

rust::String Session::set_aquisicao_anexo(rust::Str aquisicao_id, rust::Str anexo_path, rust::Str anexo_tipo) {
  return rust::String(api_.setAquisicaoAnexo(std::string(aquisicao_id), std::string(anexo_path), std::string(anexo_tipo)));
}

rust::String Session::clear_aquisicao_anexo(rust::Str aquisicao_id) {
  return rust::String(api_.clearAquisicaoAnexo(std::string(aquisicao_id)));
}

rust::String Session::list_ordens_orcamento_json(rust::Str now_iso) {
  return rust::String(api_.listOrdensOrcamentoJson(std::string(now_iso)));
}

rust::String Session::create_ordem_orcamento(rust::Str payload) {
  return rust::String(api_.createOrdemOrcamento(std::string(payload)));
}

rust::String Session::update_ordem_orcamento_info(rust::Str payload) {
  return rust::String(api_.updateOrdemOrcamentoInfo(std::string(payload)));
}

void Session::delete_ordem_orcamento(rust::Str id) { api_.deleteOrdemOrcamento(std::string(id)); }

rust::String Session::solicitar_orcamento_para_empresas(rust::Str payload, rust::Str now_iso) {
  return rust::String(api_.solicitarOrcamentoParaEmpresas(std::string(payload), std::string(now_iso)));
}

rust::String Session::reenviar_solicitacao_proposta(rust::Str proposta_id, rust::Str now_iso) {
  return rust::String(api_.reenviarSolicitacaoProposta(std::string(proposta_id), std::string(now_iso)));
}

rust::String Session::set_proposta_valor(rust::Str proposta_id, double valor) {
  return rust::String(api_.setPropostaValor(std::string(proposta_id), valor));
}

rust::String Session::set_proposta_anexo(rust::Str proposta_id, rust::Str anexo_path, rust::Str anexo_tipo) {
  return rust::String(api_.setPropostaAnexo(std::string(proposta_id), std::string(anexo_path), std::string(anexo_tipo)));
}

rust::String Session::clear_proposta_anexo(rust::Str proposta_id) {
  return rust::String(api_.clearPropostaAnexo(std::string(proposta_id)));
}

rust::String Session::set_proposta_detalhes(rust::Str proposta_id, rust::Str escopo, rust::Str forma_pagamento,
                                            rust::Str validade) {
  return rust::String(api_.setPropostaDetalhes(std::string(proposta_id), std::string(escopo),
                                               std::string(forma_pagamento), std::string(validade)));
}

rust::String Session::marcar_proposta_recomendada(rust::Str ordem_id, rust::Str proposta_id) {
  return rust::String(api_.marcarPropostaRecomendada(std::string(ordem_id), std::string(proposta_id)));
}

rust::String Session::desmarcar_proposta_recomendada(rust::Str ordem_id) {
  return rust::String(api_.desmarcarPropostaRecomendada(std::string(ordem_id)));
}

rust::String Session::enviar_orcamento_para_cliente(rust::Str payload, rust::Str now_iso) {
  return rust::String(api_.enviarOrcamentoParaCliente(std::string(payload), std::string(now_iso)));
}

rust::String Session::aprovar_proposta_orcamento(rust::Str ordem_id, rust::Str proposta_id, rust::Str now_iso) {
  return rust::String(api_.aprovarPropostaOrcamento(std::string(ordem_id), std::string(proposta_id), std::string(now_iso)));
}

rust::String Session::reativar_ordem_orcamento(rust::Str ordem_id, rust::Str now_iso) {
  return rust::String(api_.reativarOrdemOrcamento(std::string(ordem_id), std::string(now_iso)));
}

rust::String Session::get_email_config_json() { return rust::String(api_.getEmailConfigJson()); }

void Session::set_email_config(rust::Str payload) { api_.setEmailConfig(std::string(payload)); }

rust::String Session::get_email_config_internal_json() {
  return rust::String(api_.getEmailConfigInternalJson());
}

rust::String Session::list_pagamentos_json() { return rust::String(api_.listPagamentosJson()); }

rust::String Session::create_pagamento(rust::Str payload) {
  return rust::String(api_.createPagamento(std::string(payload)));
}

rust::String Session::update_pagamento(rust::Str payload) {
  return rust::String(api_.updatePagamento(std::string(payload)));
}

void Session::delete_pagamento(rust::Str id) { api_.deletePagamento(std::string(id)); }

rust::String Session::marcar_parcela(rust::Str payload) {
  return rust::String(api_.marcarParcela(std::string(payload)));
}

std::unique_ptr<Session> open_session(rust::Str db_path) {
  return std::make_unique<Session>(std::string(db_path));
}

rust::String resolve_data_dir_default() {
  auto result = estoque::portable_paths::resolveDataDir();
  if (!result.ok) throw std::runtime_error(result.error);
  // path::string() no Windows converte para a codepage ANSI local, não
  // UTF-8 — caminhos com acento (ex.: "Área de Trabalho") viram bytes
  // inválidos e o rust::String do cxx rejeita com "data for rust::String
  // is not utf-8". u8string() sempre devolve UTF-8, em qualquer plataforma.
  return rust::String(result.dataDir.u8string());
}

}  // namespace estoque::shim
