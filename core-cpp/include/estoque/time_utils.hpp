#pragma once
#include <cstdint>
#include <string>
#include <utility>

// Utilitários de data para o motor de relatório. Deliberadamente NÃO é um
// parser ISO 8601 geral — só entende o formato fixo que este app sempre
// gera: "YYYY-MM-DDTHH:MM:SS.mmmZ", sempre UTC. Nenhuma função aqui lê o
// relógio do sistema — "agora" é sempre passado pelo chamador (mantém o
// motor de relatório determinístico e testável).
namespace estoque::time_utils {

// Epoch em milissegundos (UTC), consistente com Date.parse(iso) do JS.
int64_t isoToEpochMs(const std::string& iso);
std::string epochMsToIso(int64_t ms);

// month0 é 0-11 (janeiro=0), como no JS.
int64_t monthStartMs(int year, int month0);
int64_t monthEndMs(int year, int month0);  // último ms do mês (23:59:59.999)

// Desloca (year, month0) por `delta` meses (pode ser negativo).
std::pair<int, int> shiftMonth(int year, int month0, int delta);

// Ano/mês (0-11) correspondentes a um epoch ms (UTC) — usado para bucketizar
// movimentações por mês de referência.
std::pair<int, int> yearMonthOf(int64_t epochMs);

// Dia do mês (1-31) de um epoch ms — usado para "X de Y dias decorridos".
int dayOfMonth(int64_t epochMs);

// Quantos dias tem o mês (28-31).
int daysInMonth(int year, int month0);

}  // namespace estoque::time_utils
