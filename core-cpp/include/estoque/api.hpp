#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/auth_engine.hpp"
#include "estoque/db.hpp"
#include "estoque/inventory_engine.hpp"  // MovementPatch
#include "estoque/models.hpp"

// Fachada única do domínio: junta banco + inventory_engine + report_engine
// + auth_engine + request_engine + backup/restore num objeto com estado (a
// conexão SQLite e a SESSÃO do usuário logado). É o que a ponte cxx embrulha
// (ver bridge/cpp/shim.hpp) — mas esta classe em si não sabe que `cxx`
// existe, então continua 100% testável sozinha (doctest), com os mesmos
// tipos simples usados no resto do core-cpp.
//
// AUTORIZAÇÃO MORA AQUI, e não no frontend. Todo método público confere a
// permissão da sessão antes de fazer qualquer coisa: esconder o botão na tela
// é conveniência, não controle de acesso. O mapa de qual função/ação cada
// método exige está em api.cpp, junto de cada guarda.
namespace estoque {

class Api {
 public:
  explicit Api(const std::string& dbPath);

  // ------------------------------------------------------------- sessão
  // Devolve o JSON da sessão (usuário + matriz de permissões efetiva +
  // catálogo de funções). Lança AuthError se e-mail/senha não conferirem ou
  // a conta estiver inativa — sempre com a MESMA mensagem, para não revelar
  // quais e-mails existem.
  std::string login(const std::string& email, const std::string& password, const std::string& nowIso);
  void logout();
  // "null" quando não há ninguém logado (é o que leva o frontend à tela de
  // login), senão o mesmo JSON de login().
  std::string currentSessionJson();
  void changeOwnPassword(const std::string& currentPassword, const std::string& newPassword);

  // Sessão de serviço: acesso total SEM login, para uso fora da interface
  // (core-cli e testes). NÃO é exposta como comando Tauri em src-tauri —
  // a fronteira de confiança é a lista de comandos registrada lá, então o
  // frontend não tem como chamá-la. Existe porque o core-cli opera direto no
  // arquivo do banco (onde autenticação não agrega nada: quem tem o arquivo
  // já tem tudo) e exigir senha ali só criaria uma credencial embutida no
  // código.
  void loginAsService(const std::string& label);

  // --------------------------------------------------------- catálogo/UI
  std::string permissionCatalogJson();

  // ------------------------------------------------------------ produtos
  std::vector<Product> listProducts();
  Product createProduct(const Product& input);
  Product updateProduct(const Product& input);
  void deleteProduct(const std::string& id);

  // Posição de estoque com a reserva das requisições em aberto descontada.
  std::string stockAvailabilityJson();

  std::vector<Department> listDepartments();
  Department createDepartment(const Department& input);
  Department updateDepartment(const Department& input);
  void deleteDepartment(const std::string& id);

  Movement applyEntrada(const std::string& movementId, const std::string& productId, double qty,
                        double unitPrice, const std::string& supplier, const std::string& nf,
                        const std::string& date, const std::string& obs, const std::string& createdAt);
  Movement applySaida(const std::string& movementId, const std::string& productId, double qty,
                      const std::string& departmentId, const std::string& date, const std::string& obs,
                      const std::string& requester, const std::string& createdAt);
  Movement applyCorrecao(const std::string& movementId, const std::string& productId, double qtyReal,
                        const std::string& motivo, const std::string& date, const std::string& createdAt);

  std::vector<Movement> listMovements();
  Movement updateMovement(const MovementPatch& patch);
  void deleteMovement(const std::string& id);

  std::string computeReportJson(int year, int month0, const std::string& deptFilter, int windowMonths,
                                const std::string& nowIso);

  // Retrospecto anual (matriz departamento × mês, totais do ano, comparativo
  // e teto de gastos) — ver retrospect_engine.hpp.
  std::string computeRetrospectJson(int year, const std::string& source, const std::string& nowIso);
  void saveBudgetParams(double metaReducao, double ipca, double pisoMensal);
  int importDeptCostHistory(const std::string& payload);

  // ------------------------------------------------- usuários (superadmin)
  std::string listUsersJson();
  std::string createUser(const UserInput& input);
  std::string updateUser(const UserInput& input);
  void deleteUser(const std::string& id);
  void resetUserPassword(const std::string& id, const std::string& newPassword);

  // ----------------------------------------------- permissões (superadmin)
  // Um JSON só, com os grupos (cada um com sua matriz CRUD), o catálogo de
  // funções e os departamentos com o grupo vinculado a cada um — é tudo que a
  // tela de Permissões precisa, numa ida só.
  std::string listPermissionsJson();
  std::string createPermissionGroup(const std::string& payload);
  std::string updatePermissionGroup(const std::string& payload);
  void deletePermissionGroup(const std::string& id);
  void setDepartmentPermissionGroup(const std::string& departmentId, const std::string& groupId);

  // ---------------------------------------------------------- requisições
  // Superadmin (e quem tiver `requisicoes.update`) vê todas; os demais veem
  // só as do próprio departamento — é o "histórico de pedidos do seu setor".
  std::string listRequestsJson();
  std::string createRequest(const std::string& payload);
  std::string approveRequest(const std::string& id, const std::string& note, const std::string& nowIso);
  std::string rejectRequest(const std::string& id, const std::string& note, const std::string& nowIso);
  std::string cancelRequest(const std::string& id, const std::string& note, const std::string& nowIso);
  std::string deliverRequest(const std::string& id, const std::string& nowIso,
                             const std::string& movementIdPrefix);

  // -------------------------------------------------------- backup/restore
  // JSON no MESMO formato do antigo localStorage (products/movements/departments
  // com as mesmas chaves camelCase) — permite importar os backups já existentes
  // (ex.: carga_inicial_ref_jun26.json) sem nenhuma conversão. As chaves
  // `deptCostHistory`, `settings`, `users`, `permissionGroups`,
  // `departmentPermissionGroups` e `requests` são acréscimos posteriores:
  // backups antigos que não as tenham continuam válidos (ver restoreFromJson).
  //
  // ATENÇÃO: o arquivo passa a conter o hash das senhas dos usuários. Não é
  // senha em texto puro (é PBKDF2 com salt), mas é material sensível — o
  // backup deve ser tratado como confidencial.
  std::string backupJson();

  // Substitui TODOS os dados atuais pelo conteúdo do payload — mesma
  // semântica do antigo "Importar Backup" (state = data; sem reprocessar
  // pelas regras de negócio, preserva os valores exatamente como estavam
  // gravados: qty/avgCost/resultingQty/resultingAvgCost não são recalculados).
  void restoreFromJson(const std::string& payload);

 private:
  // Lança AuthError se não há ninguém logado. Toda guarda passa por aqui.
  const User& requireSession();
  void requireSuperadmin(const std::string& oQue);
  void require(const char* feature, PermAction action);
  // "Basta uma": para dados de referência que várias telas precisam (o
  // catálogo de produtos alimenta Produtos, Linha do Tempo e Requisições).
  void requireAny(const std::vector<std::pair<const char*, PermAction>>& options);

  Database db_;
  std::optional<User> currentUser_;
  bool serviceSession_ = false;
};

}  // namespace estoque
