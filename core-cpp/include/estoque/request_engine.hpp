#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/auth_engine.hpp"
#include "estoque/db.hpp"

// Requisições de material: o caminho pedido → validação → entrega.
//
// CICLO DE VIDA
//
//   pendente ──aprovar──> aprovado ──confirmar entrega──> entregue
//      │                     │
//      ├──rejeitar──> rejeitado
//      └──cancelar──> cancelado <──cancelar──┘
//
// RESERVA (o ponto central do pedido do cliente): enquanto a requisição está
// `pendente` ou `aprovado`, os itens ficam RESERVADOS. Reserva NÃO é baixa: o
// saldo do produto continua intacto (o relatório mensal, o retrospecto e o
// custo médio não enxergam nada), mas o DISPONÍVEL para novas requisições
// desconta o que já está reservado. Duas pessoas não conseguem pedir o mesmo
// último galão.
//
// Só a confirmação de entrega debita o estoque, e ela o faz gerando uma saída
// de verdade por item (applySaida) — não um UPDATE direto em products. É o que
// mantém requisição, linha do tempo, custo médio e relatórios contando a mesma
// história. O id da saída gerada fica gravado no item (movement_id).
namespace estoque {

namespace request_status {
constexpr const char* kPendente = "pendente";
constexpr const char* kAprovado = "aprovado";
constexpr const char* kEntregue = "entregue";
constexpr const char* kRejeitado = "rejeitado";
constexpr const char* kCancelado = "cancelado";
}  // namespace request_status

struct RequestItem {
  std::string id;
  std::string requestId;
  std::string productId;
  std::string productName;  // denormalizado no pedido (histórico não muda se renomearem o produto)
  std::string unit;
  double qty = 0;
  std::string movementId;  // vazio até a entrega
};

struct Request {
  std::string id;
  std::string departmentId;
  std::string departmentName;
  std::string requesterUserId;
  std::string requesterName;
  std::string status;
  std::string obs;
  std::string createdAt;  // data/hora da solicitação
  std::string decidedAt;
  std::string decidedByUserId;
  std::string decidedByName;
  std::string decisionNote;
  std::string deliveredAt;
  std::string deliveredByUserId;
  std::string deliveredByName;
  std::vector<RequestItem> items;
};

// Entrada de criação. id/createdAt vêm do chamador (mesma regra determinística
// do resto do núcleo); o departamento é resolvido pelo chamador a partir da
// sessão, nunca escolhido livremente por um usuário comum.
struct RequestInput {
  std::string id;
  std::string departmentId;
  std::string requesterUserId;
  std::string requesterName;
  std::string obs;
  std::string createdAt;
  std::vector<RequestItem> items;  // usa id, productId e qty
};

// Posição de estoque com a reserva descontada — é o que a tela de requisições
// mostra e o que a validação de uma nova requisição usa.
struct StockAvailability {
  std::string productId;
  std::string name;
  std::string unit;
  std::string category;
  double qty = 0;        // saldo real (o mesmo de products.qty)
  double reserved = 0;   // somatório dos itens em requisições pendentes/aprovadas
  double available = 0;  // qty - reserved (nunca negativo na exibição)
  double avgCost = 0;
  double minStock = 0;
};

std::vector<StockAvailability> stockAvailability(Database& db);
double reservedQty(Database& db, const std::string& productId);

// `departmentFilter` vazio = todas as requisições (visão do superadmin);
// preenchido = só as do departamento (visão do usuário comum, que é o
// "histórico de pedidos do seu departamento").
std::vector<Request> listRequests(Database& db, const std::string& departmentFilter);
std::optional<Request> findRequest(Database& db, const std::string& id);

// Recusa item com quantidade não positiva, produto inexistente e quantidade
// acima do DISPONÍVEL (saldo menos o que já está reservado).
Request createRequest(Database& db, const RequestInput& input);

Request approveRequest(Database& db, const std::string& id, const User& actor, const std::string& note,
                       const std::string& nowIso);
Request rejectRequest(Database& db, const std::string& id, const User& actor, const std::string& note,
                      const std::string& nowIso);
// Cancelamento só faz sentido enquanto o pedido está em aberto (pendente ou
// aprovado) — depois de entregue existe uma saída no estoque, e desfazer isso
// é assunto da Linha do Tempo, não daqui.
Request cancelRequest(Database& db, const std::string& id, const User& actor, const std::string& note,
                      const std::string& nowIso);

// Confirma a entrega: gera uma saída por item (debitando o estoque de fato) e
// fecha a requisição. Tudo numa transação só — se um item falhar, nenhum é
// baixado e o pedido continua aprovado.
//
// `movementIdPrefix` é o prefixo dos ids das saídas geradas (o chamador manda,
// pelo mesmo motivo dos outros ids: o núcleo não sorteia nada). Os ids saem
// como <prefixo>_1, <prefixo>_2, ...
Request deliverRequest(Database& db, const std::string& id, const User& actor, const std::string& nowIso,
                       const std::string& movementIdPrefix);

}  // namespace estoque
