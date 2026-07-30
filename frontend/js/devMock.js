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

export async function installDevMock() {
  const [backup, report, products, departments] = await Promise.all([
    loadJson('fixtures/backup.json'),
    loadJson('fixtures/report_2026_06.json'),
    loadJson('fixtures/products.json'),
    loadJson('fixtures/departments.json'),
  ]);

  // Estado mutável em memória — CRUD do mock opera sobre isso, então criar/
  // editar/excluir na sessão de revisão visual funciona de verdade (só não
  // persiste entre reloads da página).
  let state = { products, departments, movements: backup.movements };

  function uid(p) { return (p || 'id_') + Math.random().toString(36).slice(2, 10); }

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
        const p = state.products.find((x) => x.id === args.input.productId);
        const baseQty = Math.max(p.qty, 0);
        const novaQty = p.qty + args.input.qty;
        p.avgCost = (baseQty + args.input.qty) > 0 ? (baseQty * p.avgCost + args.input.qty * args.input.unitPrice) / (baseQty + args.input.qty) : 0;
        p.qty = novaQty;
        const m = { id: uid('m_'), type: 'entrada', productId: p.id, qty: args.input.qty, unitPrice: args.input.unitPrice,
          supplier: args.input.supplier, nf: args.input.nf, date: args.input.date, obs: args.input.obs,
          resultingQty: p.qty, resultingAvgCost: p.avgCost, createdAt: args.input.createdAt };
        state.movements.push(m);
        return JSON.stringify(m);
      }
      case 'apply_saida': {
        const p = state.products.find((x) => x.id === args.input.productId);
        const dept = state.departments.find((x) => x.id === args.input.departmentId);
        p.qty -= args.input.qty;
        const m = { id: uid('m_'), type: 'saida', productId: p.id, qty: args.input.qty, unitPrice: p.avgCost,
          departmentId: dept.id, recipient: dept.name, encarregado: dept.encarregado, requester: args.input.requester,
          date: args.input.date, obs: args.input.obs, resultingQty: p.qty, resultingAvgCost: p.avgCost, createdAt: args.input.createdAt };
        state.movements.push(m);
        return JSON.stringify(m);
      }
      case 'apply_correcao': {
        const p = state.products.find((x) => x.id === args.input.productId);
        const delta = args.input.qtyReal - p.qty;
        p.qty = args.input.qtyReal;
        const m = { id: uid('m_'), type: 'ajuste', productId: p.id, qty: delta, unitPrice: p.avgCost,
          obs: args.input.motivo, date: args.input.date, resultingQty: p.qty, resultingAvgCost: p.avgCost, createdAt: args.input.createdAt };
        state.movements.push(m);
        return JSON.stringify(m);
      }
      case 'compute_report':
        // fixture pré-computada (junho/2026) — não recalcula ao vivo no mock;
        // suficiente para revisar o layout, que é o objetivo deste modo.
        return JSON.stringify(report);
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
