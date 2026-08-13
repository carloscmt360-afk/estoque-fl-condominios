#include "estoque/time_utils.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace estoque::time_utils {

namespace {

int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b, r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) --q;
  return q;
}
int64_t floorMod(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }

// Algoritmo de Howard Hinnant (domínio público) para converter entre data
// civil (calendário gregoriano proléptico) e dias desde 1970-01-01, com
// aritmética inteira pura — sem tocar timezone/libc, portável para Windows.
// http://howardhinnant.github.io/date_algorithms.html
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

struct CivilDate {
  int64_t y;
  unsigned m;  // 1-12
  unsigned d;  // 1-31
};

CivilDate civilFromDays(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp + (mp < 10 ? 3 : static_cast<unsigned>(-9));
  return CivilDate{y + (m <= 2 ? 1 : 0), m, d};
}

}  // namespace

int64_t isoToEpochMs(const std::string& iso) {
  if (iso.size() < 19) throw std::invalid_argument("data ISO inválida: '" + iso + "'");
  int year, month, day, hour, minute, sec;
  int n = std::sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &sec);
  if (n != 6) throw std::invalid_argument("data ISO inválida: '" + iso + "'");

  int ms = 0;
  auto dotPos = iso.find('.');
  if (dotPos != std::string::npos && dotPos + 3 < iso.size()) {
    ms = std::atoi(iso.substr(dotPos + 1, 3).c_str());
  }

  int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + sec;
  return seconds * 1000 + ms;
}

std::string epochMsToIso(int64_t ms) {
  int64_t totalSec = floorDiv(ms, 1000);
  int msPart = static_cast<int>(ms - totalSec * 1000);
  int64_t days = floorDiv(totalSec, 86400);
  int64_t secOfDay = totalSec - days * 86400;

  CivilDate cd = civilFromDays(days);
  int hour = static_cast<int>(secOfDay / 3600);
  int minute = static_cast<int>((secOfDay % 3600) / 60);
  int sec = static_cast<int>(secOfDay % 60);

  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02uT%02d:%02d:%02d.%03dZ",
                static_cast<long long>(cd.y), cd.m, cd.d, hour, minute, sec, msPart);
  return std::string(buf);
}

std::pair<int, int> shiftMonth(int year, int month0, int delta) {
  int64_t total = static_cast<int64_t>(year) * 12 + month0 + delta;
  int64_t y = floorDiv(total, 12);
  int64_t m0 = floorMod(total, 12);
  return {static_cast<int>(y), static_cast<int>(m0)};
}

int64_t monthStartMs(int year, int month0) {
  auto [ny, nm0] = shiftMonth(year, 0, month0);  // normaliza month0 fora de [0,11]
  int64_t days = daysFromCivil(ny, static_cast<unsigned>(nm0 + 1), 1);
  return days * 86400000LL;
}

int64_t monthEndMs(int year, int month0) {
  auto [ny, nm0] = shiftMonth(year, month0, 1);
  return monthStartMs(ny, nm0) - 1;
}

std::pair<int, int> yearMonthOf(int64_t epochMs) {
  int64_t days = floorDiv(epochMs, 86400000LL);
  CivilDate cd = civilFromDays(days);
  return {static_cast<int>(cd.y), static_cast<int>(cd.m) - 1};
}

int dayOfMonth(int64_t epochMs) {
  int64_t days = floorDiv(epochMs, 86400000LL);
  CivilDate cd = civilFromDays(days);
  return static_cast<int>(cd.d);
}

int daysInMonth(int year, int month0) { return dayOfMonth(monthEndMs(year, month0)); }

std::string systemNowIso() {
  // std::chrono::system_clock é UTC desde sempre na prática e por norma a
  // partir do C++20 — e o epoch em ms é justamente a moeda de troca do resto
  // deste arquivo, então reaproveita-se epochMsToIso em vez de mexer com
  // gmtime/localtime (que dependeriam de timezone do SO).
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return epochMsToIso(static_cast<int64_t>(ms));
}

}  // namespace estoque::time_utils
