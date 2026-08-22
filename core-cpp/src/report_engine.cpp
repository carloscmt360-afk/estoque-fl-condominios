#include "estoque/report_engine.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <unordered_map>
#include <vector>

#include "estoque/inventory_engine.hpp"
#include "estoque/retrospect_engine.hpp"  // canonDeptKey
#include "estoque/time_utils.hpp"

namespace estoque {

using json = nlohmann::json;
namespace tu = time_utils;

namespace {

const std::string SEM_DEPTO = "(sem departamento)";

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\n\r");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\n\r");
  return s.substr(b, e - b + 1);
}

struct MovementRec {
  std::string id, type, productId, supplier, nf, departmentId, recipient, encarregado, requester, obs,
      date, createdAt;
  double qty = 0, unitPrice = 0, resultingQty = 0, resultingAvgCost = 0, newAvgCost = 0;
  int64_t ts = 0;
};

std::string deptOf(const MovementRec& m) {
  std::string r = trim(m.recipient);
  return r.empty() ? SEM_DEPTO : r;
}

std::vector<MovementRec> loadMovements(Database& db) {
  std::vector<MovementRec> out;
  auto st = db.prepare(
      "SELECT id, type, product_id, qty, unit_price, supplier, nf, department_id, recipient, "
      "encarregado, requester, obs, date, resulting_qty, resulting_avg_cost, created_at, new_avg_cost "
      "FROM movements ORDER BY date, created_at");
  while (st.step()) {
    MovementRec m;
    m.id = st.columnText(0);
    m.type = st.columnText(1);
    m.productId = st.columnText(2);
    m.qty = st.columnDouble(3);
    m.unitPrice = st.columnIsNull(4) ? 0.0 : st.columnDouble(4);
    m.supplier = st.columnIsNull(5) ? "" : st.columnText(5);
    m.nf = st.columnIsNull(6) ? "" : st.columnText(6);
    m.departmentId = st.columnIsNull(7) ? "" : st.columnText(7);
    m.recipient = st.columnIsNull(8) ? "" : st.columnText(8);
    m.encarregado = st.columnIsNull(9) ? "" : st.columnText(9);
    m.requester = st.columnIsNull(10) ? "" : st.columnText(10);
    m.obs = st.columnIsNull(11) ? "" : st.columnText(11);
    m.date = st.columnText(12);
    m.resultingQty = st.columnIsNull(13) ? 0.0 : st.columnDouble(13);
    m.resultingAvgCost = st.columnIsNull(14) ? 0.0 : st.columnDouble(14);
    m.createdAt = st.columnText(15);
    m.newAvgCost = st.columnIsNull(16) ? 0.0 : st.columnDouble(16);
    // SQLite já ordenou por (date, created_at) como TEXT: nosso formato ISO
    // de largura fixa é lexicograficamente ordenável, então isso já é
    // cronológico — não precisamos reordenar aqui.
    m.ts = tu::isoToEpochMs(m.date);
    out.push_back(std::move(m));
  }
  return out;
}

struct PositionAcc {
  double qty = 0, avgCost = 0;
};
using Snapshot = std::unordered_map<std::string, PositionAcc>;

void applyToAcc(Snapshot& acc, const MovementRec& m) {
  auto& s = acc[m.productId];  // cria {0,0} se ainda não existir
  double q = m.qty;
  if (m.type == "entrada") {
    double baseQty = std::max(s.qty, 0.0);  // saldo negativo nunca entra na ponderação
    double novaQty = s.qty + q;
    s.avgCost = (baseQty + q) > 0 ? (baseQty * s.avgCost + q * m.unitPrice) / (baseQty + q) : 0.0;
    s.qty = novaQty;
  } else if (m.type == "saida") {
    s.qty -= q;
  } else {
    s.qty += q;  // ajuste: `q` já é o delta (qtyReal - qtyAnterior)
    if (m.newAvgCost > 0) s.avgCost = m.newAvgCost;  // correção manual do custo médio, opcional
  }
}

// Um único passe reconstrói a posição em vários cortes de data (mesma
// estratégia da versão web) — nunca confia em resulting_qty/resulting_avg_cost
// como fonte de verdade, sempre reprocessa cronologicamente.
std::vector<Snapshot> snapshotsAt(const std::vector<MovementRec>& sorted,
                                   const std::vector<int64_t>& cutoffsAsc) {
  Snapshot acc;
  std::vector<Snapshot> out;
  out.reserve(cutoffsAsc.size());
  size_t i = 0;
  for (int64_t cut : cutoffsAsc) {
    while (i < sorted.size() && sorted[i].ts <= cut) {
      applyToAcc(acc, sorted[i]);
      i++;
    }
    out.push_back(acc);  // cópia — snapshots anteriores não mudam depois
  }
  return out;
}

double snapValue(const Snapshot& snap) {
  double total = 0;
  for (auto& [id, s] : snap) total += s.qty * s.avgCost;
  return total;
}

struct MonthBucket {
  double consumo = 0, consumoGeral = 0, compras = 0, ajusteAbs = 0;
  int pedidos = 0;
};
int monthKey(int y, int m0) { return y * 12 + m0; }

struct ProdAgg {
  double qty = 0, val = 0;
};

struct UltimoPreco {
  int64_t ts;
  double price;
  std::string tipo;
};

json movementToJson(const MovementRec& m) {
  json j;
  j["id"] = m.id;
  j["productId"] = m.productId;
  j["qty"] = m.qty;
  j["unitPrice"] = m.unitPrice;
  j["departmentId"] = m.departmentId;
  j["recipient"] = m.recipient;
  j["encarregado"] = m.encarregado;
  j["requester"] = m.requester;
  j["obs"] = m.obs;
  j["date"] = m.date;
  j["_ts"] = m.ts;
  j["resultingQty"] = m.resultingQty;
  j["resultingAvgCost"] = m.resultingAvgCost;
  return j;
}

}  // namespace

std::string computeReportJson(Database& db, const ReportParams& params) {
  const int y = params.year;
  const int m = params.month0;
  const std::string& deptFilter = params.deptFilter;
  const int janela = params.windowMonths > 0 ? params.windowMonths : 6;

  auto products = listProducts(db);
  auto sorted = loadMovements(db);

  const int64_t fimRef = tu::monthEndMs(y, m);
  const int64_t iniRef = tu::monthStartMs(y, m);
  const int64_t hojeMs = tu::isoToEpochMs(params.nowIso);

  // 13 cortes: fechamento dos 12 meses anteriores + o mês de referência
  std::vector<std::pair<int, int>> linha;
  for (int k = 12; k >= 0; k--) linha.push_back(tu::shiftMonth(y, m, -k));
  std::vector<int64_t> cutoffs;
  cutoffs.reserve(linha.size());
  for (auto& [py, pm] : linha) cutoffs.push_back(tu::monthEndMs(py, pm));
  auto snaps = snapshotsAt(sorted, cutoffs);

  const Snapshot& posicao = snaps[12];
  const Snapshot& posicaoAnt = snaps[11];
  const Snapshot& posicaoIni12 = snaps[0];

  auto [wy, wm] = tu::shiftMonth(y, m, -(janela - 1));
  const int64_t iniJanela = tu::monthStartMs(wy, wm);

  // Mês-limite do acumulado: nunca ultrapassa o último mês já decorrido —
  // senão o ano anterior entraria completo contra um ano corrente pela metade.
  auto [hy, hm0] = tu::yearMonthOf(hojeMs);
  int mYTD;
  if (y > hy)
    mYTD = -1;
  else if (y == hy)
    mYTD = std::min(m, hm0);
  else
    mYTD = m;

  std::map<int, MonthBucket> byMonth;
  std::map<std::string, double> deptMes;
  // Gasto do mês por departamento IGNORANDO o filtro de departamento da tela:
  // o painel de limite mensal precisa continuar acusando quem estourou mesmo
  // quando o usuário está olhando o relatório de um setor só.
  std::map<std::string, double> gastoMesPorDepto;
  struct YtdEntry {
    double cur = 0, prev = 0;
  };
  std::map<std::string, YtdEntry> deptYTD;
  std::unordered_map<std::string, ProdAgg> prodMes, prodJanela;
  std::unordered_map<std::string, int64_t> prodUltimaSaida;
  std::unordered_map<std::string, UltimoPreco> prodUltimoPreco;

  json pedidosJson = json::array();
  json ajustesJson = json::array();
  int reconcLinhas = 0;
  double reconcDif = 0, reconcBaixa = 0;
  int64_t primeiroMov = -1;

  auto matchDept = [&](const MovementRec& mv) {
    return deptFilter.empty() || deptOf(mv) == deptFilter;
  };

  for (auto& mv : sorted) {
    if (primeiroMov < 0) primeiroMov = mv.ts;
    auto [dy, dm0] = tu::yearMonthOf(mv.ts);
    auto& b = byMonth[monthKey(dy, dm0)];
    double val = mv.qty * mv.unitPrice;

    if (mv.unitPrice > 0 && mv.ts <= fimRef && mv.type != "ajuste") {
      prodUltimoPreco[mv.productId] = UltimoPreco{mv.ts, mv.unitPrice, mv.type};
    }

    if (mv.type == "entrada") {
      if (mv.supplier != "Saldo inicial") b.compras += val;
    } else if (mv.type == "saida") {
      std::string dp = deptOf(mv);
      b.consumoGeral += val;
      prodUltimaSaida[mv.productId] = mv.ts;
      if (mv.ts >= iniRef && mv.ts <= fimRef) gastoMesPorDepto[canonDeptKey(dp)] += val;
      if (matchDept(mv)) {
        b.consumo += val;
        b.pedidos++;
        if (mv.ts >= iniRef && mv.ts <= fimRef) {
          pedidosJson.push_back(movementToJson(mv));
          deptMes[dp] += val;
          auto& pm = prodMes[mv.productId];
          pm.qty += mv.qty;
          pm.val += val;
        }
        if (dm0 <= mYTD && (dy == y || dy == y - 1)) {
          auto& e = deptYTD[dp];
          if (dy == y)
            e.cur += val;
          else
            e.prev += val;
        }
      }
      if (mv.ts >= iniJanela && mv.ts <= fimRef) {
        auto& pj = prodJanela[mv.productId];
        pj.qty += mv.qty;
        pj.val += val;
      }
      if (mv.ts >= iniRef && mv.ts <= fimRef && matchDept(mv)) {
        double cm = mv.resultingAvgCost;
        double dif = mv.qty * (cm - mv.unitPrice);
        if (std::abs(dif) > 0.005) {
          reconcLinhas++;
          reconcDif += dif;
        }
        reconcBaixa += mv.qty * cm;
      }
    } else {  // ajuste
      b.ajusteAbs += std::abs(val);
      if (mv.ts >= iniRef && mv.ts <= fimRef) {
        json a;
        a["id"] = mv.id;
        a["productId"] = mv.productId;
        a["qty"] = mv.qty;
        a["obs"] = mv.obs;
        a["date"] = mv.date;
        a["_ts"] = mv.ts;
        a["_val"] = val;
        ajustesJson.push_back(a);
      }
    }
  }

  auto getBucket = [&](int yy, int mm0) -> double {
    auto it = byMonth.find(monthKey(yy, mm0));
    return it == byMonth.end() ? 0.0 : it->second.consumo;
  };
  auto getBucketCompras = [&](int yy, int mm0) -> double {
    auto it = byMonth.find(monthKey(yy, mm0));
    return it == byMonth.end() ? 0.0 : it->second.compras;
  };

  auto itBRef = byMonth.find(monthKey(y, m));
  MonthBucket bRef = itBRef == byMonth.end() ? MonthBucket{} : itBRef->second;
  auto [antesY, antesM] = tu::shiftMonth(y, m, -1);

  const bool refFuturo = iniRef > hojeMs;
  const bool refEmCurso = !refFuturo && fimRef > hojeMs;
  const int diasNoMes = tu::daysInMonth(y, m);
  const int diasDecorridos = refEmCurso ? tu::dayOfMonth(hojeMs) : diasNoMes;

  int mesesHistorico = 0;
  if (primeiroMov >= 0) {
    auto [py, pm0] = tu::yearMonthOf(primeiroMov);
    mesesHistorico = std::max(0, (y - py) * 12 + (m - pm0) + 1);
  }
  const bool historicoParcial = mesesHistorico > 0 && mesesHistorico < janela;

  // ---- serie12 (spark + valor de estoque) ----
  json serie12 = json::array();
  std::vector<double> serie12Consumo;
  for (size_t i = 1; i < linha.size(); i++) {
    auto& [py, pm0] = linha[i];
    double consumo = getBucket(py, pm0);
    json s;
    s["y"] = py;
    s["m"] = pm0;
    s["consumo"] = consumo;
    s["valorEstoque"] = snapValue(snaps[i]);
    serie12.push_back(s);
    serie12Consumo.push_back(consumo);
  }

  // ---- anual: consumo mensal do ano de referência x ano anterior ----
  json anual = json::array();
  for (int i = 0; i < 12; i++) {
    bool futuro = tu::monthStartMs(y, i) > hojeMs;
    json a;
    a["mes"] = i;
    a["ref"] = futuro ? json(nullptr) : json(getBucket(y, i));
    a["ant"] = getBucket(y - 1, i);
    a["futuro"] = futuro;
    anual.push_back(a);
  }

  // ---- posição de estoque + curva ABC ----
  struct Item {
    Product p;
    double valor = 0, valorAnt = 0, consumoMesQtd = 0, consumoJanelaVal = 0;
    bool temCobertura = false;
    double cobertura = 0;
    std::string situacao;
    bool parado = false;
    bool temUltSaida = false;
    int64_t ultSaida = 0;
    bool temDivergencia = false;
    UltimoPreco divergencia{};
    double consumoRefQty = 0, consumoRefVal = 0;
    std::string classe;
  };

  std::vector<Item> itens;
  itens.reserve(products.size());
  for (auto& p : products) {
    Item it;
    it.p = p;

    auto sIt = posicao.find(p.id);
    PositionAcc s = sIt != posicao.end() ? sIt->second : PositionAcc{};
    auto sAIt = posicaoAnt.find(p.id);
    PositionAcc sA = sAIt != posicaoAnt.end() ? sAIt->second : PositionAcc{};
    auto cjIt = prodJanela.find(p.id);
    ProdAgg cj = cjIt != prodJanela.end() ? cjIt->second : ProdAgg{};

    it.consumoMesQtd = cj.qty / janela;
    it.valor = s.qty * s.avgCost;
    it.valorAnt = sA.qty * sA.avgCost;
    it.consumoJanelaVal = cj.val;

    auto usIt = prodUltimaSaida.find(p.id);
    if (usIt != prodUltimaSaida.end()) {
      it.temUltSaida = true;
      it.ultSaida = usIt->second;
    }

    it.parado = s.qty > 0 && cj.qty <= 0;
    if (it.consumoMesQtd > 0) {
      it.temCobertura = true;
      it.cobertura = s.qty / it.consumoMesQtd;
    }

    if (s.qty <= 0)
      it.situacao = "ruptura";
    else if (p.minStock > 0 && s.qty <= p.minStock)
      it.situacao = "baixo";
    else if (it.parado)
      it.situacao = "parado";
    else
      it.situacao = "ok";

    auto upIt = prodUltimoPreco.find(p.id);
    if (upIt != prodUltimoPreco.end()) {
      auto& up = upIt->second;
      if (up.price > 0 && s.avgCost > 0 && std::abs(s.avgCost - up.price) / up.price > 0.5) {
        it.temDivergencia = true;
        it.divergencia = up;
      }
    }

    auto cmIt = prodMes.find(p.id);
    if (cmIt != prodMes.end()) {
      it.consumoRefQty = cmIt->second.qty;
      it.consumoRefVal = cmIt->second.val;
    }

    it.p.qty = s.qty;
    it.p.avgCost = s.avgCost;

    itens.push_back(std::move(it));
  }

  double valorTotal = 0;
  for (auto& it : itens) valorTotal += it.valor;

  {
    // Classificação ABC: itens ordenados por valor decrescente, classe
    // atribuída pelo valor acumulado sobre valorTotal (que pode incluir
    // itens de valor negativo — comportamento preservado da versão web).
    std::vector<Item*> byValorDesc;
    byValorDesc.reserve(itens.size());
    for (auto& it : itens) byValorDesc.push_back(&it);
    std::sort(byValorDesc.begin(), byValorDesc.end(),
              [](Item* a, Item* b) { return a->valor > b->valor; });
    double acumulado = 0;
    for (auto* it : byValorDesc) {
      acumulado += it->valor;
      double pct = valorTotal > 0 ? acumulado / valorTotal : 1.0;
      it->classe = pct <= 0.80 ? "A" : (pct <= 0.95 ? "B" : "C");
      if (it->valor <= 0) it->classe = "C";
    }
  }

  struct AbcBucket {
    double v = 0;
    int n = 0;
  };
  std::map<std::string, AbcBucket> abc{{"A", {}}, {"B", {}}, {"C", {}}};
  for (auto& it : itens) {
    if (it.valor > 0) {
      abc[it.classe].v += it.valor;
      abc[it.classe].n++;
    }
  }

  // ---- indicadores ----
  double consumoJanelaTotal = 0;
  {
    int n = static_cast<int>(serie12Consumo.size());
    for (int i = std::max(0, n - janela); i < n; i++) consumoJanelaTotal += serie12Consumo[i];
  }
  double consumoMedioMes = consumoJanelaTotal / janela;
  double consumo12 = 0;
  for (double c : serie12Consumo) consumo12 += c;
  double estoqueMedio = (snapValue(posicaoIni12) + valorTotal) / 2.0;
  double movimentacaoRef = bRef.consumo + bRef.compras;

  json paradoItensJson = json::array();
  double paradoValor = 0;
  int rupturaCount = 0, baixoCount = 0;
  for (auto& it : itens) {
    if (it.situacao == "ruptura") rupturaCount++;
    if (it.situacao == "baixo") baixoCount++;
    if (it.parado) {
      paradoValor += it.valor;
      json pi;
      pi["id"] = it.p.id;
      pi["name"] = it.p.name;
      pi["valor"] = it.valor;
      paradoItensJson.push_back(pi);
    }
  }

  int itensDistintos = 0;
  {
    std::set<std::string> ids;
    for (auto& pj : pedidosJson) ids.insert(pj["productId"].get<std::string>());
    itensDistintos = static_cast<int>(ids.size());
  }

  json kpi;
  kpi["consumo"] = bRef.consumo;
  kpi["consumoAnt"] = refFuturo ? json(nullptr) : json(getBucket(antesY, antesM));
  kpi["consumoAnoAnt"] = refFuturo ? json(nullptr) : json(getBucket(y - 1, m));
  kpi["compras"] = bRef.compras;
  kpi["comprasAnt"] = refFuturo ? json(nullptr) : json(getBucketCompras(antesY, antesM));
  kpi["valorEstoque"] = valorTotal;
  kpi["valorEstoqueAnt"] = refFuturo ? json(nullptr) : json(snapValue(posicaoAnt));
  kpi["pedidos"] = static_cast<int>(pedidosJson.size());
  kpi["itensDistintos"] = itensDistintos;
  kpi["ticket"] = pedidosJson.empty() ? 0.0 : bRef.consumo / static_cast<double>(pedidosJson.size());
  kpi["ruptura"] = rupturaCount;
  kpi["baixo"] = baixoCount;
  kpi["paradoValor"] = paradoValor;
  kpi["paradoItens"] = paradoItensJson;
  kpi["cobertura"] = consumoMedioMes > 0 ? json(valorTotal / consumoMedioMes) : json(nullptr);
  kpi["giro"] = estoqueMedio > 0 ? json(consumo12 / estoqueMedio) : json(nullptr);
  kpi["acuracidade"] =
      movimentacaoRef > 0 ? json(std::max(0.0, 1.0 - bRef.ajusteAbs / movimentacaoRef)) : json(nullptr);
  kpi["ajustes"] = ajustesJson;

  // ---- itens (posição de estoque) para JSON ----
  json itensJson = json::array();
  for (auto& it : itens) {
    json j;
    j["id"] = it.p.id;
    j["sku"] = it.p.sku;
    j["name"] = it.p.name;
    j["unit"] = it.p.unit;
    j["category"] = it.p.category;
    j["minStock"] = it.p.minStock;
    j["qty"] = it.p.qty;
    j["avgCost"] = it.p.avgCost;
    j["valor"] = it.valor;
    j["valorAnt"] = it.valorAnt;
    j["consumoMesQtd"] = it.consumoMesQtd;
    j["consumoJanelaVal"] = it.consumoJanelaVal;
    j["cobertura"] = it.temCobertura ? json(it.cobertura) : json(nullptr);
    j["situacao"] = it.situacao;
    j["parado"] = it.parado;
    j["ultSaida"] = it.temUltSaida ? json(it.ultSaida) : json(nullptr);
    if (it.temDivergencia) {
      json d;
      d["ts"] = it.divergencia.ts;
      d["price"] = it.divergencia.price;
      d["tipo"] = it.divergencia.tipo;
      j["divergencia"] = d;
    } else {
      j["divergencia"] = nullptr;
    }
    json consumoRef;
    consumoRef["qty"] = it.consumoRefQty;
    consumoRef["val"] = it.consumoRefVal;
    j["consumoRef"] = consumoRef;
    j["classe"] = it.classe;
    itensJson.push_back(j);
  }

  json abcJson;
  for (auto& [k, v] : abc) {
    json b;
    b["v"] = v.v;
    b["n"] = v.n;
    abcJson[k] = b;
  }

  // ---- limite mensal por departamento ----
  // O limite é do CADASTRO do departamento (departments.monthly_limit), então
  // a lista sai do cadastro e não das saídas: um setor com limite e zero gasto
  // precisa aparecer, e um setor sem limite precisa aparecer como "não
  // definido" em vez de sumir.
  json limitesJson = json::array();
  int limComLimite = 0, limAtencao = 0, limEstourado = 0;
  double limTetoTotal = 0, limGastoTotal = 0;
  for (auto& d : listDepartments(db)) {
    auto gIt = gastoMesPorDepto.find(canonDeptKey(d.name));
    double gasto = gIt == gastoMesPorDepto.end() ? 0.0 : gIt->second;
    json l;
    l["id"] = d.id;
    l["name"] = d.name;
    l["encarregado"] = d.encarregado;
    l["limite"] = d.monthlyLimit;
    l["gasto"] = gasto;
    l["temLimite"] = d.monthlyLimit > 0;
    if (d.monthlyLimit > 0) {
      double pct = gasto / d.monthlyLimit;
      l["pct"] = pct;
      l["saldo"] = d.monthlyLimit - gasto;
      l["status"] = pct >= 1.0 ? "estourado" : (pct >= 0.9 ? "atencao" : "ok");
      limComLimite++;
      limTetoTotal += d.monthlyLimit;
      limGastoTotal += gasto;
      if (pct >= 1.0)
        limEstourado++;
      else if (pct >= 0.9)
        limAtencao++;
    } else {
      l["pct"] = nullptr;
      l["saldo"] = nullptr;
      l["status"] = "sem-limite";
    }
    limitesJson.push_back(l);
  }

  json limites;
  limites["linhas"] = limitesJson;
  limites["comLimite"] = limComLimite;
  limites["atencao"] = limAtencao;
  limites["estourado"] = limEstourado;
  limites["tetoTotal"] = limTetoTotal;
  limites["gastoTotal"] = limGastoTotal;

  json deptMesJson = json::object();
  for (auto& [k, v] : deptMes) deptMesJson[k] = v;

  json deptYTDJson = json::object();
  double ytdCur = 0, ytdPrev = 0;
  for (auto& [k, v] : deptYTD) {
    json e;
    e["cur"] = v.cur;
    e["prev"] = v.prev;
    deptYTDJson[k] = e;
    ytdCur += v.cur;
    ytdPrev += v.prev;
  }

  json result;
  result["y"] = y;
  result["m"] = m;
  result["mYTD"] = mYTD;
  result["janela"] = janela;
  result["deptFilter"] = deptFilter;
  result["fimRef"] = fimRef;
  result["iniRef"] = iniRef;
  result["refFuturo"] = refFuturo;
  result["refEmCurso"] = refEmCurso;
  result["diasDecorridos"] = diasDecorridos;
  result["diasNoMes"] = diasNoMes;
  result["mesesHistorico"] = mesesHistorico;
  result["historicoParcial"] = historicoParcial;
  result["valorTotal"] = valorTotal;
  result["itens"] = itensJson;
  result["abc"] = abcJson;
  result["kpi"] = kpi;
  result["pedidos"] = pedidosJson;
  result["serie12"] = serie12;
  result["anual"] = anual;
  result["deptMes"] = deptMesJson;
  result["limites"] = limites;
  result["deptYTD"] = deptYTDJson;
  result["ytdCur"] = ytdCur;
  result["ytdPrev"] = ytdPrev;
  json reconc;
  reconc["linhas"] = reconcLinhas;
  reconc["dif"] = reconcDif;
  reconc["baixa"] = reconcBaixa;
  result["reconc"] = reconc;

  return result.dump();
}

}  // namespace estoque
