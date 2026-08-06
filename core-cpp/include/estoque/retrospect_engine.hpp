#pragma once
#include <string>
#include <vector>

#include "estoque/db.hpp"

// Retrospecto anual: custo mensal por departamento, totais do ano e teto de
// gastos — a reprodução, dentro do app, da aba RESTROSPECTO da planilha do
// usuário.
//
// Por que existe uma tabela `dept_cost_history` em vez de simplesmente somar
// as movimentações: a planilha tem DUAS fontes distintas para a mesma
// pergunta ("quanto cada setor gastou em cada mês"). A aba CUSTO POR DEPTO
// valoriza cada retirada pelo preço histórico digitado na linha; o razão
// ENTRADA|SAÍDA — que alimentou `movements` — valoriza pelo custo médio
// ponderado vigente na data. Nos meses em que as duas coexistem elas
// DIVERGEM de verdade, e a planilha ainda cobre 2025 inteiro, anterior a
// qualquer lançamento neste app. Forçar uma a virar a outra restataria os
// números que o usuário já usa; então guardamos o histórico como está e
// mesclamos por MÊS (nunca metade de um mês de cada fonte).
namespace estoque {

struct RetrospectParams {
  int year;
  // "auto"   — planilha nos meses importados, razão de movimentações no resto
  // "ledger" — só as movimentações do sistema (usado para conferir a planilha)
  std::string source;
  std::string nowIso;  // "agora" injetado pelo chamador; o núcleo nunca lê o relógio
};

// Premissas do teto de gastos sugerido (mesma fórmula da planilha:
// fator = (1 + inflação) × (1 − meta de redução), aplicado sobre o gasto do
// ano-base, com um piso mensal para setores de gasto irrelevante).
struct BudgetParams {
  double metaReducao = 0.10;   // 10%
  double ipca = 0.045;         // 4,5%
  double pisoMensal = 300.0;   // R$/mês
};

BudgetParams loadBudgetParams(Database& db);
void saveBudgetParams(Database& db, const BudgetParams& params);

std::string computeRetrospectJson(Database& db, const RetrospectParams& params);

// Importa o histórico da planilha. O payload é
// {"rows":[{"year":2025,"month0":0,"dept":"COBRANÇA","amount":652.4}, ...]}.
// A substituição é POR ANO presente no payload (não incremental por célula):
// reimportar 2026 apaga o 2026 anterior inteiro, para que um departamento
// removido da planilha não sobreviva como fantasma. Devolve o nº de linhas
// gravadas.
int importDeptCostHistoryJson(Database& db, const std::string& payload);
std::string exportDeptCostHistoryJson(Database& db);

// Nome canônico de departamento para casar as duas fontes: apara as pontas,
// colapsa espaços internos e sobe para maiúsculas (ASCII + os acentuados
// latinos de 2 bytes em UTF-8). É o que faz 'Contabilidade' do razão casar
// com 'CONTABILIDADE' da planilha, e 'DP ' (com o espaço solto que existe no
// arquivo real) casar com 'DP'.
std::string canonDeptKey(const std::string& name);

}  // namespace estoque
