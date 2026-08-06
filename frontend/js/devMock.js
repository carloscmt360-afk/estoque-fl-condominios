// Mock de desenvolvimento — SÓ para abrir o frontend num navegador comum e
// revisar o layout sem precisar compilar Rust/C++/Tauri. Alimentado por
// fixtures reais (mesmos bytes que o backend C++ geraria), capturadas via
// core-cli. Não é carregado pelo app de produção (o Tauri real nunca define
// window.__TAURI_DEV_MOCK__).
import { useMock } from './api.js';

async function loadJson(path) {
  const res = await fetch(path);
  return res.json();
}

async function loadJsonOptional(path) {
  try {
    const res = await fetch(path);
    return res.ok ? await res.json() : null;
  } catch (e) {
    return null;
  }
}

export async function installDevMock() {
  const [backup, report, products, departments] = await Promise.all([
    loadJson('fixtures/backup.json'),
    loadJson('fixtures/report_2026_06.json'),
    loadJson('fixtures/products.json'),
    loadJson('fixtures/departments.json'),
  ]);
  // Opcional: a fixture do retrospecto só existe se tiver sido gerada de um
  // banco com o histórico da planilha importado (ver core-cli --retrospect).
  const retrospect = await loadJsonOptional('fixtures/retrospect_2026.json');

  // Estado mutável em memória — CRUD do mock opera sobre isso, então criar/
  // editar/excluir na sessão de revisão visual funciona de verdade (só não
  // persiste entre reloads da página).
  let state = { products, departments, movements: backup.movements };

  function uid(p) { return (p || 'id_') + Math.random().toString(36).slice(2, 10); }

  const cmp = (a, b) => (a < b ? -1 : a > b ? 1 : 0);
  function ordemCronologica(a, b) {
    return cmp(a.date, b.date) || cmp(a.createdAt || '', b.createdAt || '');
  }
  function passo(s, m) {
    if (m.type === 'entrada') {
      const base = Math.max(s.qty, 0);
      s.avgCost = base + m.qty > 0 ? (base * s.avgCost + m.qty * (m.unitPrice || 0)) / (base + m.qty) : 0;
      s.qty += m.qty;
    } else if (m.type === 'saida') s.qty -= m.qty;
    else s.qty += m.qty;
  }
  /* Espelha inventory_engine.cpp::recomputeProduct, inclusive a preservação
     do offset entre o razão e o saldo gravado — sem isso o mock mostraria um
     comportamento de edição diferente do app real. */
  function recompute(productId, offsetQty) {
    const p = state.products.find((x) => x.id === productId);
    const doProduto = state.movements.filter((m) => m.productId === productId).sort(ordemCronologica);
    const s = { qty: 0, avgCost: 0 };
    for (const m of doProduto) {
      passo(s, m);
      if (m.type === 'ajuste') m.unitPrice = s.avgCost;
      m.resultingQty = s.qty + offsetQty;
      m.resultingAvgCost = s.avgCost;
    }
    if (p) { p.qty = s.qty + offsetQty; p.avgCost = s.avgCost; }
  }
  function offsetDe(productId) {
    const p = state.products.find((x) => x.id === productId);
    const s = { qty: 0, avgCost: 0 };
    state.movements.filter((m) => m.productId === productId).sort(ordemCronologica).forEach((m) => passo(s, m));
    return (p ? p.qty : 0) - s.qty;
  }
  function posicaoAntes(productId, date, createdAt, excluirId, offsetQty) {
    const s = { qty: 0, avgCost: 0 };
    state.movements
      .filter((m) => m.productId === productId && m.id !== excluirId &&
        (m.date < date || (m.date === date && (m.createdAt || '') < createdAt)))
      .sort(ordemCronologica)
      .forEach((m) => passo(s, m));
    return { qty: s.qty + offsetQty, avgCost: s.avgCost };
  }

  useMock(async (cmd, args) => {
    switch (cmd) {
      case 'app_status': return null;
      case 'list_products': return JSON.stringify(state.products);
      case 'list_departments': return JSON.stringify(state.departments);
      case 'create_product': {
        const p = { ...args.product, qty: 0, avgCost: 0 };
        state.products.push(p);
        return JSON.stringify(p);
      }
      case 'update_product': {
        const i = state.products.findIndex((p) => p.id === args.product.id);
        if (i >= 0) state.products[i] = { ...state.products[i], ...args.product };
        return JSON.stringify(state.products[i]);
      }
      case 'delete_product':
        state.products = state.products.filter((p) => p.id !== args.id);
        state.movements = state.movements.filter((m) => m.productId !== args.id);
        return null;
      case 'create_department': {
        state.departments.push(args.department);
        return JSON.stringify(args.department);
      }
      case 'update_department': {
        const i = state.departments.findIndex((d) => d.id === args.department.id);
        if (i >= 0) state.departments[i] = { ...state.departments[i], ...args.department };
        return JSON.stringify(state.departments[i]);
      }
      case 'delete_department':
        state.departments = state.departments.filter((d) => d.id !== args.id);
        return null;
      case 'apply_entrada': {
        const i = args.input;
        const offset = offsetDe(i.productId);
        const m = { id: uid('m_'), type: 'entrada', productId: i.productId, qty: i.qty, unitPrice: i.unitPrice,
          supplier: i.supplier, nf: i.nf, departmentId: '', recipient: '', encarregado: '', requester: '',
          date: i.date, obs: i.obs, resultingQty: 0, resultingAvgCost: 0, createdAt: i.createdAt };
        state.movements.push(m);
        recompute(i.productId, offset);
        return JSON.stringify(m);
      }
      case 'apply_saida': {
        const i = args.input;
        const dept = state.departments.find((x) => x.id === i.departmentId);
        const offset = offsetDe(i.productId);
        const m = { id: uid('m_'), type: 'saida', productId: i.productId, qty: i.qty,
          unitPrice: posicaoAntes(i.productId, i.date, i.createdAt, null, offset).avgCost,
          departmentId: dept.id, recipient: dept.name, encarregado: dept.encarregado, requester: i.requester,
          date: i.date, obs: i.obs, resultingQty: 0, resultingAvgCost: 0, createdAt: i.createdAt };
        state.movements.push(m);
        recompute(i.productId, offset);
        return JSON.stringify(m);
      }
      case 'apply_correcao': {
        const i = args.input;
        const offset = offsetDe(i.productId);
        const antes = posicaoAntes(i.productId, i.date, i.createdAt, null, offset);
        const m = { id: uid('m_'), type: 'ajuste', productId: i.productId, qty: i.qtyReal - antes.qty,
          unitPrice: antes.avgCost, departmentId: '', recipient: '', encarregado: '', requester: '',
          obs: i.motivo, date: i.date, resultingQty: 0, resultingAvgCost: 0, createdAt: i.createdAt };
        state.movements.push(m);
        recompute(i.productId, offset);
        return JSON.stringify(m);
      }
      case 'list_movements':
        return JSON.stringify([...state.movements].sort(ordemCronologica));
      case 'update_movement': {
        const patch = args.patch;
        const m = state.movements.find((x) => x.id === patch.id);
        if (!m) throw new Error('lançamento não encontrado: ' + patch.id);
        const offset = offsetDe(m.productId);
        const date = patch.date || m.date;

        if (m.type === 'entrada') {
          if (!(patch.qty > 0)) throw new Error('a quantidade da entrada deve ser maior que zero');
          Object.assign(m, { qty: patch.qty, unitPrice: patch.unitPrice, supplier: patch.supplier, nf: patch.nf });
        } else if (m.type === 'saida') {
          if (!(patch.qty > 0)) throw new Error('a quantidade da saída deve ser maior que zero');
          const dept = state.departments.find((x) => x.id === patch.departmentId);
          if (!dept) throw new Error('departamento não encontrado: ' + patch.departmentId);
          Object.assign(m, { qty: patch.qty, unitPrice: patch.unitPrice, departmentId: dept.id,
            recipient: dept.name, encarregado: dept.encarregado, requester: patch.requester });
        } else {
          m.qty = patch.qtyReal - posicaoAntes(m.productId, date, m.createdAt, m.id, offset).qty;
        }
        m.obs = patch.obs;
        m.date = date;
        recompute(m.productId, offset);
        return JSON.stringify(m);
      }
      case 'delete_movement': {
        const m = state.movements.find((x) => x.id === args.id);
        if (!m) throw new Error('lançamento não encontrado: ' + args.id);
        const offset = offsetDe(m.productId);
        state.movements = state.movements.filter((x) => x.id !== args.id);
        recompute(m.productId, offset);
        return null;
      }
      case 'compute_report': {
        // fixture pré-computada (junho/2026) — não recalcula ao vivo no mock;
        // suficiente para revisar o layout, que é o objetivo deste modo. Só o
        // bloco de limites é recalculado, porque ele depende do limite mensal
        // que o mock deixa editar na tela de Departamentos.
        const gastoPorDepto = {};
        for (const p of report.pedidos || []) {
          const k = String(p.recipient || '').trim().toUpperCase();
          gastoPorDepto[k] = (gastoPorDepto[k] || 0) + p.qty * p.unitPrice;
        }
        const linhas = state.departments.map((d) => {
          const gasto = gastoPorDepto[String(d.name || '').trim().toUpperCase()] || 0;
          const limite = d.monthlyLimit || 0;
          const pct = limite > 0 ? gasto / limite : null;
          return { id: d.id, name: d.name, encarregado: d.encarregado, limite, gasto,
            temLimite: limite > 0, pct, saldo: limite > 0 ? limite - gasto : null,
            status: limite > 0 ? (pct >= 1 ? 'estourado' : pct >= 0.9 ? 'atencao' : 'ok') : 'sem-limite' };
        });
        const comLim = linhas.filter((l) => l.temLimite);
        return JSON.stringify({ ...report, limites: {
          linhas,
          comLimite: comLim.length,
          atencao: linhas.filter((l) => l.status === 'atencao').length,
          estourado: linhas.filter((l) => l.status === 'estourado').length,
          tetoTotal: comLim.reduce((s, l) => s + l.limite, 0),
          gastoTotal: comLim.reduce((s, l) => s + l.gasto, 0),
        } });
      }
      case 'compute_retrospect':
        // fixture pré-computada (2026) — mesma lógica do compute_report acima.
        if (!retrospect) {
          throw new Error('fixture fixtures/retrospect_2026.json ausente — gere com: ' +
            'cargo run -p core-cli -- --db <estoque.db> --retrospect 2026 --dump-fixtures frontend/fixtures');
        }
        return JSON.stringify(retrospect);
      case 'save_budget_params':
      case 'import_dept_cost_history':
        // sem efeito no mock: o retrospecto vem de fixture pré-computada
        return null;
      case 'backup':
        return JSON.stringify(state);
      case 'restore_backup':
        state = JSON.parse(args.payload);
        return null;
      default:
        throw new Error('comando de mock não implementado: ' + cmd);
    }
  });
}
