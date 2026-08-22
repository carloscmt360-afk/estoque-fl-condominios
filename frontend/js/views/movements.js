import { api } from '../api.js';
import { fmtBRL, fmtNum, fmtDateTimeBR, escapeHtml, agoraLocalArquivo } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';

let movements = [];
let products = [];
let departments = [];

const TIPOS = {
  entrada: { label: 'Entrada', pill: 'pill-ok', sinal: '+' },
  saida: { label: 'Saída', pill: 'pill-accent', sinal: '-' },
  ajuste: { label: 'Correção', pill: 'pill-warn', sinal: '' },
};

/* Busca livre insensível a acento E a caixa: "GERENCIA" acha "GERÊNCIA",
   "pilha" acha "PILHA AA". Sem isso o campo seria inútil justamente nos
   nomes de departamento, que são todos acentuados e em maiúsculas. */
function fold(s) {
  return String(s || '')
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '') // marcas de acento isoladas pelo NFD
    .toLowerCase();
}

export async function initMovements() {
  wireControls();
  await reload();
}

function wireControls() {
  document.getElementById('filtroTimelineBusca').addEventListener('input', render);
  ['filtroTimelineProduto', 'filtroTimelineTipo', 'filtroTimelineDepto', 'filtroTimelineSolicitante']
    .forEach((id) => document.getElementById(id).addEventListener('change', render));

  // Datas: 'change' cobre a escolha pelo calendário e 'input' cobre a
  // digitação direta — qualquer um dos dois invalida o atalho de período que
  // estiver marcado, senão o botão continuaria aceso mostrando outra faixa.
  ['filtroTimelineInicio', 'filtroTimelineFim'].forEach((id) =>
    ['input', 'change'].forEach((ev) =>
      document.getElementById(id).addEventListener(ev, () => { marcarPeriodo(null); render(); })));

  document.querySelectorAll('#filtroTimelinePeriodo button').forEach((b) =>
    b.addEventListener('click', () => aplicarPeriodo(b.dataset.periodo)));

  document.getElementById('btnLimparFiltrosTimeline').addEventListener('click', limparFiltros);
  document.getElementById('btnExportarTimelineCSV').addEventListener('click', exportarCSV);

  document.getElementById('btnSalvarEdicaoMov').addEventListener('click', salvarEdicao);
  enableRowSelection(document.getElementById('timelineTbody'));
  document.getElementById('btnExcluirMov').addEventListener('click', excluirLancamento);
  ['edtMovQtd', 'edtMovPreco', 'edtMovQtdReal', 'edtMovDepartamento'].forEach((id) =>
    document.getElementById(id).addEventListener('input', atualizarInfoEdicao));

  // Os três lançamentos novos reaproveitam os modais da aba Produtos — não
  // faz sentido ter dois formulários de entrada para manter em sincronia.
  document.getElementById('btnMovEntrada').addEventListener('click', () => abrirModalDeProdutos('btnRegistrarEntrada'));
  document.getElementById('btnMovSaida').addEventListener('click', () => abrirModalDeProdutos('btnBaixaProdutos'));
  document.getElementById('btnMovCorrecao').addEventListener('click', () => abrirModalDeProdutos('btnCorrigirEstoque'));

  // Um lançamento novo feito pelos modais da view de Produtos precisa
  // aparecer aqui na hora, sem depender de trocar de aba e voltar.
  document.addEventListener('estoque:dados-alterados', () => {
    if (document.getElementById('view-movements').classList.contains('active')) reload();
  });
}

/* Os modais de novo lançamento (entrada/baixa/correção) moram na view de
   Produtos e são preenchidos pelos handlers dela. Em vez de duplicar esses
   formulários aqui, garantimos que a view esteja inicializada e disparamos o
   clique no botão correspondente — assim os modais abrem já com a lista de
   produtos e departamentos carregada. initProducts é idempotente. */
async function abrirModalDeProdutos(botaoId) {
  const { initProducts } = await import('./products.js');
  await initProducts();
  document.getElementById(botaoId).click();
}

export async function reload() {
  [movements, products, departments] = await Promise.all([
    api.listMovements(),
    api.listProducts(),
    api.listDepartments(),
  ]);
  aplicarPermissoes();
  preencherFiltros();
  render();
}

/* Esconde os botões de lançamento para quem não pode lançar. O backend recusa
   de qualquer forma (ver api.cpp) — isto só evita oferecer e depois negar. */
function aplicarPermissoes() {
  const pode = can('linha_do_tempo', 'create') || can('produtos', 'create');
  ['btnMovEntrada', 'btnMovSaida', 'btnMovCorrecao'].forEach((id) => {
    document.getElementById(id).style.display = pode ? '' : 'none';
  });
}

function getProduct(id) { return products.find((p) => p.id === id); }

function preencherFiltros() {
  const manterSelecao = (sel, opcoes, placeholder) => {
    const atual = sel.value;
    sel.innerHTML = `<option value="">${placeholder}</option>` +
      opcoes.map((o) => `<option value="${escapeHtml(o.value)}">${escapeHtml(o.label)}</option>`).join('');
    sel.value = opcoes.some((o) => o.value === atual) ? atual : '';
  };

  manterSelecao(
    document.getElementById('filtroTimelineProduto'),
    [...products].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR')).map((p) => ({ value: p.id, label: p.name })),
    'Todos os produtos');

  // Departamentos: a lista sai dos LANÇAMENTOS (campo recipient), não do
  // cadastro — assim um departamento já excluído continua filtrável no
  // histórico que ele deixou, que é justamente o que se quer consultar.
  const deptos = [...new Set(movements.map((m) => (m.recipient || '').trim()).filter(Boolean))]
    .sort((a, b) => a.localeCompare(b, 'pt-BR'));
  manterSelecao(document.getElementById('filtroTimelineDepto'),
    deptos.map((d) => ({ value: d, label: d })), 'Todos os departamentos');

  const solicitantes = [...new Set(movements.map((m) => (m.requester || '').trim()).filter(Boolean))]
    .sort((a, b) => a.localeCompare(b, 'pt-BR'));
  manterSelecao(document.getElementById('filtroTimelineSolicitante'),
    solicitantes.map((s) => ({ value: s, label: s })), 'Todos os solicitantes');

  // O datalist do modal de saída também serve o de edição
  document.getElementById('listaSolicitantes').innerHTML =
    solicitantes.map((s) => `<option value="${escapeHtml(s)}"></option>`).join('');
}

/* ------------------------------- Filtros ------------------------------- */

function marcarPeriodo(chave) {
  document.querySelectorAll('#filtroTimelinePeriodo button')
    .forEach((b) => b.classList.toggle('active', b.dataset.periodo === chave));
}

function paraInputDate(d) {
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
}

function aplicarPeriodo(chave) {
  const hoje = new Date();
  let inicio = '';
  let fim = paraInputDate(hoje);
  if (chave === 'tudo') { inicio = ''; fim = ''; }
  else if (chave === 'hoje') inicio = paraInputDate(hoje);
  else if (chave === '7d') inicio = paraInputDate(new Date(hoje.getFullYear(), hoje.getMonth(), hoje.getDate() - 6));
  else if (chave === '30d') inicio = paraInputDate(new Date(hoje.getFullYear(), hoje.getMonth(), hoje.getDate() - 29));
  else if (chave === 'mes') inicio = paraInputDate(new Date(hoje.getFullYear(), hoje.getMonth(), 1));
  else if (chave === 'ano') inicio = paraInputDate(new Date(hoje.getFullYear(), 0, 1));

  document.getElementById('filtroTimelineInicio').value = inicio;
  document.getElementById('filtroTimelineFim').value = fim;
  marcarPeriodo(chave);
  render();
}

function limparFiltros() {
  ['filtroTimelineBusca', 'filtroTimelineProduto', 'filtroTimelineTipo', 'filtroTimelineDepto',
    'filtroTimelineSolicitante', 'filtroTimelineInicio', 'filtroTimelineFim']
    .forEach((id) => { document.getElementById(id).value = ''; });
  marcarPeriodo('tudo');
  render();
}

function filtrar() {
  const busca = fold(document.getElementById('filtroTimelineBusca').value.trim());
  const produto = document.getElementById('filtroTimelineProduto').value;
  const tipo = document.getElementById('filtroTimelineTipo').value;
  const depto = document.getElementById('filtroTimelineDepto').value;
  const solicitante = document.getElementById('filtroTimelineSolicitante').value;
  const inicio = document.getElementById('filtroTimelineInicio').value;
  const fim = document.getElementById('filtroTimelineFim').value;

  let list = [...movements].sort((a, b) => new Date(b.date) - new Date(a.date));
  if (produto) list = list.filter((m) => m.productId === produto);
  if (tipo) list = list.filter((m) => m.type === tipo);
  if (depto) list = list.filter((m) => (m.recipient || '').trim() === depto);
  if (solicitante) list = list.filter((m) => (m.requester || '').trim() === solicitante);
  if (inicio) list = list.filter((m) => new Date(m.date) >= new Date(inicio + 'T00:00:00'));
  if (fim) list = list.filter((m) => new Date(m.date) <= new Date(fim + 'T23:59:59.999'));
  if (busca) {
    // Termos separados por espaço são cumulativos (E), não uma frase: dá
    // para escrever "pilha pastas" e achar as saídas de pilha para PASTAS.
    const termos = busca.split(/\s+/);
    list = list.filter((m) => {
      const p = getProduct(m.productId);
      const alvo = fold([p ? p.sku : '', p ? p.name : '', m.supplier, m.nf, m.recipient, m.encarregado, m.requester, m.obs]
        .filter(Boolean).join(' '));
      return termos.every((t) => alvo.includes(t));
    });
  }
  return list;
}

/* ------------------------------- Render -------------------------------- */

function valorDe(m) { return (m.qty || 0) * (m.unitPrice || 0); }

function render() {
  const list = filtrar();
  renderStatGrid(list);
  renderTabela(list);
}

function renderStatGrid(list) {
  const soma = (tipo) => list.filter((m) => m.type === tipo).reduce((s, m) => s + Math.abs(valorDe(m)), 0);
  const conta = (tipo) => list.filter((m) => m.type === tipo).length;
  const tiles = [
    { label: 'Lançamentos filtrados', value: fmtNum(list.length), foot: `de ${fmtNum(movements.length)} no total` },
    { label: 'Entradas', value: fmtBRL(soma('entrada')), foot: `${fmtNum(conta('entrada'))} lançamento(s)`, cls: 'is-good' },
    { label: 'Saídas', value: fmtBRL(soma('saida')), foot: `${fmtNum(conta('saida'))} lançamento(s)` },
    { label: 'Correções', value: fmtBRL(soma('ajuste')), foot: `${fmtNum(conta('ajuste'))} lançamento(s)`, cls: conta('ajuste') ? 'is-warn' : '' },
  ];
  document.getElementById('movementsStatGrid').innerHTML = tiles.map((t) =>
    `<div class="stat-tile ${t.cls || ''}"><div class="label">${escapeHtml(t.label)}</div>
      <div class="value">${t.value}</div><div class="foot">${escapeHtml(t.foot)}</div></div>`).join('');
}

function detalhesDe(m) {
  if (m.type === 'entrada') {
    return `Fornecedor: <b>${escapeHtml(m.supplier || '-')}</b> · NF: <b>${escapeHtml(m.nf || '-')}</b>` +
      `${m.obs ? ' · ' + escapeHtml(m.obs) : ''}`;
  }
  if (m.type === 'saida') {
    return `Departamento: <b>${escapeHtml(m.recipient || '-')}</b>` +
      `${m.encarregado ? ' · Encarregado: ' + escapeHtml(m.encarregado) : ''}` +
      `${m.requester ? ' · Solicitante: <b>' + escapeHtml(m.requester) + '</b>' : ''}` +
      `${m.obs ? ' · ' + escapeHtml(m.obs) : ''}`;
  }
  return `Motivo: <b>${escapeHtml(m.obs || '-')}</b>`;
}

function renderTabela(list) {
  const tbody = document.getElementById('timelineTbody');
  const rodape = document.getElementById('timelineRodape');

  if (!list.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="9">${movements.length === 0
      ? 'Nenhuma movimentação registrada ainda.'
      : 'Nenhuma movimentação encontrada com esses filtros.'}</td></tr>`;
    rodape.textContent = '';
    return;
  }

  // "Editar" abre o modal que também exclui — basta um dos dois direitos para
  // ele fazer sentido; o modal esconde o botão de excluir por conta própria.
  const podeEditar = can('linha_do_tempo', 'update') || can('linha_do_tempo', 'delete');
  tbody.innerHTML = list.map((m) => {
    const p = getProduct(m.productId);
    const unit = p ? p.unit : '';
    const t = TIPOS[m.type] || TIPOS.ajuste;
    const sinal = m.type === 'ajuste' ? (m.qty >= 0 ? '+' : '') : t.sinal;
    return `<tr>
      <td>${fmtDateTimeBR(m.date)}</td>
      <td><span class="pill ${t.pill}">${t.label}</span></td>
      <td>${escapeHtml(p ? p.sku : '') || '<span class="muted">—</span>'}</td>
      <td>${escapeHtml(p ? p.name : '(produto excluído)')}</td>
      <td class="num">${sinal}${fmtNum(m.qty)} ${escapeHtml(unit)}</td>
      <td>${detalhesDe(m)}</td>
      <td class="num">${fmtBRL(Math.abs(valorDe(m)))}</td>
      <td class="num">${fmtNum(m.resultingQty)} ${escapeHtml(unit)}</td>
      <td><div class="row-actions">${podeEditar
        ? `<button class="btn-sm btn-ghost" data-editar="${m.id}">Editar</button>`
        : '<span class="muted">—</span>'}</div></td>
    </tr>`;
  }).join('');

  tbody.querySelectorAll('[data-editar]')
    .forEach((b) => b.addEventListener('click', () => abrirModalEdicao(b.dataset.editar)));

  rodape.textContent = `${list.length} lançamento(s) exibido(s), do mais recente para o mais antigo.`;
}

/* ------------------------------ Exportar ------------------------------- */

function csvEscape(v) {
  const s = String(v === null || v === undefined ? '' : v);
  return /[";\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
}

function exportarCSV() {
  const list = filtrar();
  if (!list.length) { toast('Não há lançamentos filtrados para exportar.', 'error'); return; }

  const cabecalho = ['Data', 'Tipo', 'SKU', 'Produto', 'Unidade', 'Quantidade', 'Preço unitário', 'Valor',
    'Fornecedor', 'NF', 'Departamento', 'Encarregado', 'Solicitante', 'Observação', 'Saldo após'];
  const linhas = list.map((m) => {
    const p = getProduct(m.productId);
    return [
      fmtDateTimeBR(m.date), (TIPOS[m.type] || TIPOS.ajuste).label, p ? p.sku : '',
      p ? p.name : '(produto excluído)',
      p ? p.unit : '', m.qty, m.unitPrice, valorDe(m),
      m.supplier || '', m.nf || '', m.recipient || '', m.encarregado || '', m.requester || '',
      m.obs || '', m.resultingQty,
    ];
  });

  // Ponto e vírgula + BOM: é o que o Excel em pt-BR abre sem "assistente de
  // importação" e sem quebrar os acentos.
  const csv = '﻿' + [cabecalho, ...linhas].map((l) => l.map(csvEscape).join(';')).join('\r\n');
  const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `movimentacoes_${agoraLocalArquivo()}.csv`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
  toast(`${list.length} lançamento(s) exportado(s).`, 'success');
}

/* --------------------------- Editar / excluir --------------------------- */

function movAtual() { return movements.find((m) => m.id === document.getElementById('edtMovId').value); }

/* Saldo do produto imediatamente antes deste lançamento, reproduzindo a
   linha do tempo do jeito que o backend faz (ordem cronológica real, ajuste
   guardando delta). Serve só para o texto de apoio do formulário — quem
   decide de fato é o C++. */
function saldoAntesDe(mov) {
  const doProduto = movements
    .filter((m) => m.productId === mov.productId)
    .sort((a, b) => (a.date < b.date ? -1 : a.date > b.date ? 1 : (a.createdAt || '').localeCompare(b.createdAt || '')));
  let qty = 0;
  for (const m of doProduto) {
    if (m.id === mov.id) break;
    qty += m.type === 'saida' ? -m.qty : m.qty;
  }
  const p = getProduct(mov.productId);
  // mesma correção de base que o backend aplica: a diferença pré-existente
  // entre o razão e o saldo gravado do produto acompanha a linha inteira
  const totalRazao = doProduto.reduce((s, m) => s + (m.type === 'saida' ? -m.qty : m.qty), 0);
  const offset = p ? p.qty - totalRazao : 0;
  return qty + offset;
}

function abrirModalEdicao(id) {
  const m = movements.find((x) => x.id === id);
  if (!m) return;
  const p = getProduct(m.productId);
  const t = TIPOS[m.type] || TIPOS.ajuste;

  document.getElementById('edtMovId').value = m.id;
  document.getElementById('modalEditarMovTitulo').textContent = `Editar ${t.label.toLowerCase()}`;
  document.getElementById('edtMovCabecalho').innerHTML =
    `<b>${escapeHtml(p ? p.name : '(produto excluído)')}</b> — lançado em ${fmtDateTimeBR(m.date)}.<br>` +
    `O tipo do lançamento e o produto não podem ser alterados; para isso, exclua e registre de novo.`;

  const mostrar = (elId, visivel) => { document.getElementById(elId).style.display = visivel ? '' : 'none'; };
  // Editar e excluir são direitos separados: quem só pode excluir abre este
  // modal para consultar e apagar, sem o botão de salvar, e vice-versa.
  mostrar('btnExcluirMov', can('linha_do_tempo', 'delete'));
  mostrar('btnSalvarEdicaoMov', can('linha_do_tempo', 'update'));
  mostrar('edtMovQtdBox', m.type !== 'ajuste');
  mostrar('edtMovEntradaBox', m.type === 'entrada');
  mostrar('edtMovSaidaBox', m.type === 'saida');
  mostrar('edtMovAjusteBox', m.type === 'ajuste');

  document.getElementById('edtMovQtd').value = m.type === 'ajuste' ? '' : m.qty;
  document.getElementById('edtMovPreco').value = m.unitPrice;
  document.getElementById('edtMovPrecoLabel').textContent =
    m.type === 'entrada' ? 'Preço unitário de compra (R$) *' : 'Preço de valoração registrado (R$) *';
  document.getElementById('edtMovFornecedor').value = m.supplier || '';
  document.getElementById('edtMovNF').value = m.nf || '';
  document.getElementById('edtMovSolicitante').value = m.requester || '';
  document.getElementById('edtMovQtdReal').value = m.type === 'ajuste' ? m.resultingQty : '';
  document.getElementById('edtMovObsLabel').textContent = m.type === 'ajuste' ? 'Motivo do ajuste *' : 'Observações';
  document.getElementById('edtMovObs').value = m.obs || '';

  // datetime-local espera hora LOCAL, e m.date é UTC
  const d = new Date(m.date);
  d.setMinutes(d.getMinutes() - d.getTimezoneOffset());
  document.getElementById('edtMovData').value = d.toISOString().slice(0, 16);

  const selDep = document.getElementById('edtMovDepartamento');
  const ordenados = [...departments].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  selDep.innerHTML = ordenados.length
    ? ordenados.map((dp) => `<option value="${dp.id}">${escapeHtml(dp.name)}</option>`).join('')
    : '<option value="">Nenhum departamento cadastrado</option>';
  // O departamento original pode ter sido excluído do cadastro; nesse caso
  // ele não está na lista e o select cairia no primeiro item sem avisar.
  if (m.departmentId && ordenados.some((dp) => dp.id === m.departmentId)) selDep.value = m.departmentId;
  else if (m.type === 'saida' && m.recipient) {
    selDep.insertAdjacentHTML('afterbegin',
      `<option value="">${escapeHtml(m.recipient)} (departamento excluído — escolha outro)</option>`);
    selDep.value = '';
  }

  atualizarInfoEdicao();
  openModal('modalEditarMov');
}

function atualizarInfoEdicao() {
  const m = movAtual();
  const box = document.getElementById('edtMovInfoBox');
  if (!m) { box.textContent = ''; return; }
  const p = getProduct(m.productId);
  const unit = p ? p.unit : '';
  const antes = saldoAntesDe(m);

  const dep = departments.find((d) => d.id === document.getElementById('edtMovDepartamento').value);
  document.getElementById('edtMovEncarregadoHint').textContent = dep ? 'Encarregado: ' + dep.encarregado : '';

  if (m.type === 'ajuste') {
    const real = parseFloat(document.getElementById('edtMovQtdReal').value);
    const delta = (isNaN(real) ? antes : real) - antes;
    box.innerHTML = `Saldo na data deste ajuste, antes dele: <b>${fmtNum(antes)} ${escapeHtml(unit)}</b><br>` +
      `Ajuste que será gravado: <b>${delta > 0 ? '+' : ''}${fmtNum(delta)} ${escapeHtml(unit)}</b>`;
    return;
  }

  const qtd = parseFloat(document.getElementById('edtMovQtd').value) || 0;
  const preco = parseFloat(document.getElementById('edtMovPreco').value) || 0;
  const depois = m.type === 'entrada' ? antes + qtd : antes - qtd;
  box.innerHTML = `Saldo na data deste lançamento, antes dele: <b>${fmtNum(antes)} ${escapeHtml(unit)}</b><br>` +
    `Saldo logo depois: <b>${fmtNum(depois)} ${escapeHtml(unit)}</b> — valor: <b>${fmtBRL(qtd * preco)}</b><br>` +
    `Os lançamentos posteriores deste produto serão reprocessados.`;
}

async function salvarEdicao() {
  const m = movAtual();
  if (!m) return;

  const dataVal = document.getElementById('edtMovData').value;
  const patch = {
    id: m.id,
    qty: 0,
    qtyReal: 0,
    unitPrice: parseFloat(document.getElementById('edtMovPreco').value) || 0,
    supplier: '',
    nf: '',
    departmentId: '',
    requester: '',
    obs: document.getElementById('edtMovObs').value.trim(),
    date: dataVal ? new Date(dataVal).toISOString() : m.date,
  };

  if (m.type === 'ajuste') {
    const real = parseFloat(document.getElementById('edtMovQtdReal').value);
    if (isNaN(real) || real < 0) { toast('Informe a quantidade contada.', 'error'); return; }
    if (!patch.obs) { toast('Informe o motivo do ajuste.', 'error'); return; }
    patch.qtyReal = real;
  } else {
    const qtd = parseFloat(document.getElementById('edtMovQtd').value);
    if (!qtd || qtd <= 0) { toast('Informe uma quantidade maior que zero.', 'error'); return; }
    if (isNaN(patch.unitPrice) || patch.unitPrice < 0) { toast('Informe um preço unitário válido.', 'error'); return; }
    patch.qty = qtd;
    if (m.type === 'entrada') {
      patch.supplier = document.getElementById('edtMovFornecedor').value.trim();
      patch.nf = document.getElementById('edtMovNF').value.trim();
    } else {
      const depId = document.getElementById('edtMovDepartamento').value;
      if (!depId) { toast('Selecione um departamento.', 'error'); return; }
      patch.departmentId = depId;
      patch.requester = document.getElementById('edtMovSolicitante').value.trim();
    }
  }

  try {
    await api.updateMovement(patch);
    await reload();
    closeModal('modalEditarMov');
    toast('Lançamento atualizado.', 'success');
  } catch (e) {
    toast('Erro ao salvar: ' + e, 'error');
  }
}

async function excluirLancamento() {
  const m = movAtual();
  if (!m) return;
  const p = getProduct(m.productId);
  const t = TIPOS[m.type] || TIPOS.ajuste;
  const nome = p ? p.name : '(produto excluído)';

  // Consequência principal em primeiro lugar: o saldo muda e os lançamentos
  // seguintes são revalorizados. Excluir uma entrada mexe no custo médio.
  const efeito = m.type === 'entrada' ? -m.qty : m.type === 'saida' ? m.qty : -m.qty;
  const saldoFinal = (p ? p.qty : 0) + efeito;
  let aviso = `Excluir esta ${t.label.toLowerCase()} de ${fmtNum(m.qty)} ${p ? p.unit : ''} de "${nome}"?\n\n` +
    `O saldo do produto passa de ${fmtNum(p ? p.qty : 0)} para ${fmtNum(saldoFinal)}, e os lançamentos ` +
    `posteriores deste produto serão reprocessados.`;
  if (saldoFinal < 0) aviso += `\n\nATENÇÃO: o saldo fica NEGATIVO (${fmtNum(saldoFinal)}).`;
  aviso += '\n\nEsta ação não pode ser desfeita.';
  if (!confirm(aviso)) return;

  try {
    await api.deleteMovement(m.id);
    await reload();
    closeModal('modalEditarMov');
    toast('Lançamento excluído.', 'success');
  } catch (e) {
    toast('Erro ao excluir: ' + e, 'error');
  }
}
