#pragma once
#include <string>

#include "estoque/db.hpp"

namespace estoque {

struct ReportParams {
  int year;
  int month0;              // 0-11 (janeiro=0)
  std::string deptFilter;  // "" = todos os departamentos
  int windowMonths;        // janela de consumo p/ cobertura/giro: 3, 6 ou 12
  std::string nowIso;      // "agora" injetado pelo chamador — o núcleo nunca lê o relógio do sistema
};

// Relatório mensal completo (KPIs, 6 séries de gráfico, curva ABC, posição de
// estoque, pedidos do período) como uma string JSON com as mesmas chaves
// camelCase do antigo localStorage — o frontend consome via JSON.parse(...)
// sem precisar saber que a fonte agora é C++/SQLite.
std::string computeReportJson(Database& db, const ReportParams& params);

}  // namespace estoque
