#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/auth_engine.hpp"
#include "estoque/companies_engine.hpp"
#include "estoque/dates_engine.hpp"
#include "estoque/db.hpp"
#include "estoque/commissions_engine.hpp"
#include "estoque/inventory_engine.hpp"  // MovementPatch
#include "estoque/managers_engine.hpp"
#include "estoque/models.hpp"
#include "estoque/purchases_engine.hpp"
#include "estoque/suprimentos_engine.hpp"

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

  // Logo da FL: ativo global (fora do banco, ver bridge/src/images.rs),
  // então o comando Tauri que grava/apaga o arquivo não passa pela sessão
  // C++ como as outras operações — chama isto antes só pra confirmar que
  // quem está logado pode trocar a logo. Lança se não houver sessão ou se
  // quem está logado não for superadministrador; não lança nada (silêncio =
  // liberado) quando pode.
  void assertPodeEditarLogoFl();

  // ------------------------------------------------------------ produtos
  std::vector<Product> listProducts();
  Product createProduct(const Product& input);
  Product updateProduct(const Product& input);
  Product setProductImage(const std::string& productId, const std::string& imagePath,
                          const std::string& thumbnailPath);
  Product clearProductImage(const std::string& productId);
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
                        const std::string& motivo, const std::string& date, const std::string& createdAt,
                        double newAvgCost = 0);

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
  // Substitui os itens de uma requisição ainda aberta — só quem valida
  // requisições pode usar (corrige quantidade errada, acrescenta item
  // esquecido). `payload` é `{"items":[{"id","productId","qty"}, ...]}`.
  std::string updateRequestItems(const std::string& id, const std::string& payload, const std::string& nowIso);
  std::string approveRequest(const std::string& id, const std::string& note, const std::string& nowIso);
  std::string rejectRequest(const std::string& id, const std::string& note, const std::string& nowIso);
  std::string cancelRequest(const std::string& id, const std::string& note, const std::string& nowIso);
  std::string deliverRequest(const std::string& id, const std::string& nowIso,
                             const std::string& movementIdPrefix);

  // ------------------------------------------------- janela de requisições
  // Quem valida requisições (`requisicoes.update`) é quem abre e encerra o
  // período em que o sistema aceita pedidos; todo mundo pode CONSULTAR o
  // status, porque é ele que explica a tela ("fechado, reabre em ...").
  //
  // O "agora" destes métodos é sempre a hora do sistema, nunca uma data vinda
  // do frontend: um prazo que o próprio pedido informa não é prazo nenhum.
  std::string requestWindowStatusJson();
  std::string listRequestWindowsJson();
  std::string createRequestWindow(const std::string& payload);
  std::string closeRequestWindowNow(const std::string& id);
  void deleteRequestWindow(const std::string& id);

  // ------------------------------------------------------ gestão de prazos
  // Condomínios: cadastro de referência. É lido tanto pela tela "Cadastro de
  // Condomínios" quanto pela "Gestão de Prazos" (para o seletor de vínculo) —
  // mesmo critério de requireAny já usado em listProducts/listDepartments.
  std::vector<Condominio> listCondominios();
  Condominio createCondominio(const Condominio& input);
  Condominio updateCondominio(const Condominio& input);
  void deleteCondominio(const std::string& id);

  // Tipos de serviço: catálogo (nome, prazo em dias, cor de identificação).
  std::vector<TipoServico> listTiposServico();
  TipoServico createTipoServico(const TipoServico& input);
  TipoServico updateTipoServico(const TipoServico& input);
  void deleteTipoServico(const std::string& id);

  // Vínculos (condomínio × tipo de serviço), já com vencimento e status
  // (em dia/atenção/vencido) calculados a partir de `nowIso` — um JSON só,
  // para a tela de Gestão de Prazos não precisar de três idas.
  std::string listServicosCondominioJson(const std::string& nowIso);
  std::string createServicoCondominio(const std::string& payload);
  std::string updateServicoCondominio(const std::string& payload);
  void deleteServicoCondominio(const std::string& id);

  // Registra uma renovação (novo histórico) e atualiza a data do vínculo.
  std::string renovarServico(const std::string& payload);

  // filtro vazio = todo o histórico; preenchido = só as renovações de um
  // vínculo específico.
  std::string listRenovacoesJson(const std::string& servicoCondominioFilter);

  // ------------------------------- fornecedores e prestadores de serviços
  // Um JSON só com os quatro setores (catálogo fixo) e as especialidades de
  // cada um — é tudo que a tela de Setorização precisa, numa ida.
  std::string listSetorizacaoJson();
  std::string createEspecialidade(const std::string& payload);
  std::string updateEspecialidade(const std::string& payload);
  void deleteEspecialidade(const std::string& id);

  // Empresas já com as especialidades resolvidas (setor + nome de cada uma),
  // porque as três telas do módulo mostram isso e nenhuma faz join.
  std::string listEmpresasJson();
  // Só as parceiras (o Sim da ficha) — é a lista de Gestão SOS > Parceiros.
  // A filtragem é feita no SQL, e não na tela: assim quem abre Parceiros não
  // recebe o cadastro inteiro de empresas pela ponte só para descartar a maior
  // parte dele no JavaScript.
  std::string listParceirosJson();
  std::string createEmpresa(const std::string& payload);
  std::string updateEmpresa(const std::string& payload);
  void deleteEmpresa(const std::string& id);

  // ------------------------------------------ gestão sos: gerentes e carteiras
  // Gerente já com os condomínios da carteira resolvidos (id + nome), porque
  // tanto a tela de Gerentes quanto a de Carteiras mostram isso sem join.
  std::string listGerentesJson();
  std::string createGerente(const std::string& payload);
  std::string updateGerente(const std::string& payload);
  void deleteGerente(const std::string& id);

  // --------------------------------- gestão sos: serviços e fechamentos
  // Serviço já com o valor de comissão calculado (venda × porcentagem ÷ 100)
  // — nunca gravado, ver commissions_engine.hpp.
  std::string listServicosJson();
  std::string createServico(const std::string& payload);
  std::string updateServico(const std::string& payload);
  void deleteServico(const std::string& id);

  std::string listFechamentosJson();
  // payload: {mesReferencia, observacoes}
  std::string fecharMes(const std::string& payload);
  void reabrirFechamento(const std::string& id);

  // Configurações do módulo (hoje só a porcentagem padrão de comissão).
  std::string getSosConfigJson();
  void setSosConfig(const std::string& payload);

  // ------------------------------------------ gestão sos: delta síndicos
  // Puxado automaticamente de Serviços (condomínios com deltaSindica=true) —
  // não existe mais criar/editar/excluir lançamento aqui (ver
  // commissions_engine.hpp). Comissão vem calculada (venda × porcentagem ÷
  // 100), nunca gravada, mesmo critério dos serviços.
  std::string listDeltaSindicosJson();

  // ------------------------------- gestão sos: dashboard de fechamento
  // Monta o dashboard do mês (não grava). payload: a DashboardEntrada —
  // mês + os campos que o usuário informa (ver commissions_engine.hpp).
  std::string montarDashboard(const std::string& payload);
  // Grava o retrato como foi apresentado. Nunca substitui: gerar de novo o
  // mesmo mês acrescenta uma entrada ao histórico.
  std::string salvarDashboard(const std::string& payload);
  std::string listDashboardsJson();

  // ------------------------------------------------- gestão sos: pagamentos
  // Monta a PROPOSTA de pagamento do mês (não grava): pega o Dashboard de
  // Fechamento já salvo daquele mês e resolve quem recebe (Gerentes,
  // Suprimentos por categoria, Delta) com a Chave PIX de cada um. Se o mês
  // já tem um pagamento salvo, devolve ELE (pra reabrir em modo edição), não
  // uma proposta nova — payload: {"mesReferencia": "YYYY-MM"}.
  std::string montarPagamentoSos(const std::string& payload);
  // Fecha OU corrige a lista (mesmo registro do mês — nunca duplica, ver
  // PagamentoSalvo em commissions_engine.hpp).
  std::string salvarPagamentoSos(const std::string& payload);
  std::string listPagamentosSosJson();

  // --------------------------------------------- gestão sos: suprimentos
  std::string listSuprimentosJson();
  std::string createSuprimento(const std::string& payload);
  std::string updateSuprimento(const std::string& payload);
  void deleteSuprimento(const std::string& id);

  // --------------------------------------------------------------- compras
  // Aquisições FL: compra geral, ligada a um fornecedor cadastrado.
  std::string listAquisicoesJson();
  std::string createAquisicao(const std::string& payload);
  std::string updateAquisicao(const std::string& payload);
  void deleteAquisicao(const std::string& id);
  // anexoPath/anexoTipo já vêm prontos (arquivo já salvo em disco pela ponte
  // Rust — ver bridge/src/attachments.rs) — mesmo critério de
  // setProductImage/clearProductImage.
  std::string setAquisicaoAnexo(const std::string& aquisicaoId, const std::string& anexoPath,
                                const std::string& anexoTipo);
  std::string clearAquisicaoAnexo(const std::string& aquisicaoId);

  // Orçamentos: fluxo de cotação (ordem 1:N propostas) — ver
  // purchases_engine.hpp para o modelo. Os métodos que disparam e-mail
  // (solicitar/enviarParaCliente/aprovar) NÃO enviam nada sozinhos: o C++
  // não tem acesso a rede — devolvem o JSON da ordem atualizada MAIS um
  // array "emails" (to/subject/bodyHtml/attachmentPaths) já composto, que o
  // comando Tauri (src-tauri/src/commands.rs) itera chamando
  // bridge::mailer para de fato enviar, com as credenciais de
  // getEmailConfigJson().
  std::string listOrdensOrcamentoJson(const std::string& nowIso);
  std::string createOrdemOrcamento(const std::string& payload);
  std::string updateOrdemOrcamentoInfo(const std::string& payload);
  void deleteOrdemOrcamento(const std::string& id);
  // payload: {ordemId, empresas:[{empresaId,empresaNome}]}
  std::string solicitarOrcamentoParaEmpresas(const std::string& payload, const std::string& nowIso);
  // Reenvia a solicitação (mesmo texto de composeSolicitacaoBody) para UMA
  // empresa já solicitada — devolve a ORDEM inteira + "emails" com 1 item,
  // mesmo padrão dos outros três que disparam e-mail.
  std::string reenviarSolicitacaoProposta(const std::string& propostaId, const std::string& nowIso);
  std::string setPropostaValor(const std::string& propostaId, double valor);
  std::string setPropostaAnexo(const std::string& propostaId, const std::string& anexoPath,
                               const std::string& anexoTipo);
  std::string clearPropostaAnexo(const std::string& propostaId);
  std::string marcarPropostaRecomendada(const std::string& ordemId, const std::string& propostaId);
  std::string desmarcarPropostaRecomendada(const std::string& ordemId);
  // payload: {ordemId, destinatarioEmail (opcional — sobrepõe o e-mail do
  // condomínio), mensagemExtra (opcional)}
  std::string enviarOrcamentoParaCliente(const std::string& payload, const std::string& nowIso);
  std::string aprovarPropostaOrcamento(const std::string& ordemId, const std::string& propostaId,
                                       const std::string& nowIso);
  std::string reativarOrdemOrcamento(const std::string& ordemId, const std::string& nowIso);

  // Configuração de SMTP (Configurações > E-mail) — só superadministrador
  // (credencial sensível, sistema inteiro, mesmo critério de
  // assertPodeEditarLogoFl). Senha nunca volta em getEmailConfigJson (só
  // "temSenha": bool). setEmailConfig só grava a chave `password` quando o
  // payload a CONTÉM (mesmo critério de gravarSePresente em setSosConfig) —
  // reenviar o formulário sem tocar no campo senha não apaga a já salva.
  std::string getEmailConfigJson();
  void setEmailConfig(const std::string& payload);
  // Mesmos campos de getEmailConfigJson, mas com a senha DE VERDADE —
  // exclusivo de uso interno do comando Tauri que monta o SmtpConfig antes
  // de mandar pro bridge::mailer (ver src-tauri/src/commands.rs); nunca
  // volta pro frontend (não tem wrapper em frontend/js/api.js).
  std::string getEmailConfigInternalJson();

  // Acompanhamento de pagamentos: uma NF (ligada a uma Aquisição) e as
  // parcelas dela.
  std::string listPagamentosJson();
  std::string createPagamento(const std::string& payload);
  std::string updatePagamento(const std::string& payload);
  void deletePagamento(const std::string& id);
  // payload: {pagamentoId, parcelaId, pago, dataPagamento}
  std::string marcarParcela(const std::string& payload);

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
