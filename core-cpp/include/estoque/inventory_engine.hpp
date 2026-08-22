#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/db.hpp"
#include "estoque/models.hpp"

// Motor de CRUD + custo médio ponderado móvel. Nenhuma função aqui toca
// relógio de parede ou gera aleatoriedade: id e timestamp de criação são
// sempre passados pelo chamador (a ponte cxx os gera do lado Rust) — isso
// mantém o núcleo determinístico e barato de testar.
namespace estoque {

// ---- Produtos ----
// `input` já deve trazer id/createdAt preenchidos pelo chamador; qty/avgCost
// são sempre forçados a 0 na criação (entram por uma entrada explícita).
// O SKU (input.sku, se vier preenchido) é IGNORADO — sempre gerado pelo
// servidor no formato CMT###### (ver nextSku em inventory_engine.cpp).
// Lança std::invalid_argument se input.category não for uma das seis
// categorias válidas (Papelaria/Informática/Assembleia/Gráfica/Brinde/Valor).
Product createProduct(Database& db, const Product& input);
// Atualiza name/unit/minStock/category; nunca mexe em qty/avgCost (esses só
// mudam via applyEntrada/applySaida/applyCorrecao) nem em sku (permanente
// por definição — ver o comentário de Product::sku em models.hpp). Mesma
// validação de categoria de createProduct — inclusive para tirar um item de
// "Não Classificado": só sai desse estado escolhendo uma categoria válida.
Product updateProduct(Database& db, const Product& input);
// Grava/limpa só image_path/thumbnail_path — nunca mexe em SKU, categoria,
// saldo ou histórico. imagePath/thumbnailPath já devem vir prontos
// (caminhos relativos gerados pelo módulo Rust de imagens); este par nunca
// decodifica nem grava bytes de imagem, só a referência. Lança
// NotFoundError se productId não existir.
Product setProductImage(Database& db, const std::string& productId, const std::string& imagePath,
                         const std::string& thumbnailPath);
Product clearProductImage(Database& db, const std::string& productId);
void deleteProduct(Database& db, const std::string& id);  // remove também as movimentações do produto
std::vector<Product> listProducts(Database& db);
// Preenche o SKU de produtos que ainda não têm um (dados legados/backup
// restaurado sem SKU) — em ordem de criação, sem alterar quem já tem.
void backfillMissingSkus(Database& db);
// Reclassifica produtos cuja categoria não é uma das seis válidas (dado
// legado/restaurado) por palavra-chave no nome, ou "Não Classificado" se
// não conseguir — nunca toca em quem já está numa categoria válida. Roda
// uma única vez (flag em app_settings); devolve quantos produtos mexeu.
int classifyLegacyCategories(Database& db);
std::optional<Product> findProduct(Database& db, const std::string& id);

// ---- Departamentos ----
Department createDepartment(Database& db, const Department& input);
Department updateDepartment(Database& db, const Department& input);
void deleteDepartment(Database& db, const std::string& id);
std::vector<Department> listDepartments(Database& db);
std::optional<Department> findDepartment(Database& db, const std::string& id);

// ---- Lançamentos ----
// Cada uma persiste o produto atualizado + insere a movimentação numa única
// transação (commit só no fim; qualquer exceção reverte as duas). Lança
// NotFoundError se productId/departmentId não existir.
//
// Todas reprocessam o produto ao final (ver recomputeProduct): a posição
// resultante nunca depende da ORDEM DE DIGITAÇÃO, só da ordem cronológica —
// um lançamento retroativo entra no lugar certo da linha do tempo e os
// lançamentos posteriores são revalorizados.

Movement applyEntrada(Database& db, const std::string& movementId, const std::string& productId,
                       double qty, double unitPrice, const std::string& supplier, const std::string& nf,
                       const std::string& date, const std::string& obs, const std::string& createdAt);

Movement applySaida(Database& db, const std::string& movementId, const std::string& productId, double qty,
                     const std::string& departmentId, const std::string& date, const std::string& obs,
                     const std::string& requester, const std::string& createdAt);

// `qtyReal` é a quantidade CONTADA fisicamente; o que se grava em
// movements.qty é o delta contra o saldo vigente NA DATA do ajuste (não
// contra o saldo de hoje) — é isso que faz um ajuste retroativo continuar
// coerente depois que outro lançamento é editado antes dele.
//
// `newAvgCost` (opcional, default 0 = não altera) corrige o CUSTO MÉDIO —
// diferente de qtyReal, que corrige o SALDO. Os dois podem vir juntos (uma
// contagem física às vezes revela os dois errados) ou só um dos dois
// (qtyReal = saldo atual do produto → delta zero, só o custo muda).
Movement applyCorrecao(Database& db, const std::string& movementId, const std::string& productId,
                        double qtyReal, const std::string& motivo, const std::string& date,
                        const std::string& createdAt, double newAvgCost = 0);

// ---- Consulta de lançamentos ----
std::vector<Movement> listMovements(Database& db);  // ordem cronológica (date, created_at)
std::optional<Movement> findMovement(Database& db, const std::string& id);

// ---- Edição e exclusão de lançamentos ----

// Campos editáveis de um lançamento já gravado. O TIPO e o PRODUTO são
// imutáveis de propósito: entrada/saída/ajuste carregam campos e semântica
// diferentes (uma entrada tem fornecedor e NF e forma o custo médio; uma
// saída tem departamento e o consome), e trocar o produto exigiria
// reprocessar dois produtos para desfazer/refazer efeitos financeiros em
// cadeia. Para esses casos: exclua o lançamento e registre outro.
struct MovementPatch {
  std::string id;
  double qty = 0;        // entrada/saída: quantidade movimentada (> 0)
  double qtyReal = 0;    // ajuste: quantidade CONTADA (o delta é derivado dela)
  double unitPrice = 0;  // entrada: preço de compra · saída: preço de valoração gravado
  std::string supplier;  // entrada
  std::string nf;        // entrada
  std::string departmentId;  // saída (redenormaliza recipient/encarregado)
  std::string requester;     // saída
  std::string obs;           // todos — no ajuste, é o motivo
  std::string date;
};

// Lança NotFoundError (lançamento/departamento inexistente) ou
// std::invalid_argument (quantidade não positiva em entrada/saída).
Movement updateMovement(Database& db, const MovementPatch& patch);

void deleteMovement(Database& db, const std::string& id);

// Reprocessa a linha do tempo INTEIRA de um produto em ordem cronológica,
// regravando resulting_qty/resulting_avg_cost de cada lançamento e a posição
// do produto. Mesma aritmética do report_engine, para que os dois nunca
// divirjam sobre os mesmos lançamentos.
//
// `offsetQty` preserva a diferença que JÁ EXISTIA entre o saldo gravado no
// produto e o que o razão reproduz — nos dados reais do usuário 13 de 157
// produtos divergem (a planilha de origem tem saídas sem a entrada
// correspondente), somando R$ 8,2 mil. Sem esse offset, editar a observação
// de um lançamento qualquer de PILHA AA saltaria o estoque dela de 21 para
// 101 unidades. Com ele, uma edição move o saldo exatamente pelo efeito da
// edição — nada mais. Os chamadores capturam o offset ANTES de mexer no
// razão (ver ledgerOffset, no .cpp).
//
// `previousQty` é o saldo do produto ANTES desta operação (o mesmo `product`
// que os chamadores já buscaram para calcular offsetQty) — usado só pela
// trava de saldo negativo: uma operação não pode fazer o saldo terminar mais
// negativo do que já estava. Produtos já negativos (dado legado) não ficam
// travados para sempre; ver repairNegativeBalances.
void recomputeProduct(Database& db, const std::string& productId, double offsetQty, double previousQty);

// Corrige, uma única vez, produtos com saldo negativo herdado de antes da
// trava de saldo negativo existir (ver o comentário em recomputeProduct no
// .cpp) — zera o saldo deles via um ajuste auditável. Idempotente: chamadas
// depois da primeira são no-op (flag em app_settings). Devolve os IDs dos
// produtos corrigidos nesta chamada (vazio se já tinha rodado antes).
std::vector<std::string> repairNegativeBalances(Database& db, const std::string& nowIso);

// Reconcilia products.avg_cost com o que o razão de movimentações reproduz
// (ver o comentário longo no .cpp) — sem isso, um produto vindo de um backup
// restaurado pode mostrar um custo na tela de Produtos e OUTRO no Relatório/
// Retrospecto, porque só esses últimos recalculam pelo razão. Ao contrário de
// repairNegativeBalances, não é uma correção "uma vez só": é barata de rodar
// sempre (só paga o replay completo do produto quando ele já diverge) e
// idempotente, então roda em toda abertura de sessão e ao fim de todo
// restoreFromJson. Nunca sobrescreve com um custo zerado (razão sem nenhuma
// entrada/ajuste que estabeleça um custo) — aí prefere manter o que já
// estava, a apagar um preço porventura real vindo da origem. Devolve os IDs
// dos produtos corrigidos nesta chamada.
std::vector<std::string> reconcileAvgCost(Database& db);

}  // namespace estoque
