import { api } from '../api.js';
import { fmtBRL, fmtNum, escapeHtml, uid, nowIso, nowLocalInputValue } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';

let products = [];
let departments = [];

export async function initProducts() {
  wireControls();
  await reload();
}

function wireControls() {
  document.getElementById('btnNovoProduto').addEventListener('click', () => openProductModal());
  document.getElementById('btnRegistrarEntrada').addEventListener('click', () => openEntradaModal());
  document.getElementById('btnBaixaProdutos').addEventListener('click', () => openSaidaModal());
  document.getElementById('btnCorrigirEstoque').addEventListener('click', () => openCorrecaoModal());
  document.getElementById('filtroProdutoNome').addEventListener('input', renderTable);
  document.getElementById('filtroProdutoStatus').addEventListener('change', renderTable);
  document.getElementById('btnSalvarProduto').addEventListener('click', saveProduct);
  document.getElementById('btnSalvarEntrada').addEventListener('click', saveEntrada);
  document.getElementById('btnSalvarSaida').addEventListener('click', saveSaida);
  document.getElementById('btnSalvarCorrecao').addEventListener('click', saveCorrecao);
  document.querySelectorAll('[data-close]').forEach((b) => b.addEventListener('click', () => closeModal(b.dataset.close)));
  ['entProduto', 'entQtd', 'entPreco'].forEach((id) => document.getElementById(id).addEventListener('input', updateEntradaInfo));
  ['saiProduto', 'saiQtd', 'saiDepartamento'].forEach((id) => document.getElementById(id).addEventListener('input', updateSaidaInfo));
  ['corProduto', 'corQtdReal'].forEach((id) => document.getElementById(id).addEventListener('input', updateCorrecaoInfo));
}

export async function reload() {
  [products, departments] = await Promise.all([api.listProducts(), api.listDepartments()]);
  renderStatGrid();
  renderTable();
}

function renderStatGrid() {
  const valorTotal = products.reduce((s, p) => s + p.qty * p.avgCost, 0);
  const abaixo = products.filter((p) => p.qty <= p.minStock).length;
  document.getElementById('productsStatGrid').innerHTML = [
    { label: 'Produtos cadastrados', value: fmtNum(products.length) },
    { label: 'Valor total em estoque', value: fmtBRL(valorTotal) },
    { label: 'Abaixo do mínimo', value: fmtNum(abaixo), cls: abaixo > 0 ? 'is-critical' : 'is-good' },
    { label: 'Departamentos', value: fmtNum(departments.length) },
  ].map((t) => `<div class="stat-tile ${t.cls || ''}"><div class="label">${escapeHtml(t.label)}</div><div class="value">${t.value}</div></div>`).join('');
}

function renderTable() {
  const nome = (document.getElementById('filtroProdutoNome').value || '').toLowerCase();
  const status = document.getElementById('filtroProdutoStatus').value;
  let list = [...products].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  if (nome) list = list.filter((p) => p.name.toLowerCase().includes(nome));
  if (status === 'low') list = list.filter((p) => p.qty <= p.minStock);
  if (status === 'ok') list = list.filter((p) => p.qty > p.minStock);

  const tbody = document.getElementById('productsTbody');
  if (!list.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="9">${products.length === 0 ? 'Nenhum produto cadastrado.' : 'Nenhum produto com esses filtros.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = list.map((p) => {
    const low = p.qty <= p.minStock;
    return `<tr><td><b>${escapeHtml(p.name)}</b></td><td>${p.category ? escapeHtml(p.category) : '<span class="muted">—</span>'}</td>
      <td>${escapeHtml(p.unit)}</td><td class="num">${fmtNum(p.qty)}</td><td class="num">${fmtNum(p.minStock)}</td>
      <td class="num">${fmtBRL(p.avgCost)}</td><td class="num">${fmtBRL(p.qty * p.avgCost)}</td>
      <td><span class="pill ${low ? 'pill-low' : 'pill-ok'}">${low ? 'Abaixo do mínimo' : 'OK'}</span></td>
      <td><div class="row-actions">
        <button class="btn-sm btn-outline" data-entrada="${p.id}">↓</button>
        <button class="btn-sm btn-outline" data-saida="${p.id}">↑</button>
        <button class="btn-sm btn-ghost" data-editar="${p.id}">Editar</button>
        <button class="btn-sm btn-danger" data-excluir="${p.id}">Excluir</button>
      </div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-entrada]').forEach((b) => b.addEventListener('click', () => openEntradaModal(b.dataset.entrada)));
  tbody.querySelectorAll('[data-saida]').forEach((b) => b.addEventListener('click', () => openSaidaModal(b.dataset.saida)));
  tbody.querySelectorAll('[data-editar]').forEach((b) => b.addEventListener('click', () => openProductModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) => b.addEventListener('click', () => deleteProduct(b.dataset.excluir)));
}

function fillProductSelect(selectId, selectedId) {
  const sel = document.getElementById(selectId);
  const sorted = [...products].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  sel.innerHTML = sorted.map((p) => `<option value="${p.id}">${escapeHtml(p.name)} (${fmtNum(p.qty)} ${escapeHtml(p.unit)})</option>`).join('');
  if (selectedId) sel.value = selectedId;
}
function fillDepartmentSelect(selectId) {
  const sel = document.getElementById(selectId);
  const sorted = [...departments].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  sel.innerHTML = sorted.length
    ? sorted.map((d) => `<option value="${d.id}">${escapeHtml(d.name)}</option>`).join('')
    : '<option value="">Nenhum departamento cadastrado</option>';
}
function fillCategoriasDatalist() {
  const cats = [...new Set(products.map((p) => (p.category || '').trim()).filter(Boolean))].sort((a, b) => a.localeCompare(b, 'pt-BR'));
  document.getElementById('listaCategorias').innerHTML = cats.map((c) => `<option value="${escapeHtml(c)}"></option>`).join('');
}
function fillSolicitantesDatalist() {
  document.getElementById('listaSolicitantes').innerHTML = '';
}

function openProductModal(id) {
  fillCategoriasDatalist();
  document.getElementById('modalProdutoTitulo').textContent = id ? 'Editar Produto' : 'Novo Produto';
  document.getElementById('prodId').value = id || '';
  const inicialBox = document.getElementById('prodInicialBox');
  if (id) {
    const p = products.find((x) => x.id === id);
    document.getElementById('prodNome').value = p.name;
    document.getElementById('prodUnidadeSelect').value = p.unit;
    document.getElementById('prodMinimo').value = p.minStock;
    document.getElementById('prodCategoria').value = p.category || '';
    inicialBox.style.display = 'none';
  } else {
    document.getElementById('prodNome').value = '';
    document.getElementById('prodUnidadeSelect').value = 'Unidade';
    document.getElementById('prodMinimo').value = 0;
    document.getElementById('prodCategoria').value = '';
    document.getElementById('prodQtdInicial').value = 0;
    document.getElementById('prodCustoInicial').value = 0;
    inicialBox.style.display = 'grid';
  }
  openModal('modalProduto');
}

async function saveProduct() {
  const id = document.getElementById('prodId').value;
  const name = document.getElementById('prodNome').value.trim();
  const unit = document.getElementById('prodUnidadeSelect').value;
  const minStock = parseFloat(document.getElementById('prodMinimo').value) || 0;
  const category = document.getElementById('prodCategoria').value.trim();
  if (!name || !unit) { toast('Preencha nome e unidade.', 'error'); return; }
  try {
    if (id) {
      await api.updateProduct({ id, name, unit, minStock, category, qty: 0, avgCost: 0, createdAt: '' });
    } else {
      const newId = uid('p_');
      const created = new Date().toISOString();
      await api.createProduct({ id: newId, name, unit, minStock, category, qty: 0, avgCost: 0, createdAt: created });
      const qtdInicial = parseFloat(document.getElementById('prodQtdInicial').value) || 0;
      const custoInicial = parseFloat(document.getElementById('prodCustoInicial').value) || 0;
      if (qtdInicial > 0) {
        await api.applyEntrada({ movementId: uid('m_'), productId: newId, qty: qtdInicial, unitPrice: custoInicial, supplier: 'Saldo inicial', nf: '-', date: created, obs: 'Cadastro inicial do produto', createdAt: created });
      }
    }
    await reload();
    closeModal('modalProduto');
    toast('Produto salvo com sucesso.', 'success');
  } catch (e) { toast('Erro ao salvar: ' + e, 'error'); }
}

async function deleteProduct(id) {
  const p = products.find((x) => x.id === id);
  if (!p) return;
  if (!confirm(`Excluir "${p.name}"? O histórico de movimentações dele também será removido.`)) return;
  await api.deleteProduct(id);
  await reload();
  toast('Produto excluído.', 'success');
}

function openEntradaModal(productId) {
  fillProductSelect('entProduto', productId);
  document.getElementById('entQtd').value = '';
  document.getElementById('entPreco').value = '';
  document.getElementById('entFornecedor').value = '';
  document.getElementById('entNF').value = '';
  document.getElementById('entObs').value = '';
  document.getElementById('entData').value = nowLocalInputValue();
  updateEntradaInfo();
  openModal('modalEntrada');
}
function updateEntradaInfo() {
  const p = products.find((x) => x.id === document.getElementById('entProduto').value);
  const box = document.getElementById('entInfoBox');
  if (!p) { box.textContent = 'Cadastre um produto primeiro.'; return; }
  const qtd = parseFloat(document.getElementById('entQtd').value) || 0;
  const preco = parseFloat(document.getElementById('entPreco').value) || 0;
  const baseQtd = Math.max(p.qty, 0);
  const novaQtd = p.qty + qtd;
  const novoCusto = (baseQtd + qtd) > 0 ? (baseQtd * p.avgCost + qtd * preco) / (baseQtd + qtd) : 0;
  box.innerHTML = `Estoque atual: <b>${fmtNum(p.qty)} ${p.unit}</b> a ${fmtBRL(p.avgCost)}<br>Após esta entrada: <b>${fmtNum(novaQtd)} ${p.unit}</b> — novo custo médio: <b>${fmtBRL(novoCusto)}</b>`;
}
async function saveEntrada() {
  const p = products.find((x) => x.id === document.getElementById('entProduto').value);
  const qtd = parseFloat(document.getElementById('entQtd').value);
  const preco = parseFloat(document.getElementById('entPreco').value);
  const fornecedor = document.getElementById('entFornecedor').value.trim();
  const nf = document.getElementById('entNF').value.trim();
  const dataVal = document.getElementById('entData').value;
  const obs = document.getElementById('entObs').value.trim();
  if (!p) { toast('Selecione um produto.', 'error'); return; }
  if (!qtd || qtd <= 0) { toast('Informe uma quantidade válida.', 'error'); return; }
  if (isNaN(preco) || preco < 0) { toast('Informe um preço válido.', 'error'); return; }
  if (!fornecedor || !nf) { toast('Informe fornecedor e nota fiscal.', 'error'); return; }
  try {
    const dataISO = dataVal ? new Date(dataVal).toISOString() : nowIso();
    await api.applyEntrada({ movementId: uid('m_'), productId: p.id, qty: qtd, unitPrice: preco, supplier: fornecedor, nf, date: dataISO, obs, createdAt: nowIso() });
    await reload();
    closeModal('modalEntrada');
    toast('Entrada registrada.', 'success');
  } catch (e) { toast('Erro: ' + e, 'error'); }
}

function openSaidaModal(productId) {
  fillProductSelect('saiProduto', productId);
  fillDepartmentSelect('saiDepartamento');
  fillSolicitantesDatalist();
  document.getElementById('saiQtd').value = '';
  document.getElementById('saiObs').value = '';
  document.getElementById('saiSolicitante').value = '';
  document.getElementById('saiData').value = nowLocalInputValue();
  updateSaidaInfo();
  openModal('modalSaida');
}
function updateSaidaInfo() {
  const p = products.find((x) => x.id === document.getElementById('saiProduto').value);
  const dept = departments.find((x) => x.id === document.getElementById('saiDepartamento').value);
  document.getElementById('saiEncarregadoHint').textContent = dept ? 'Encarregado: ' + dept.encarregado : '';
  const box = document.getElementById('saiInfoBox');
  if (!p) { box.textContent = 'Cadastre um produto primeiro.'; return; }
  const qtd = parseFloat(document.getElementById('saiQtd').value) || 0;
  box.innerHTML = `Estoque disponível: <b>${fmtNum(p.qty)} ${p.unit}</b><br>Após esta baixa: <b>${fmtNum(p.qty - qtd)} ${p.unit}</b> — valor: <b>${fmtBRL(qtd * p.avgCost)}</b>`;
}
async function saveSaida() {
  const p = products.find((x) => x.id === document.getElementById('saiProduto').value);
  const qtd = parseFloat(document.getElementById('saiQtd').value);
  const dept = departments.find((x) => x.id === document.getElementById('saiDepartamento').value);
  const dataVal = document.getElementById('saiData').value;
  const obs = document.getElementById('saiObs').value.trim();
  const solicitante = document.getElementById('saiSolicitante').value.trim();
  if (!p) { toast('Selecione um produto.', 'error'); return; }
  if (!qtd || qtd <= 0) { toast('Informe uma quantidade válida.', 'error'); return; }
  if (!dept) { toast('Selecione um departamento.', 'error'); return; }
  if (qtd > p.qty) { toast(`Quantidade maior que o disponível (${fmtNum(p.qty)} ${p.unit}).`, 'error'); return; }
  try {
    const dataISO = dataVal ? new Date(dataVal).toISOString() : nowIso();
    await api.applySaida({ movementId: uid('m_'), productId: p.id, qty: qtd, departmentId: dept.id, date: dataISO, obs, requester: solicitante, createdAt: nowIso() });
    await reload();
    closeModal('modalSaida');
    toast('Baixa registrada.', 'success');
  } catch (e) { toast('Erro: ' + e, 'error'); }
}

function openCorrecaoModal(productId) {
  fillProductSelect('corProduto', productId);
  document.getElementById('corMotivo').value = '';
  document.getElementById('corData').value = nowLocalInputValue();
  const p = products.find((x) => x.id === document.getElementById('corProduto').value);
  document.getElementById('corQtdReal').value = p ? p.qty : 0;
  updateCorrecaoInfo();
  openModal('modalCorrecao');
}
function updateCorrecaoInfo() {
  const p = products.find((x) => x.id === document.getElementById('corProduto').value);
  const box = document.getElementById('corInfoBox');
  if (!p) { box.textContent = 'Cadastre um produto primeiro.'; return; }
  const qtdReal = parseFloat(document.getElementById('corQtdReal').value);
  const delta = (isNaN(qtdReal) ? 0 : qtdReal) - p.qty;
  box.innerHTML = `Estoque no sistema: <b>${fmtNum(p.qty)} ${p.unit}</b><br>Ajuste: <b>${delta > 0 ? '+' : ''}${fmtNum(delta)} ${p.unit}</b> (custo médio permanece ${fmtBRL(p.avgCost)})`;
}
async function saveCorrecao() {
  const p = products.find((x) => x.id === document.getElementById('corProduto').value);
  const qtdReal = parseFloat(document.getElementById('corQtdReal').value);
  const motivo = document.getElementById('corMotivo').value.trim();
  const dataVal = document.getElementById('corData').value;
  if (!p) { toast('Selecione um produto.', 'error'); return; }
  if (isNaN(qtdReal) || qtdReal < 0) { toast('Informe a quantidade contada.', 'error'); return; }
  if (!motivo) { toast('Informe o motivo.', 'error'); return; }
  try {
    const dataISO = dataVal ? new Date(dataVal).toISOString() : nowIso();
    await api.applyCorrecao({ movementId: uid('m_'), productId: p.id, qtyReal: qtdReal, motivo, date: dataISO, createdAt: nowIso() });
    await reload();
    closeModal('modalCorrecao');
    toast('Estoque corrigido.', 'success');
  } catch (e) { toast('Erro: ' + e, 'error'); }
}

export { products as productsCache };
