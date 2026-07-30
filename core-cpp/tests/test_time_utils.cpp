#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/time_utils.hpp"

using namespace estoque::time_utils;

TEST_CASE("isoToEpochMs / epochMsToIso fazem round-trip") {
  std::string iso = "2026-06-15T14:30:05.123Z";
  int64_t ms = isoToEpochMs(iso);
  CHECK(epochMsToIso(ms) == iso);
}

TEST_CASE("isoToEpochMs concorda com valores conhecidos (epoch e um marco recente)") {
  CHECK(isoToEpochMs("1970-01-01T00:00:00.000Z") == 0);
  // 2026-01-01T00:00:00Z em epoch ms (conferido contra `date -u -d ... +%s`)
  CHECK(isoToEpochMs("2026-01-01T00:00:00.000Z") == 1767225600000LL);
}

TEST_CASE("monthStartMs / monthEndMs delimitam o mês corretamente") {
  int64_t start = monthStartMs(2026, 5);  // junho/2026 (month0=5)
  int64_t end = monthEndMs(2026, 5);
  CHECK(epochMsToIso(start) == "2026-06-01T00:00:00.000Z");
  CHECK(epochMsToIso(end) == "2026-06-30T23:59:59.999Z");
  CHECK(end > start);
  CHECK(end - start == 30LL * 86400000 - 1);  // junho tem 30 dias
}

TEST_CASE("monthEndMs de fevereiro respeita ano bissexto") {
  CHECK(daysInMonth(2024, 1) == 29);  // 2024 é bissexto
  CHECK(daysInMonth(2026, 1) == 28);  // 2026 não é
}

TEST_CASE("shiftMonth atravessa fronteira de ano nos dois sentidos") {
  auto [y1, m1] = shiftMonth(2026, 0, -1);  // janeiro/2026 - 1 mês
  CHECK(y1 == 2025);
  CHECK(m1 == 11);  // dezembro/2025

  auto [y2, m2] = shiftMonth(2026, 11, 1);  // dezembro/2026 + 1 mês
  CHECK(y2 == 2027);
  CHECK(m2 == 0);  // janeiro/2027

  auto [y3, m3] = shiftMonth(2026, 5, -12);  // junho/2026 - 12 meses
  CHECK(y3 == 2025);
  CHECK(m3 == 5);
}

TEST_CASE("yearMonthOf e dayOfMonth batem com o epoch calculado") {
  int64_t ms = isoToEpochMs("2026-06-15T10:00:00.000Z");
  auto [y, m0] = yearMonthOf(ms);
  CHECK(y == 2026);
  CHECK(m0 == 5);
  CHECK(dayOfMonth(ms) == 15);
}
