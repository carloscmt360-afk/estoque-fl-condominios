import { api } from '../api.js';
import { fmtNum, fmtDateTimeBR, escapeHtml } from '../format.js';

let movements = [];
let products = [];

export async function initMovements() {
  document.getElementById('filtroTimelineProduto').addEventListener('change', render);
  document.getElementById('filtroTimelineTipo').addEventListener('change', render);
  await reload();
}

export async function reload() {
  const [backup, prods] = await Promise.all([api.backup(), api.listProducts()]);
  movements = backup.movements;
  products = prods;
  fillProductFilter();
  render();
}

function fillProductFilter() {
  const sel = document.getElementById('filtroTimelineProduto');
  const current = sel.value;
  const sorted = [...products].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  sel.innerHTML = '<option value="">Todos os produtos</option>' + sorted.map((p) => `<option value="${p.id}">${escapeHtml(p.name)}</option>`).join('');
  sel.value = current;
}

function getProduct(id) { return products.find((p) => p.id === id); }

function render() {
  const prodFiltro = document.getElementById('filtroTimelineProduto').value;
  const tipoFiltro = document.getElementById('filtroTimelineTipo').value;
  let list = [...movements].sort((a, b) => new Date(b.date) - new Date(a.date));
  if (prodFiltro) list = list.filter((m) => m.productId === prodFiltro);
  if (tipoFiltro) list = list.filter((m) => m.type === tipoFiltro);

  const tbody = document.getElementById('timelineTbody');
  if (!list.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="6">Nenhuma movimentação encontrada.</td></tr>';
    return;
  }
  tbody.innerHTML = list.map((m) => {
    const p = getProduct(m.productId);
    const prodName = p ? p.name : '(produto excluído)';
    const unit = p ? p.unit : '';
    let tipoLabel, pillCls, detalhes, qtdLabel;
    if (m.type === 'entrada') {
      tipoLabel = 'Entrada'; pillCls = 'pill-ok';
      detalhes = `Fornecedor: <b>${escapeHtml(m.supplier || '-')}</b> · NF: <b>${escapeHtml(m.nf || '-')}</b>${m.obs ? ' · ' + escapeHtml(m.obs) : ''}`;
      qtdLabel = '+' + fmtNum(m.qty);
    } else if (m.type === 'saida') {
      tipoLabel = 'Saída'; pillCls = 'pill-low';
      detalhes = `Departamento: <b>${escapeHtml(m.recipient || '-')}</b>${m.requester ? ' · Solicitante: <b>' + escapeHtml(m.requester) + '</b>' : ''}${m.obs ? ' · ' + escapeHtml(m.obs) : ''}`;
      qtdLabel = '-' + fmtNum(m.qty);
    } else {
      tipoLabel = 'Correção'; pillCls = 'pill-ok';
      detalhes = `Motivo: <b>${escapeHtml(m.obs || '-')}</b>`;
      qtdLabel = (m.qty >= 0 ? '+' : '') + fmtNum(m.qty);
    }
    return `<tr><td>${fmtDateTimeBR(m.date)}</td><td><span class="pill ${pillCls}">${tipoLabel}</span></td>
      <td>${escapeHtml(prodName)}</td><td class="num">${qtdLabel} ${escapeHtml(unit)}</td>
      <td>${detalhes}</td><td class="num">${fmtNum(m.resultingQty)} ${escapeHtml(unit)}</td></tr>`;
  }).join('');
}
