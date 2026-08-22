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
  // Preenchido quando alguém (normalmente quem valida) corrige os itens em
  // nome do solicitante — corrige quantidade errada ou acrescenta um material
  // que foi pedido fora do sistema e esquecido na hora de registrar.
  std::string editedAt;
  std::string editedByUserId;
  std::string editedByName;
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
  std::string sku;  // usado pela lista de separação impressa, para achar o item na prateleira
  std::string name;
  std::string unit;
  std::string category;
  std::string imagePath;      // foto principal — clique-para-ampliar na tela de requisições
  std::string thumbnailPath;  // miniatura — mostrada junto ao material nas listas de pedido
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

// Corrige os itens de uma requisição AINDA ABERTA (pendente ou aprovada) —
// substitui a lista inteira, mesmo padrão de createRequest: o chamador manda
// o conjunto final (produto + quantidade), não um diff. Serve para corrigir
// uma quantidade digitada errada ou acrescentar algo que o solicitante pediu
// fora do sistema e esqueceu de registrar.
//
// Recusa se a requisição já foi entregue/rejeitada/cancelada: nesses estados
// os itens são histórico (a entrega já gerou saída de estoque), não mais um
// rascunho editável. A validação de saldo desconta a PRÓPRIA reserva atual
// desta requisição do disponível — trocar de 2 para 3 unidades de um item já
// reservado não pode falhar por "já está reservado" contra si mesmo.
Request updateRequestItems(Database& db, const std::string& id, const std::vector<RequestItem>& items,
                           const User& actor, const std::string& nowIso);

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

// ------------------------------------------------------- janela de pedidos
//
// Fora de uma janela aberta, o sistema NÃO aceita requisição nova. "Aberta"
// nunca é um estado gravado numa coluna (não existiria quem virasse a chave
// no minuto exato do fim): é sempre calculado contra o instante da pergunta —
// opensAt <= agora < closesAt, e sem fechamento antecipado.
//
// Encerrada uma janela, o sistema volta a recusar pedidos sozinho, até que
// alguém abra a próxima. Um banco sem nenhuma janela cadastrada está FECHADO
// (é o estado de fábrica: pedir exige alguém ter aberto o período antes).
struct RequestWindow {
  std::string id;
  std::string opensAt;
  std::string closesAt;
  std::string obs;
  std::string createdAt;
  std::string createdByUserId;
  std::string createdByName;
  // Fechamento antecipado à mão: a janela acaba aqui em vez de em closesAt.
  // Vazio = seguiu até o fim programado (ou ainda vai seguir).
  std::string closedAt;
  std::string closedByUserId;
  std::string closedByName;
};

// Mais recente primeiro (ordenado por opensAt decrescente).
std::vector<RequestWindow> listRequestWindows(Database& db);
std::optional<RequestWindow> findRequestWindow(Database& db, const std::string& id);

// A janela valendo AGORA, se houver. `nowIso` sempre vem de fora (o núcleo
// não lê relógio); quem chama de verdade é a Api, que passa a hora do sistema.
std::optional<RequestWindow> openRequestWindowAt(Database& db, const std::string& nowIso);
// A próxima janela que ainda vai abrir — o que a tela mostra para quem
// chegou fora do prazo ("reabre em ...").
std::optional<RequestWindow> nextRequestWindowAfter(Database& db, const std::string& nowIso);

// Lança se não houver janela aberta no instante dado. É a trava de verdade:
// mora no núcleo, então esconder o botão no frontend é só conveniência.
void requireOpenRequestWindow(Database& db, const std::string& nowIso);

// Recusa período invertido/vazio, janela que já nasceria encerrada e
// sobreposição com outra janela ainda não encerrada — duas janelas valendo ao
// mesmo tempo tornariam "a janela aberta" uma pergunta sem resposta única.
RequestWindow createRequestWindow(Database& db, const RequestWindow& input, const std::string& nowIso);

// Encerra agora, antes do fim programado. Só faz sentido numa janela que
// ainda não terminou.
RequestWindow closeRequestWindowNow(Database& db, const std::string& id, const User& actor,
                                    const std::string& nowIso);

// Só para desfazer engano em janela que ainda não começou: apagar uma janela
// que já valeu reescreveria a história de por que os pedidos daquele período
// foram aceitos.
void deleteRequestWindow(Database& db, const std::string& id, const std::string& nowIso);

}  // namespace estoque
