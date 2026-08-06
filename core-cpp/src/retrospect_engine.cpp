#include "estoque/retrospect_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

#include "estoque/inventory_engine.hpp"
#include "estoque/time_utils.hpp"

namespace estoque {

using json = nlohmann::json;
namespace tu = time_utils;

namespace {

const char* kSemDepto = "(sem departamento)";

using Row = std::array<double, 12>;

// Uma fonte de dados (planilha importada ou razão de movimentações) já
// agregada em ano -> departamento -> 12 meses. `temMes` distingue "o mês
// existe nesta fonte e vale zero" de "esta fonte não conhece o mês" — é essa
// diferença que a mesclagem usa para decidir de quem é a vez.
struct YearGrid {
  std::map<std::string, Row> byDept;
  std::array<bool, 12> temMes{};
};
using Source = std::map<int, YearGrid>;

Source loadHistory(Database& db, std::map<std::string, std::string>& display) {
  Source src;
  auto st = db.prepare("SELECT year, month0, dept_key, dept_name, amount FROM dept_cost_history");
  while (st.step()) {
    int m = static_cast<int>(st.columnDouble(1));
    if (m < 0 || m > 11) continue;
    std::string key = st.columnText(2);
    if (key.empty()) continue;
    auto& g = src[static_cast<int>(st.columnDouble(0))];
    g.byDept[key][m] += st.columnDouble(4);
    g.temMes[m] = true;
    display.emplace(key, st.columnText(3));
  }
  return src;
}

Source loadLedger(Database& db, std::map<std::string, std::string>& display) {
  Source src;
  // Mesma valoração do report_engine (qty × unit_price gravado no lançamento),
  // para que o retrospecto no modo "só movimentações" bata exatamente com o
  // "custo por departamento" do relatório mensal.
  auto st = db.prepare("SELECT date, recipient, qty, unit_price FROM movements WHERE type='saida'");
  while (st.step()) {
    auto [y, m] = tu::yearMonthOf(tu::isoToEpochMs(st.columnText(0)));
    if (m < 0 || m > 11) continue;
    std::string nome = st.columnIsNull(1) ? "" : st.columnText(1);
    std::string key = canonDeptKey(nome);
    if (key.empty()) {
      nome = kSemDepto;
      key = canonDeptKey(nome);
    }
    auto& g = src[y];
    g.byDept[key][m] += st.columnDouble(2) * (st.columnIsNull(3) ? 0.0 : st.columnDouble(3));
    g.temMes[m] = true;
    display.emplace(key, nome);
  }
  return src;
}

// Um ano já resolvido: para cada mês, os valores vieram de UMA fonte só.
struct MergedYear {
  int year = 0;
  std::map<std::string, Row> byDept;
  std::array<double, 12> totaisMes{};
  std::array<std::string, 12> origem;  // "planilha" | "sistema" | "" (sem dado)
  double total = 0;
  int ultimoMes = -1;
};

MergedYear mergeYear(int year, const Source& hist, const Source& led, bool ledgerOnly) {
  MergedYear out;
  out.year = year;
  auto hIt = hist.find(year);
  auto lIt = led.find(year);
  const YearGrid* h = hIt != hist.end() ? &hIt->second : nullptr;
  const YearGrid* l = lIt != led.end() ? &lIt->second : nullptr;

  for (int m = 0; m < 12; m++) {
    const YearGrid* fonte = nullptr;
    // Mesclagem por MÊS INTEIRO, nunca por célula: se metade dos
    // departamentos viesse da planilha e a outra do razão no mesmo mês, o
    // total do mês não seria de fonte nenhuma.
    if (!ledgerOnly && h && h->temMes[m]) {
      fonte = h;
      out.origem[m] = "planilha";
    } else if (l && l->temMes[m]) {
      fonte = l;
      out.origem[m] = "sistema";
    }
    if (!fonte) continue;
    out.ultimoMes = m;
    for (auto& [key, row] : fonte->byDept) {
      out.byDept[key][m] = row[m];
      out.totaisMes[m] += row[m];
    }
  }
  for (double v : out.totaisMes) out.total += v;
  return out;
}

double somaAte(const Row& row, int mesLimite) {
  double s = 0;
  for (int m = 0; m <= mesLimite && m < 12; m++) s += row[m];
  return s;
}

const Row kRowZero{};
const Row& rowOf(const std::map<std::string, Row>& m, const std::string& key) {
  auto it = m.find(key);
  return it == m.end() ? kRowZero : it->second;
}

json gradeToJson(const MergedYear& g, const std::map<std::string, std::string>& display) {
  json linhas = json::array();
  for (auto& [key, row] : g.byDept) {
    double total = 0;
    json meses = json::array();
    for (double v : row) {
      meses.push_back(v);
      total += v;
    }
    // Departamento que só aparece com zeros (a planilha lista o setor no mês
    // mesmo sem consumo) não vira linha — a matriz já é larga demais.
    if (std::abs(total) < 0.005) continue;
    json l;
    auto dIt = display.find(key);
    l["key"] = key;
    l["name"] = dIt != display.end() ? dIt->second : key;
    l["meses"] = meses;
    l["total"] = total;
    linhas.push_back(l);
  }
  json totaisMes = json::array();
  for (double v : g.totaisMes) totaisMes.push_back(v);
  json origem = json::array();
  for (auto& o : g.origem) origem.push_back(o);

  json out;
  out["year"] = g.year;
  out["linhas"] = linhas;
  out["totaisMes"] = totaisMes;
  out["origem"] = origem;
  out["total"] = g.total;
  out["ultimoMes"] = g.ultimoMes;
  return out;
}

double settingNum(Database& db, const char* key, double fallback) {
  auto st = db.prepare("SELECT value FROM app_settings WHERE key=?");
  st.bind(1, std::string(key));
  if (!st.step()) return fallback;
  try {
    return std::stod(st.columnText(0));
  } catch (const std::exception&) {
    return fallback;
  }
}

void setSettingNum(Database& db, const char* key, double value) {
  auto st = db.prepare("INSERT INTO app_settings (key, value) VALUES (?, ?) "
                       "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  st.bind(1, std::string(key)).bind(2, std::to_string(value));
  st.step();
}

}  // namespace

std::string canonDeptKey(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  bool espacoPendente = false;
  for (size_t i = 0; i < name.size();) {
    unsigned char c = static_cast<unsigned char>(name[i]);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!out.empty()) espacoPendente = true;  // espaço à esquerda some; à direita nunca é emitido
      i++;
      continue;
    }
    if (espacoPendente) {
      out.push_back(' ');
      espacoPendente = false;
    }
    // Latin-1 acentuado em UTF-8 ocupa 2 bytes: 0xC3 seguido de 0xA0..0xBE
    // (à..þ). Subtrair 0x20 do segundo byte sobe para a maiúscula
    // correspondente (0x80..0x9E). 0xB7 é ÷, não uma letra.
    if (c == 0xC3 && i + 1 < name.size()) {
      unsigned char d = static_cast<unsigned char>(name[i + 1]);
      if (d >= 0xA0 && d <= 0xBE && d != 0xB7) d -= 0x20;
      out.push_back(static_cast<char>(c));
      out.push_back(static_cast<char>(d));
      i += 2;
      continue;
    }
    out.push_back(static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c));
    i++;
  }
  return out;
}

BudgetParams loadBudgetParams(Database& db) {
  BudgetParams p;
  p.metaReducao = settingNum(db, "retro.metaReducao", p.metaReducao);
  p.ipca = settingNum(db, "retro.ipca", p.ipca);
  p.pisoMensal = settingNum(db, "retro.pisoMensal", p.pisoMensal);
  return p;
}

void saveBudgetParams(Database& db, const BudgetParams& params) {
  Transaction tx(db);
  setSettingNum(db, "retro.metaReducao", params.metaReducao);
  setSettingNum(db, "retro.ipca", params.ipca);
  setSettingNum(db, "retro.pisoMensal", std::max(0.0, params.pisoMensal));
  tx.commit();
}

int importDeptCostHistoryJson(Database& db, const std::string& payload) {
  json root = json::parse(payload);
  const json& rows = root.is_array() ? root : (root.contains("rows") ? root["rows"] : json::array());
  if (!rows.is_array()) throw std::runtime_error("histórico inválido: 'rows' deve ser uma lista");

  std::set<int> anos;
  for (auto& r : rows) {
    if (!r.contains("year")) throw std::runtime_error("histórico inválido: linha sem 'year'");
    anos.insert(r["year"].get<int>());
  }

  Transaction tx(db);
  for (int ano : anos) {
    db.prepare("DELETE FROM dept_cost_history WHERE year=?").bind(1, static_cast<double>(ano)).step();
  }
  int gravadas = 0;
  for (auto& r : rows) {
    std::string nome = r.contains("dept") ? r["dept"].get<std::string>() : "";
    std::string key = canonDeptKey(nome);
    if (key.empty()) continue;
    int m = r.contains("month0") ? r["month0"].get<int>() : -1;
    if (m < 0 || m > 11) throw std::runtime_error("histórico inválido: 'month0' fora de 0..11");
    auto st = db.prepare(
        "INSERT INTO dept_cost_history (year, month0, dept_key, dept_name, amount) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(year, month0, dept_key) DO UPDATE SET amount = amount + excluded.amount");
    st.bind(1, static_cast<double>(r["year"].get<int>())).bind(2, static_cast<double>(m));
    st.bind(3, key).bind(4, nome);
    st.bind(5, r.contains("amount") && !r["amount"].is_null() ? r["amount"].get<double>() : 0.0);
    st.step();
    gravadas++;
  }
  tx.commit();
  return gravadas;
}

std::string exportDeptCostHistoryJson(Database& db) {
  json rows = json::array();
  auto st = db.prepare(
      "SELECT year, month0, dept_name, amount FROM dept_cost_history ORDER BY year, month0, dept_key");
  while (st.step()) {
    json r;
    r["year"] = static_cast<int>(st.columnDouble(0));
    r["month0"] = static_cast<int>(st.columnDouble(1));
    r["dept"] = st.columnText(2);
    r["amount"] = st.columnDouble(3);
    rows.push_back(r);
  }
  return rows.dump();
}

std::string computeRetrospectJson(Database& db, const RetrospectParams& params) {
  const int y = params.year;
  const int yAnt = y - 1;
  const bool ledgerOnly = params.source == "ledger";
  const int64_t hojeMs = tu::isoToEpochMs(params.nowIso);

  // Ordem de prioridade do nome de exibição: cadastro de departamentos (é o
  // que o usuário edita hoje) > planilha > razão. `emplace` não sobrescreve,
  // então basta inserir nessa ordem.
  std::map<std::string, std::string> display;
  std::map<std::string, double> limiteMensal;
  for (auto& d : listDepartments(db)) {
    std::string key = canonDeptKey(d.name);
    if (key.empty()) continue;
    display.emplace(key, d.name);
    limiteMensal[key] = d.monthlyLimit;
  }
  auto hist = loadHistory(db, display);
  auto led = loadLedger(db, display);

  std::set<int> anosSet;
  for (auto& [ano, _] : hist) anosSet.insert(ano);
  for (auto& [ano, _] : led) anosSet.insert(ano);
  anosSet.insert(y);
  std::vector<int> anos(anosSet.rbegin(), anosSet.rend());

  MergedYear ref = mergeYear(y, hist, led, ledgerOnly);
  MergedYear ant = mergeYear(yAnt, hist, led, ledgerOnly);

  // ---- comparativo do MESMO período (jan..último mês com dado no ano ref) ----
  const int mesLimite = ref.ultimoMes;
  std::set<std::string> chaves;
  for (auto& [k, _] : ref.byDept) chaves.insert(k);
  for (auto& [k, _] : ant.byDept) chaves.insert(k);

  json compLinhas = json::array();
  double curTotal = 0, prevTotal = 0;
  if (mesLimite >= 0) {
    for (auto& key : chaves) {
      double cur = somaAte(rowOf(ref.byDept, key), mesLimite);
      double prev = somaAte(rowOf(ant.byDept, key), mesLimite);
      curTotal += cur;
      prevTotal += prev;
      if (std::abs(cur) < 0.005 && std::abs(prev) < 0.005) continue;
      json l;
      auto dIt = display.find(key);
      l["key"] = key;
      l["name"] = dIt != display.end() ? dIt->second : key;
      l["cur"] = cur;
      l["prev"] = prev;
      compLinhas.push_back(l);
    }
  }

  // ---- teto de gastos sugerido a partir do ano-base (ano anterior) ----
  BudgetParams bp = loadBudgetParams(db);
  const double fator = (1.0 + bp.ipca) * (1.0 - bp.metaReducao);
  const int mesesDecorridos = mesLimite + 1;

  std::set<std::string> chavesTeto = chaves;
  for (auto& [k, _] : limiteMensal) chavesTeto.insert(k);

  json tetoLinhas = json::array();
  double baseTotal = 0, tetoMensalTotal = 0, tetoAnualTotal = 0, realizadoTotal = 0, tetoPeriodoTotal = 0;
  for (auto& key : chavesTeto) {
    const Row& rowAnt = rowOf(ant.byDept, key);
    const Row& rowRef = rowOf(ref.byDept, key);
    double base = somaAte(rowAnt, 11);
    double realizado = somaAte(rowRef, 11);
    auto limIt = limiteMensal.find(key);
    double limite = limIt != limiteMensal.end() ? limIt->second : 0.0;
    if (std::abs(base) < 0.005 && std::abs(realizado) < 0.005 && limite <= 0) continue;

    double ajustado = base * fator / 12.0;
    double tetoMes = std::max(ajustado, bp.pisoMensal);
    double tetoAno = tetoMes * 12.0;
    double tetoPeriodo = tetoMes * mesesDecorridos;

    json l;
    auto dIt = display.find(key);
    l["key"] = key;
    l["name"] = dIt != display.end() ? dIt->second : key;
    l["base"] = base;
    l["tetoMensal"] = tetoMes;
    l["tetoAnual"] = tetoAno;
    l["tetoPeriodo"] = tetoPeriodo;
    l["realizado"] = realizado;
    l["noPiso"] = ajustado < bp.pisoMensal;
    l["limiteMensal"] = limite;
    l["temLimite"] = limite > 0;
    l["pctAnual"] = tetoAno > 0 ? json(realizado / tetoAno) : json(nullptr);
    // O status compara o realizado com o teto PROPORCIONAL aos meses já
    // decorridos — senão meio ano de gasto sempre pareceria folgado contra um
    // teto anual inteiro.
    if (tetoPeriodo > 0) {
      double pct = realizado / tetoPeriodo;
      l["pctPeriodo"] = pct;
      l["status"] = pct >= 1.0 ? "estourado" : (pct >= 0.9 ? "atencao" : "ok");
    } else {
      l["pctPeriodo"] = nullptr;
      l["status"] = "sem-base";
    }
    tetoLinhas.push_back(l);

    baseTotal += base;
    tetoMensalTotal += tetoMes;
    tetoAnualTotal += tetoAno;
    tetoPeriodoTotal += tetoPeriodo;
    realizadoTotal += realizado;
  }

  // ---- série mensal para o gráfico de colunas agrupadas (ref x ano anterior) ----
  // `ref` vira null tanto no mês que ainda não chegou quanto no mês que
  // nenhuma fonte conhece: os dois casos são "não sabemos", e um deles virando
  // R$ 0,00 seria afirmar que o setor não gastou nada.
  json anual = json::array();
  for (int i = 0; i < 12; i++) {
    bool futuro = tu::monthStartMs(y, i) > hojeMs;
    bool semDado = ref.origem[i].empty();
    bool antSemDado = ant.origem[i].empty();
    json a;
    a["mes"] = i;
    a["ref"] = (futuro || semDado) ? json(nullptr) : json(ref.totaisMes[i]);
    a["ant"] = ant.totaisMes[i];
    a["futuro"] = futuro;
    a["semDado"] = semDado;
    a["antSemDado"] = antSemDado;
    anual.push_back(a);
  }

  int fontePlanilha = 0, fonteSistema = 0, fonteVazio = 0;
  for (auto& o : ref.origem) {
    if (o == "planilha") fontePlanilha++;
    else if (o == "sistema") fonteSistema++;
    else fonteVazio++;
  }

  json paramsJson;
  paramsJson["metaReducao"] = bp.metaReducao;
  paramsJson["ipca"] = bp.ipca;
  paramsJson["pisoMensal"] = bp.pisoMensal;
  paramsJson["fator"] = fator;

  json comparativo;
  comparativo["mesLimite"] = mesLimite;
  comparativo["linhas"] = compLinhas;
  comparativo["curTotal"] = curTotal;
  comparativo["prevTotal"] = prevTotal;

  json teto;
  teto["baseYear"] = yAnt;
  teto["mesesDecorridos"] = mesesDecorridos;
  teto["linhas"] = tetoLinhas;
  teto["baseTotal"] = baseTotal;
  teto["tetoMensalTotal"] = tetoMensalTotal;
  teto["tetoAnualTotal"] = tetoAnualTotal;
  teto["tetoPeriodoTotal"] = tetoPeriodoTotal;
  teto["realizadoTotal"] = realizadoTotal;

  json fontes;
  fontes["planilha"] = fontePlanilha;
  fontes["sistema"] = fonteSistema;
  fontes["vazio"] = fonteVazio;

  json out;
  out["year"] = y;
  out["prevYear"] = yAnt;
  out["source"] = ledgerOnly ? "ledger" : "auto";
  out["anos"] = anos;
  out["params"] = paramsJson;
  out["ref"] = gradeToJson(ref, display);
  out["ant"] = gradeToJson(ant, display);
  out["comparativo"] = comparativo;
  out["teto"] = teto;
  out["anual"] = anual;
  out["fontes"] = fontes;
  return out.dump();
}

}  // namespace estoque
