import { api } from '../api.js';
import { installTooltip, legendHTML, VIZ } from '../charts/palette.js';
import { drawSpark } from '../charts/sparkline.js';
import { drawConsumoAnual } from '../charts/groupedBars.js';
import { drawValorEstoque } from '../charts/stockValueLine.js';
import { drawBarrasH } from '../charts/deptHBars.js';
import { drawDumbbell } from '../charts/dumbbell.js';
import { drawABC } from '../charts/abcStacked.js';
import { fmtBRL, fmtNum, fmtPct, fmtDateBR, deltaHTML, deltaCell, escapeHtml, meterHTML } from '../format.js';
import { toast } from '../components/toast.js';
import { printDocument, buildEstoqueDoc, buildAnaliticoDoc, buildCustoDeptoDoc } from '../print.js';

const MESES = ['Janeiro','Fevereiro','Março','Abril','Maio','Junho','Julho','Agosto','Setembro','Outubro','Novembro','Dezembro'];
const MESES_ABR = ['Jan','Fev','Mar','Abr','Mai','Jun','Jul','Ago','Set','Out','Nov','Dez'];

let repRef = { y: null, m: null };
let repData = null;
let controlsPopulated = false;

export async function initDashboard() {
  installTooltip();
  wireStaticControls();
  await ensureControlsPopulated();
  await renderReport();
}

function wireStaticControls() {
  ['repMes', 'repAno', 'repDepto', 'repJanela'].forEach((id) =>
    document.getElementById(id).addEventListener('change', renderReport));
  document.getElementById('btnMesAtual').addEventListener('click', goToCurrentMonth);
  document.getElementById('btnExportarRelatorio').addEventListener('click', exportReportCSV);
  document.getElementById('btnImprimirEstoque').addEventListener('click', () => imprimir(buildEstoqueDoc));
  document.getElementById('btnImprimirAnalitico').addEventListener('click', () => imprimir(buildAnaliticoDoc));
  document.getElementById('btnImprimirCustoDepto').addEventListener('click', () => imprimir(buildCustoDeptoDoc));
  document.getElementById('repLimiteSoConfig').addEventListener('change', renderLimites);
  document.getElementById('repFiltroProduto').addEventListener('input', renderPosicao);
  document.getElementById('repFiltroClasse').addEventListener('change', renderPosicao);
  document.getElementById('repFiltroSituacao').addEventListener('change', renderPosicao);
  document.getElementById('repFiltroPedido').addEventListener('input', renderPedidos);
  document.querySelectorAll('#view-dashboard [data-toggle-table]').forEach((btn) =>
    btn.addEventListener('click', () => toggleCardTable(btn.dataset.toggleTable)));
}

/* Popula mês/ano uma única vez — recriar as opções a cada render apagaria a
   escolha do usuário no meio de uma sessão (bug já corrigido na v. web). */
async function ensureControlsPopulated() {
  const selM = document.getElementById('repMes');
  const selA = document.getElementById('repAno');
  const hoje = new Date();
  if (repRef.y === null) { repRef.y = hoje.getFullYear(); repRef.m = hoje.getMonth(); }

  if (selM.options.length === 0) {
    selM.innerHTML = MESES.map((n, i) => `<option value="${i}">${n}</option>`).join('');
    selM.value = repRef.m;
  }
  if (!controlsPopulated) {
    const anos = new Set([hoje.getFullYear(), hoje.getFullYear() - 1, hoje.getFullYear() + 1]);
    selA.innerHTML = [...anos].sort((a, b) => b - a).map((a) => `<option value="${a}">${a}</option>`).join('');
    selA.value = repRef.y;
    controlsPopulated = true;
  }

  const selD = document.getElementById('repDepto');
  const atual = selD.value;
  const departamentos = await api.listDepartments();
  selD.innerHTML = '<option value="">Todos os departamentos</option>' +
    departamentos.map((d) => `<option value="${escapeHtml(d.name)}">${escapeHtml(d.name)}</option>`).join('');
  if (atual) selD.value = atual;
}

async function goToCurrentMonth() {
  const d = new Date();
  repRef = { y: d.getFullYear(), m: d.getMonth() };
  await ensureControlsPopulated();
  document.getElementById('repMes').value = repRef.m;
  document.getElementById('repAno').value = repRef.y;
  await renderReport();
}

function toggleCardTable(id) {
  const card = document.getElementById(id);
  const svg = card.querySelector('.chart-host');
  const tbl = card.querySelector('.tbl-view');
  const legend = card.querySelector('.legend');
  const showingTable = tbl.style.display !== 'none';
  tbl.style.display = showingTable ? 'none' : 'block';
  svg.style.display = showingTable ? 'block' : 'none';
  if (legend) legend.style.display = showingTable ? 'flex' : 'none';
  card.querySelector('[data-toggle-table]').textContent = showingTable ? 'Ver tabela' : 'Ver gráfico';
}

export async function renderReport() {
  const panel = document.getElementById('view-dashboard');
  if (!panel.classList.contains('active')) return;

  repRef.y = parseInt(document.getElementById('repAno').value, 10);
  repRef.m = parseInt(document.getElementById('repMes').value, 10);
  const deptFilter = document.getElementById('repDepto').value;
  const janela = parseInt(document.getElementById('repJanela').value, 10);

  let R;
  try {
    R = repData = await api.computeReport({
      year: repRef.y, month0: repRef.m, deptFilter, windowMonths: janela, nowIso: new Date().toISOString(),
    });
  } catch (e) {
    toast('Erro ao gerar relatório: ' + e, 'error');
    return;
  }

  const mesNome = MESES[R.m], ano = R.y;
  panel.querySelectorAll('.ry').forEach((e) => (e.textContent = ano));
  panel.querySelectorAll('.ry-1').forEach((e) => (e.textContent = ano - 1));
  panel.querySelectorAll('.rmes').forEach((e) => (e.textContent = mesNome + '/' + ano));

  const nota = document.getElementById('repScopeNote');
  const avisos = [];
  if (R.refFuturo) avisos.push(`<b>${mesNome}/${ano} ainda não começou.</b> Sem base de comparação para este período.`);
  else if (R.refEmCurso) avisos.push(`<b>Mês em curso:</b> ${R.diasDecorridos} de ${R.diasNoMes} dias decorridos.`);
  if (R.historicoParcial) avisos.push(`<b>Histórico curto:</b> ${R.mesesHistorico} ${R.mesesHistorico === 1 ? 'mês' : 'meses'} de dados até ${mesNome}/${ano}, menos que a janela de ${R.janela} meses.`);
  if (R.deptFilter) avisos.push(`<b>Filtro ativo: ${escapeHtml(R.deptFilter)}.</b> Estoque e ABC continuam gerais.`);
  if (avisos.length) {
    nota.style.display = 'block';
    nota.className = R.refFuturo ? 'warn-box' : 'info-box';
    nota.innerHTML = avisos.map((a) => `<div>${a}</div>`).join('<div style="height:6px"></div>');
  } else nota.style.display = 'none';

  renderLimiteAvisos(R);
  renderKPIs(R);
  renderCharts(R);
  renderLimites();
  renderAlertas(R);
  renderPosicao();
  renderDeptos(R);
  renderPedidos();
}

function imprimir(builder) {
  if (!repData) { toast('Abra o relatório antes de imprimir.', 'error'); return; }
  const soComEstoque = document.getElementById('repImprimirSoComEstoque').checked;
  printDocument(builder(repData, { soComEstoque }));
}

/* Avisos de limite mensal. Não têm botão de fechar: enquanto um setor estiver
   em 90% ou tiver estourado o teto, o aviso continua na tela a cada abertura
   do relatório. Os dois níveis aparecem juntos quando existem os dois. */
function renderLimiteAvisos(R) {
  const host = document.getElementById('repLimiteAvisos');
  const lim = R.limites || { linhas: [] };
  const mesRef = `${MESES[R.m].toLowerCase()}/${R.y}`;
  const estourados = lim.linhas.filter((l) => l.status === 'estourado').sort((a, b) => b.pct - a.pct);
  const atencao = lim.linhas.filter((l) => l.status === 'atencao').sort((a, b) => b.pct - a.pct);

  const detalhe = (l) => `<b>${escapeHtml(l.name)}</b> ${fmtBRL(l.gasto)} de ${fmtBRL(l.limite)} (${fmtPct(l.pct, 0)})`;
  const blocos = [];
  if (estourados.length) {
    blocos.push(`<div class="danger-box" style="margin-bottom:8px;">
      ⛔ <b>Limite mensal estourado em ${estourados.length} departamento(s)</b> — ${mesRef}:
      ${estourados.map(detalhe).join(' · ')}.
      <div style="margin-top:4px;">Excesso total: <b>${fmtBRL(estourados.reduce((s, l) => s + (l.gasto - l.limite), 0))}</b>.</div>
    </div>`);
  }
  if (atencao.length) {
    blocos.push(`<div class="warn-box" style="margin-bottom:8px;">
      ⚠ <b>Limite mensal em 90% ou mais</b> — ${mesRef}: ${atencao.map(detalhe).join(' · ')}.
    </div>`);
  }
  host.innerHTML = blocos.join('');
}

function renderLimites() {
  const R = repData;
  if (!R) return;
  const lim = R.limites || { linhas: [], comLimite: 0, tetoTotal: 0, gastoTotal: 0 };
  const soConfig = document.getElementById('repLimiteSoConfig').checked;
  const tbody = document.getElementById('repLimitesTbody');
  const tfoot = document.getElementById('repLimitesTfoot');

  const linhas = lim.linhas
    .filter((l) => !soConfig || l.temLimite)
    .sort((a, b) => (b.pct === null ? -1 : b.pct) - (a.pct === null ? -1 : a.pct) || a.name.localeCompare(b.name, 'pt-BR'));

  if (!linhas.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="7">${lim.linhas.length
      ? 'Nenhum departamento tem limite mensal definido. Cadastre em Departamentos, ou desmarque o filtro acima.'
      : 'Nenhum departamento cadastrado.'}</td></tr>`;
    tfoot.innerHTML = '';
    return;
  }
  const sit = {
    estourado: { cls: 'flag-critical', ico: '⛔', txt: 'Estourou' },
    atencao: { cls: 'flag-warn', ico: '⚠', txt: 'Atenção' },
    ok: { cls: 'flag-good', ico: '✓', txt: 'Dentro do limite' },
  };
  tbody.innerHTML = linhas.map((l) => {
    const s = sit[l.status];
    return `<tr><td><b>${escapeHtml(l.name)}</b></td><td>${escapeHtml(l.encarregado || '—')}</td>
      <td class="num">${l.temLimite ? fmtBRL(l.limite) : '<span class="muted">sem limite</span>'}</td>
      <td class="num">${fmtBRL(l.gasto)}</td>
      <td class="num">${l.temLimite
        ? `<span class="${l.saldo < 0 ? 'flag flag-critical' : ''}">${fmtBRL(l.saldo)}</span>`
        : '<span class="muted">—</span>'}</td>
      <td>${meterHTML(l.pct)}</td>
      <td>${s ? `<span class="flag ${s.cls}">${s.ico} ${s.txt}</span>` : '<span class="muted">não controlado</span>'}</td></tr>`;
  }).join('');

  const pct = lim.tetoTotal > 0 ? lim.gastoTotal / lim.tetoTotal : null;
  tfoot.innerHTML = `<tr><td colspan="2">TOTAL — ${fmtNum(lim.comLimite)} departamento(s) com limite</td>
    <td class="num">${fmtBRL(lim.tetoTotal)}</td><td class="num">${fmtBRL(lim.gastoTotal)}</td>
    <td class="num">${fmtBRL(lim.tetoTotal - lim.gastoTotal)}</td><td>${meterHTML(pct)}</td><td></td></tr>`;
}

function renderKPIs(R) {
  const k = R.kpi;
  document.getElementById('repHeroValor').textContent = fmtBRL(k.consumo);
  document.getElementById('repHeroDeltaMes').innerHTML = deltaHTML(k.consumo, k.consumoAnt, false, 'vs mês anterior');
  document.getElementById('repHeroDeltaAno').innerHTML = deltaHTML(k.consumo, k.consumoAnoAnt, false, `vs ${MESES_ABR[R.m].toLowerCase()}/${R.y - 1}`);
  drawSpark(document.getElementById('repSpark'), R.serie12);

  const tiles = [
    { label: 'Valor em estoque', value: fmtBRL(k.valorEstoque), foot: deltaHTML(k.valorEstoque, k.valorEstoqueAnt, null, 'vs mês anterior'), help: 'Saldo × custo médio na posição de fechamento.' },
    { label: 'Compras no mês', value: fmtBRL(k.compras), foot: deltaHTML(k.compras, k.comprasAnt, null, 'vs mês anterior'), help: 'Entradas valorizadas pelo preço da nota.' },
    { label: 'Pedidos atendidos', value: fmtNum(k.pedidos), foot: `${fmtNum(k.itensDistintos)} materiais distintos`, help: 'Cada saída registrada no período.' },
    { label: 'Ticket médio', value: fmtBRL(k.ticket), foot: 'Consumo ÷ pedidos', help: 'Valor médio de cada retirada.' },
    { label: 'Itens em ruptura', value: fmtNum(k.ruptura), cls: k.ruptura > 0 ? 'is-critical' : 'is-good', foot: `${fmtNum(k.baixo)} abaixo do mínimo`, help: 'Saldo zerado ou negativo.' },
    { label: 'Capital parado', value: fmtBRL(k.paradoValor), cls: k.valorEstoque > 0 && k.paradoValor / k.valorEstoque > 0.3 ? 'is-warn' : '', foot: `${fmtNum(k.paradoItens.length)} itens sem consumo`, help: 'Valor sem nenhuma saída na janela.' },
    { label: 'Cobertura', value: k.cobertura === null ? 'n/d' : fmtNum(Math.round(k.cobertura * 10) / 10) + ' meses', cls: k.cobertura !== null && k.cobertura > 12 ? 'is-warn' : '', foot: k.cobertura === null ? 'sem consumo na janela' : `ritmo dos últimos ${R.janela} meses`, help: 'Estoque ÷ consumo médio mensal.' },
    { label: 'Giro anualizado', value: k.giro === null ? 'n/d' : fmtNum(Math.round(k.giro * 100) / 100) + '×', foot: 'consumo 12m ÷ estoque médio', help: 'Quantas vezes o estoque se renovou no ano.' },
    { label: 'Acuracidade', value: k.acuracidade === null ? 'n/d' : fmtPct(k.acuracidade, 1), cls: k.acuracidade !== null ? (k.acuracidade >= 0.98 ? 'is-good' : k.acuracidade >= 0.95 ? 'is-warn' : 'is-critical') : '', foot: `${fmtNum(k.ajustes.length)} ajuste(s) no mês`, help: '100% menos participação dos ajustes.' },
  ];
  document.getElementById('repTiles').innerHTML = tiles.map((t) => `
    <div class="stat-tile ${t.cls || ''}">
      <div class="label">${escapeHtml(t.label)}<span class="help" title="${escapeHtml(t.help)}">i</span></div>
      <div class="value">${t.value === 'n/d' ? '<span class="muted">n/d</span>' : escapeHtml(String(t.value))}</div>
      <div class="foot">${t.foot}</div>
    </div>`).join('');
}

function renderCharts(R) {
  document.getElementById('legConsumoAnual').innerHTML = legendHTML([{ label: String(R.y), color: VIZ.s1 }, { label: String(R.y - 1), color: VIZ.s2 }]);
  drawConsumoAnual(document.getElementById('chartConsumoAnual'), R.anual, R.y, R.m);
  document.getElementById('tblConsumoAnual').innerHTML = `<table><thead><tr><th>Mês</th><th class="num">${R.y}</th><th class="num">${R.y - 1}</th><th class="num">Δ %</th></tr></thead>
    <tbody>${R.anual.map((a) => `<tr><td>${MESES[a.mes]}</td><td class="num">${a.futuro ? '<span class="muted">—</span>' : fmtBRL(a.ref)}</td>
      <td class="num">${fmtBRL(a.ant)}</td><td class="num">${a.futuro ? '<span class="muted">—</span>' : deltaCell(a.ref || 0, a.ant)}</td></tr>`).join('')}</tbody></table>`;

  drawValorEstoque(document.getElementById('chartValorEstoque'), R.serie12);
  document.getElementById('tblValorEstoque').innerHTML = `<table><thead><tr><th>Mês</th><th class="num">Estoque</th><th class="num">Consumo</th></tr></thead>
    <tbody>${R.serie12.map((s) => `<tr><td>${MESES[s.m]}/${s.y}</td><td class="num">${fmtBRL(s.valorEstoque)}</td><td class="num">${fmtBRL(s.consumo)}</td></tr>`).join('')}</tbody></table>`;

  const mesRows = Object.entries(R.deptMes).map(([label, value]) => ({ label, value })).sort((a, b) => b.value - a.value);
  const totMes = mesRows.reduce((s, r) => s + r.value, 0);
  drawBarrasH(document.getElementById('chartDeptoMes'), mesRows, { tipLabel: 'Consumo no mês', aria: 'Custo por departamento', empty: 'Nenhuma saída no mês de referência.' });
  document.getElementById('tblDeptoMes').innerHTML = mesRows.length
    ? `<table><thead><tr><th>Departamento</th><th class="num">Consumo</th><th class="num">%</th></tr></thead>
       <tbody>${mesRows.map((r) => `<tr><td>${escapeHtml(r.label)}</td><td class="num">${fmtBRL(r.value)}</td><td class="num">${totMes > 0 ? fmtPct(r.value / totMes, 1) : '—'}</td></tr>`).join('')}</tbody>
       <tfoot><tr><td>TOTAL</td><td class="num">${fmtBRL(totMes)}</td><td class="num">100%</td></tr></tfoot></table>`
    : '<div class="chart-empty">Nenhuma saída no mês de referência.</div>';

  const ytdRows = Object.entries(R.deptYTD).map(([label, e]) => ({ label, cur: e.cur, prev: e.prev })).filter((r) => r.cur > 0 || r.prev > 0).sort((a, b) => Math.max(b.cur, b.prev) - Math.max(a.cur, a.prev));
  document.getElementById('legDeptoYTD').innerHTML = legendHTML([{ label: 'Ano atual', color: VIZ.cur }, { label: 'Ano anterior', color: VIZ.prior }]);
  drawDumbbell(document.getElementById('chartDeptoYTD'), ytdRows, { curLabel: `${R.y} (parcial)`, prevLabel: `${R.y - 1} (mesmo período)` });
  document.getElementById('tblDeptoYTD').innerHTML = ytdRows.length
    ? `<table><thead><tr><th>Departamento</th><th class="num">${R.y}</th><th class="num">${R.y - 1}</th><th class="num">Δ %</th></tr></thead>
       <tbody>${ytdRows.map((r) => `<tr><td>${escapeHtml(r.label)}</td><td class="num">${fmtBRL(r.cur)}</td><td class="num">${fmtBRL(r.prev)}</td><td class="num">${deltaCell(r.cur, r.prev)}</td></tr>`).join('')}</tbody>
       <tfoot><tr><td>TOTAL</td><td class="num">${fmtBRL(R.ytdCur)}</td><td class="num">${fmtBRL(R.ytdPrev)}</td><td class="num">${deltaCell(R.ytdCur, R.ytdPrev)}</td></tr></tfoot></table>`
    : '<div class="chart-empty">Sem acumulado comparável.</div>';

  document.getElementById('legABC').innerHTML = legendHTML([{ label: 'Classe A', color: VIZ.abc.A }, { label: 'Classe B', color: VIZ.abc.B }, { label: 'Classe C', color: VIZ.abc.C }]);
  const totalItensValorados = R.abc.A.n + R.abc.B.n + R.abc.C.n;
  const abcValor = R.abc.A.v + R.abc.B.v + R.abc.C.v;
  drawABC(document.getElementById('chartABC'), R.abc, abcValor, totalItensValorados);
  document.getElementById('tblABC').innerHTML = `<table><thead><tr><th>Classe</th><th class="num">Itens</th><th class="num">Valor</th><th class="num">%</th></tr></thead>
    <tbody>${['A', 'B', 'C'].map((c) => `<tr><td><span class="abc-pill abc-${c}">${c}</span></td><td class="num">${fmtNum(R.abc[c].n)}</td><td class="num">${fmtBRL(R.abc[c].v)}</td><td class="num">${abcValor > 0 ? fmtPct(R.abc[c].v / abcValor, 1) : '—'}</td></tr>`).join('')}</tbody></table>`;

  const topRows = R.itens.filter((i) => i.consumoRef.val > 0).sort((a, b) => b.consumoRef.val - a.consumoRef.val).slice(0, 10)
    .map((i) => ({ label: i.name, value: i.consumoRef.val, tipExtra: `<br>Qtd.: ${fmtNum(i.consumoRef.qty)} ${escapeHtml(i.unit)}` }));
  drawBarrasH(document.getElementById('chartTopConsumo'), topRows, { tipLabel: 'Consumo', aria: 'Materiais mais consumidos', empty: 'Nenhum material consumido no período.' });
  document.getElementById('tblTopConsumo').innerHTML = topRows.length
    ? `<table><thead><tr><th>Material</th><th class="num">Valor</th></tr></thead><tbody>${topRows.map((r) => `<tr><td>${escapeHtml(r.label)}</td><td class="num">${fmtBRL(r.value)}</td></tr>`).join('')}</tbody></table>`
    : '<div class="chart-empty">Nenhum material consumido.</div>';
}

function renderAlertas(R) {
  const k = R.kpi;
  const nomes = (arr) => arr.length ? escapeHtml(arr.slice(0, 4).map((i) => i.name).join(', ')) + (arr.length > 4 ? ` e mais ${arr.length - 4}` : '') : '—';
  const ruptura = R.itens.filter((i) => i.situacao === 'ruptura');
  const baixo = R.itens.filter((i) => i.situacao === 'baixo');
  const curto = R.itens.filter((i) => i.cobertura !== null && i.cobertura < 1 && i.qty > 0);
  const diverg = R.itens.filter((i) => i.divergencia !== null);
  const negativo = R.itens.filter((i) => i.qty < 0);
  const semSolic = R.pedidos.filter((p) => !(p.requester || '').trim());

  const lim = R.limites || { linhas: [] };
  const limEstourado = lim.linhas.filter((l) => l.status === 'estourado');
  const limAtencao = lim.linhas.filter((l) => l.status === 'atencao');
  const nomesDepto = (arr) => arr.length
    ? escapeHtml(arr.slice(0, 4).map((l) => l.name).join(', ')) + (arr.length > 4 ? ` e mais ${arr.length - 4}` : '')
    : '—';

  const linhas = [
    { sev: 'critical', nome: 'Limite mensal estourado', itens: limEstourado.length,
      valor: limEstourado.reduce((s, l) => s + (l.gasto - l.limite), 0),
      det: limEstourado.length ? 'Gasto acima do teto combinado: ' + nomesDepto(limEstourado) : 'Nenhum setor passou do limite.' },
    { sev: 'warn', nome: 'Limite mensal em 90% ou mais', itens: limAtencao.length,
      valor: limAtencao.reduce((s, l) => s + l.gasto, 0),
      det: limAtencao.length ? 'Perto do teto do mês: ' + nomesDepto(limAtencao) : 'Nenhum setor perto do limite.' },
    { sev: 'critical', nome: 'Ruptura de estoque', itens: ruptura.length, valor: null, det: 'Saldo zerado ou negativo: ' + nomes(ruptura) },
    { sev: 'critical', nome: 'Saldo negativo (erro de lançamento)', itens: negativo.length, valor: null, det: negativo.length ? 'Use "Corrigir Estoque": ' + nomes(negativo) : 'Nenhum saldo negativo.' },
    { sev: 'warn', nome: 'Abaixo do estoque mínimo', itens: baixo.length, valor: baixo.reduce((s, i) => s + i.valor, 0), det: 'Repor antes da ruptura: ' + nomes(baixo) },
    { sev: 'warn', nome: 'Cobertura menor que 1 mês', itens: curto.length, valor: curto.reduce((s, i) => s + i.valor, 0), det: 'Acaba antes do próximo fechamento: ' + nomes(curto) },
    { sev: 'serious', nome: `Capital parado (sem consumo em ${R.janela} meses)`, itens: k.paradoItens.length, valor: k.paradoValor, det: 'Avaliar redistribuição ou baixa: ' + nomes(k.paradoItens) },
    { sev: 'serious', nome: 'Custo médio distante do preço praticado', itens: diverg.length, valor: diverg.reduce((s, i) => s + i.valor, 0), det: diverg.length ? 'Conferir cadastro/nota: ' + nomes(diverg) : 'Nenhuma divergência.' },
    { sev: 'warn', nome: 'Ajustes de inventário no mês', itens: k.ajustes.length, valor: k.ajustes.reduce((s, a) => s + Math.abs(a._val), 0), det: k.ajustes.length ? 'Cada ajuste reduz a acuracidade.' : 'Nenhum ajuste no período.' },
    { sev: 'serious', nome: 'Baixa de estoque ≠ custo debitado', itens: R.reconc.linhas, valor: Math.abs(R.reconc.dif), det: R.reconc.linhas ? `Estoque baixado em ${fmtBRL(R.reconc.baixa)}; ${fmtBRL(k.consumo)} debitados aos departamentos.` : 'Baixa e custo debitado coincidem.' },
    { sev: 'warn', nome: 'Saídas sem solicitante informado', itens: semSolic.length, valor: null, det: semSolic.length ? 'Preencher "Solicitante" garante rastreabilidade.' : 'Todas as saídas têm solicitante.' },
  ];
  const icon = { critical: '⛔', warn: '⚠', serious: '◆', good: '✓' };
  document.getElementById('repAlertasTbody').innerHTML = linhas.map((l) => {
    const zero = l.itens === 0;
    const sev = zero ? 'good' : l.sev;
    return `<tr><td><span class="flag flag-${sev}">${icon[sev]} ${escapeHtml(l.nome)}</span></td>
      <td class="num" style="font-weight:700;">${fmtNum(l.itens)}</td>
      <td class="num">${l.valor === null ? '<span class="muted">—</span>' : fmtBRL(l.valor)}</td>
      <td class="muted" style="font-size:12px;">${zero ? 'Nada a tratar.' : l.det}</td></tr>`;
  }).join('');
}

function renderPosicao() {
  const R = repData;
  if (!R) return;
  const busca = (document.getElementById('repFiltroProduto').value || '').toLowerCase();
  const classe = document.getElementById('repFiltroClasse').value;
  const situacao = document.getElementById('repFiltroSituacao').value;

  let list = [...R.itens].sort((a, b) => b.valor - a.valor || a.name.localeCompare(b.name, 'pt-BR'));
  if (busca) list = list.filter((i) => i.name.toLowerCase().includes(busca));
  if (classe) list = list.filter((i) => i.classe === classe);
  if (situacao) list = list.filter((i) => i.situacao === situacao);

  const tbody = document.getElementById('repPosicaoTbody');
  const tfoot = document.getElementById('repPosicaoTfoot');
  if (!list.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="11">${R.itens.length ? 'Nenhum material com esses filtros.' : 'Nenhum produto cadastrado.'}</td></tr>`;
    tfoot.innerHTML = '';
    return;
  }
  const sit = { ruptura: { cls: 'flag-critical', ico: '⛔', txt: 'Ruptura' }, baixo: { cls: 'flag-warn', ico: '⚠', txt: 'Abaixo do mínimo' }, parado: { cls: 'flag-serious', ico: '◆', txt: 'Capital parado' }, ok: { cls: 'flag-good', ico: '✓', txt: 'Normal' } };
  tbody.innerHTML = list.map((i) => {
    const s = sit[i.situacao];
    return `<tr><td><b>${escapeHtml(i.name)}</b></td><td>${i.category ? escapeHtml(i.category) : '<span class="muted">—</span>'}</td>
      <td>${escapeHtml(i.unit)}</td><td class="num">${fmtNum(i.qty)}</td>
      <td class="num">${fmtBRL(i.avgCost)}</td><td class="num">${fmtBRL(i.valor)}</td>
      <td class="num">${R.valorTotal > 0 ? fmtPct(i.valor / R.valorTotal, 1) : '—'}</td>
      <td><span class="abc-pill abc-${i.classe}">${i.classe}</span></td>
      <td class="num">${i.consumoMesQtd > 0 ? fmtNum(Math.round(i.consumoMesQtd * 100) / 100) : '<span class="muted">—</span>'}</td>
      <td class="num">${i.cobertura === null ? '<span class="muted">—</span>' : i.cobertura > 99 ? '99+ m' : fmtNum(Math.round(i.cobertura * 10) / 10) + ' m'}</td>
      <td><span class="flag ${s.cls}">${s.ico} ${s.txt}</span></td></tr>`;
  }).join('');
  const soma = list.reduce((s, i) => s + i.valor, 0);
  tfoot.innerHTML = `<tr><td colspan="5">TOTAL${list.length !== R.itens.length ? ` (${list.length} de ${R.itens.length})` : ' GERAL'}</td>
    <td class="num">${fmtBRL(soma)}</td><td class="num">${R.valorTotal > 0 ? fmtPct(soma / R.valorTotal, 1) : '—'}</td><td colspan="4"></td></tr>`;
}

function renderDeptos(R) {
  const nomes = new Set([...Object.keys(R.deptMes), ...Object.keys(R.deptYTD)]);
  const rows = [...nomes].map((n) => {
    const y = R.deptYTD[n] || { cur: 0, prev: 0 };
    return { name: n, mes: R.deptMes[n] || 0, cur: y.cur, prev: y.prev };
  }).filter((r) => r.mes || r.cur || r.prev).sort((a, b) => b.cur - a.cur || b.mes - a.mes);

  const tbody = document.getElementById('repDeptoTbody');
  const tfoot = document.getElementById('repDeptoTfoot');
  if (!rows.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="7">Nenhuma saída registrada. Cadastre departamentos e registre baixas.</td></tr>';
    tfoot.innerHTML = '';
    return;
  }
  const totMes = rows.reduce((s, r) => s + r.mes, 0);
  tbody.innerHTML = rows.map((r) => `<tr><td><b>${escapeHtml(r.name)}</b></td><td class="num">${fmtBRL(r.mes)}</td>
    <td class="num">${fmtBRL(r.cur)}</td><td class="num">${fmtBRL(r.prev)}</td><td class="num">${fmtBRL(r.cur - r.prev)}</td>
    <td class="num">${deltaCell(r.cur, r.prev)}</td><td class="num">${totMes > 0 ? fmtPct(r.mes / totMes, 1) : '—'}</td></tr>`).join('');
  tfoot.innerHTML = `<tr><td>TOTAL GERAL</td><td class="num">${fmtBRL(totMes)}</td><td class="num">${fmtBRL(R.ytdCur)}</td>
    <td class="num">${fmtBRL(R.ytdPrev)}</td><td class="num">${fmtBRL(R.ytdCur - R.ytdPrev)}</td><td class="num">${deltaCell(R.ytdCur, R.ytdPrev)}</td><td class="num">100%</td></tr>`;
}

function renderPedidos() {
  const R = repData;
  if (!R) return;
  const busca = (document.getElementById('repFiltroPedido').value || '').toLowerCase();
  const byId = new Map(R.itens.map((i) => [i.id, i]));
  let list = [...R.pedidos].sort((a, b) => a._ts - b._ts);
  if (busca) list = list.filter((p) => [byId.get(p.productId)?.name, p.recipient, p.requester, p.encarregado, p.obs].some((v) => String(v || '').toLowerCase().includes(busca)));

  const tbody = document.getElementById('repPedidosTbody');
  const tfoot = document.getElementById('repPedidosTfoot');
  if (!list.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="8">${R.pedidos.length ? 'Nenhum pedido com esse filtro.' : 'Nenhuma saída no mês de referência.'}</td></tr>`;
    tfoot.innerHTML = '';
    return;
  }
  let total = 0;
  tbody.innerHTML = list.map((p) => {
    const prod = byId.get(p.productId);
    const val = p.qty * p.unitPrice;
    total += val;
    return `<tr><td>${fmtDateBR(p._ts)}</td><td>${escapeHtml(p.recipient || '(sem departamento)')}</td>
      <td>${escapeHtml(prod ? prod.name : '(produto excluído)')}</td><td class="num">${fmtNum(p.qty)} ${escapeHtml(prod ? prod.unit : '')}</td>
      <td class="num">${fmtBRL(p.unitPrice)}</td><td class="num">${fmtBRL(val)}</td>
      <td>${p.requester ? escapeHtml(p.requester) : '<span class="muted">não informado</span>'}</td><td>${escapeHtml(p.encarregado || '—')}</td></tr>`;
  }).join('');
  tfoot.innerHTML = `<tr><td colspan="5">TOTAL — ${fmtNum(list.length)} pedido(s)</td><td class="num">${fmtBRL(total)}</td><td colspan="2"></td></tr>`;
}

function downloadFile(filename, content, mime) {
  const blob = new Blob([content], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = filename;
  document.body.appendChild(a); a.click(); document.body.removeChild(a);
  URL.revokeObjectURL(url);
}
function csvEscape(v) {
  const s = String(v === undefined || v === null ? '' : v);
  return /[";\n,]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
}

function exportReportCSV() {
  if (!repData) { toast('Abra o relatório antes de exportar.', 'error'); return; }
  const R = repData, k = R.kpi;
  const L = [];
  const push = (...cols) => L.push(cols.map(csvEscape).join(';'));
  const money = (v) => (v === null || v === undefined ? '' : Number(v).toFixed(2));
  const ref = MESES[R.m] + '/' + R.y;

  push('RELATORIO MENSAL - FL CONDOMINIOS');
  push('Referencia', ref);
  push('Departamento', R.deptFilter || 'Todos');
  push('');
  push('1. INDICADORES');
  push('Consumo do mes (R$)', money(k.consumo));
  push('Valor em estoque (R$)', money(k.valorEstoque));
  push('Pedidos atendidos', k.pedidos);
  push('Cobertura (meses)', k.cobertura === null ? 'n/d' : k.cobertura.toFixed(1));
  push('Giro anualizado', k.giro === null ? 'n/d' : k.giro.toFixed(2));
  push('Acuracidade (%)', k.acuracidade === null ? 'n/d' : (k.acuracidade * 100).toFixed(1));
  push('');
  push('2. LIMITE MENSAL POR DEPARTAMENTO');
  push('Departamento', 'Encarregado', 'Limite mensal', 'Gasto no mes', 'Saldo', '% do limite', 'Situacao');
  (R.limites ? R.limites.linhas : []).forEach((l) => push(l.name, l.encarregado, l.temLimite ? money(l.limite) : '',
    money(l.gasto), l.temLimite ? money(l.saldo) : '', l.pct === null ? '' : (l.pct * 100).toFixed(1), l.status));
  push('');
  push('3. POSICAO DE ESTOQUE');
  push('Material', 'Unidade', 'Saldo', 'Custo medio', 'Valor', 'Classe ABC', 'Situacao');
  [...R.itens].sort((a, b) => b.valor - a.valor).forEach((i) => push(i.name, i.unit, i.qty.toFixed(3), money(i.avgCost), money(i.valor), i.classe, i.situacao));
  push('');
  push('4. PEDIDOS DE ' + ref.toUpperCase());
  push('Data', 'Departamento', 'Material', 'Qtd', 'Valor unit.', 'Valor total', 'Solicitante');
  const byId = new Map(R.itens.map((i) => [i.id, i]));
  R.pedidos.forEach((p) => push(fmtDateBR(p._ts), p.recipient, byId.get(p.productId)?.name || '', p.qty.toFixed(3), money(p.unitPrice), money(p.qty * p.unitPrice), p.requester || ''));

  downloadFile(`relatorio_fl_${R.y}-${String(R.m + 1).padStart(2, '0')}.csv`, '﻿' + L.join('\r\n'), 'text/csv;charset=utf-8;');
  toast('Relatório exportado.', 'success');
}
