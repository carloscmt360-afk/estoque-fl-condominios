#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/db.hpp"

// Autenticação e autorização.
//
// MODELO (é o pedido do cliente, e vale a pena deixar explícito porque foge
// do RBAC clássico "grupo -> usuário"):
//
//   grupo de permissão --atribuído a--> DEPARTAMENTO --atrelado a--> usuário
//
// A permissão nunca é dada a uma pessoa: ela é dada ao setor. O usuário herda
// a do setor em que está. Mover alguém de setor já muda tudo que ele enxerga,
// sem existir uma segunda lista para alguém esquecer de revisar. A única
// exceção é o papel `superadmin`, que ignora a matriz inteira (acesso total,
// inclusive à gestão de usuários e à validação de requisições).
//
// Cada par (grupo, função) guarda os quatro bits do CRUD. O que cada bit
// controla em cada função está documentado em kFeatureCatalog (auth_engine.cpp)
// e é o MESMO texto exibido na tela de Permissões — a tela não reescreve a
// regra por conta própria.
namespace estoque {

// ---- Papéis ----
constexpr const char* kRoleSuperadmin = "superadmin";
constexpr const char* kRoleUsuario = "usuario";

// ---- Funções (features) sujeitas a permissão ----
namespace features {
constexpr const char* kRelatorioMensal = "relatorio_mensal";
constexpr const char* kRetrospecto = "retrospecto";
constexpr const char* kProdutos = "produtos";
constexpr const char* kLinhaDoTempo = "linha_do_tempo";
constexpr const char* kDepartamentos = "departamentos";
constexpr const char* kImportarExportar = "importar_exportar";
constexpr const char* kRequisicoes = "requisicoes";
constexpr const char* kCondominios = "condominios";
constexpr const char* kGestaoDatas = "gestao_datas";
constexpr const char* kEmpresas = "empresas";
constexpr const char* kGerentes = "gerentes";
constexpr const char* kGestaoSosServicos = "gestao_sos_servicos";
constexpr const char* kSuprimentos = "suprimentos";
constexpr const char* kDeltaSindicos = "delta_sindicos";
constexpr const char* kAquisicoes = "aquisicoes";
constexpr const char* kOrcamentos = "orcamentos";
constexpr const char* kPagamentos = "pagamentos";
}  // namespace features

enum class PermAction { Create, Read, Update, Delete };
std::string permActionToString(PermAction a);

// Descrição de uma função para a tela de Permissões: o rótulo e o que cada
// letra do CRUD significa ali (string vazia = ação que não existe naquela
// função, e a tela desabilita a caixinha em vez de fingir que ela faz algo).
struct FeatureInfo {
  std::string key;
  std::string label;
  std::string createLabel;
  std::string readLabel;
  std::string updateLabel;
  std::string deleteLabel;
};
const std::vector<FeatureInfo>& featureCatalog();
bool isKnownFeature(const std::string& feature);

struct FeaturePermissions {
  std::string feature;
  bool create = false;
  bool read = false;
  bool update = false;
  bool del = false;

  bool has(PermAction a) const;
  void set(PermAction a, bool v);
};

struct PermissionGroup {
  std::string id;
  std::string name;
  std::string description;
  std::string createdAt;
  std::vector<FeaturePermissions> perms;  // sempre uma linha por função do catálogo
};

struct User {
  std::string id;
  std::string name;
  std::string email;
  std::string role;          // kRoleSuperadmin | kRoleUsuario
  std::string departmentId;  // vazio = sem setor (e portanto sem nenhuma permissão herdada)
  bool active = true;
  std::string createdAt;
  std::string lastLoginAt;

  bool isSuperadmin() const { return role == kRoleSuperadmin; }
};

// Entrada de criação/edição. `password` vazia num update significa "não mexer
// na senha" — nunca "apagar a senha".
struct UserInput {
  std::string id;
  std::string name;
  std::string email;
  std::string role;
  std::string departmentId;
  bool active = true;
  std::string createdAt;
  std::string password;
};

// Falta de sessão ou credencial inválida (o frontend derruba para a tela de
// login quando vê isso).
class AuthError : public std::runtime_error {
 public:
  explicit AuthError(const std::string& msg) : std::runtime_error(msg) {}
};

// Sessão válida, mas sem direito à operação pedida.
class ForbiddenError : public std::runtime_error {
 public:
  explicit ForbiddenError(const std::string& msg) : std::runtime_error(msg) {}
};

// ---- Semeadura ----
// Cria o superadmin de fábrica (carlos.matos@flcondominios.com.br) se — e só
// se — não existir NENHUM superadmin ativo no banco. Idempotente e não
// destrutiva: nunca redefine a senha de um superadmin já existente, então
// trocar a senha no app não é desfeito no próximo boot.
void ensureDefaultSuperadmin(Database& db, const std::string& nowIso);
constexpr const char* kDefaultSuperadminEmail = "carlos.matos@flcondominios.com.br";

// ---- Usuários ----
std::vector<User> listUsers(Database& db);
std::optional<User> findUser(Database& db, const std::string& id);
std::optional<User> findUserByEmail(Database& db, const std::string& email);
User createUser(Database& db, const UserInput& input);
// Não altera a senha (use setUserPassword). Impede rebaixar/desativar o
// último superadmin ativo — senão o app ficaria sem ninguém que administra.
User updateUser(Database& db, const UserInput& input);
void deleteUser(Database& db, const std::string& id);
void setUserPassword(Database& db, const std::string& id, const std::string& password);

// Devolve o usuário se e-mail + senha conferirem E a conta estiver ativa;
// std::nullopt em qualquer outro caso (o chamador nunca deve dizer ao usuário
// QUAL das duas coisas falhou). Grava last_login_at no sucesso.
std::optional<User> authenticate(Database& db, const std::string& email, const std::string& password,
                                 const std::string& nowIso);
bool checkUserPassword(Database& db, const std::string& id, const std::string& password);

// Lança std::invalid_argument com a exigência não cumprida.
void validatePassword(const std::string& password);

// ---- Grupos de permissão ----
std::vector<PermissionGroup> listPermissionGroups(Database& db);
std::optional<PermissionGroup> findPermissionGroup(Database& db, const std::string& id);
PermissionGroup createPermissionGroup(Database& db, const PermissionGroup& input);
PermissionGroup updatePermissionGroup(Database& db, const PermissionGroup& input);
// Departamentos que apontavam para o grupo ficam sem grupo (e portanto sem
// permissão herdada) — nunca com uma permissão órfã pendurada.
void deletePermissionGroup(Database& db, const std::string& id);
void setDepartmentPermissionGroup(Database& db, const std::string& departmentId,
                                  const std::string& groupId);
std::string departmentPermissionGroupId(Database& db, const std::string& departmentId);

// ---- Resolução ----
// Matriz efetiva do usuário: tudo ligado para superadmin; a matriz do grupo do
// departamento dele para os demais; tudo desligado se ele não tem setor ou o
// setor não tem grupo.
std::vector<FeaturePermissions> effectivePermissions(Database& db, const User& user);
bool userCan(Database& db, const User& user, const std::string& feature, PermAction action);

}  // namespace estoque
