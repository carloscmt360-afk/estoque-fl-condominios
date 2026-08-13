// Retrospecto: a aba RESTROSPECTO da planilha dentro do app — custo mensal por
// departamento, total do ano, comparativo com o ano anterior no mesmo número de
// meses e o teto de gastos sugerido.
//
// Um detalhe de honestidade que atravessa a tela inteira: mês sem NENHUM
// lançamento em nenhuma fonte aparece como "—", nunca como R$ 0,00. Zero é uma
// afirmação ("este setor não gastou nada"); a ausência de dado não é.
import { api } from '../api.js';
import { installTooltip, legendHTML, VIZ } from '../charts/palette.js';
import { drawConsumoAnual } from '../charts/groupedBars.js';
import { drawBarrasH } from '../charts/deptHBars.js';
import { fmtBRL, fmtNum, fmtPct, escapeHtml, deltaCell, deltaInfo, meterHTML } from '../format.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { printDocument, buildRetrospectDoc } from '../print.js';

const MESES = ['Janeiro','Fevereiro','Março','Abril','Maio','Junho','Julho','Agosto','Setembro','Outubro','Novembro','Dezembro'];
const MESES_ABR = ['Jan','Fev','Mar','Abr','Mai','Jun','Jul','Ago','Set','Out','Nov','Dez'];

let RE = null;
let anoSelecionado = null;

export async function initRetrospect() {
  installTooltip();
  document.getElementById('retroAno').addEventListener('change', () => {
    anoSelecionado = parseInt(document.getElementById('retroAno').value, 10);
    render();
  });
  document.getElementById('retroFonte').addEventListener('change', render);
  document.getElementById('retroOcultarZeros').addEventListener('change', () => {
    if (RE) renderMatrizes(RE);
  });
  document.getElementById('btnSalvarTetoParams').addEventListener('click', salvarPremissas);
  document.getElementById('btnAplicarTeto').addEventListener('click', aplicarTetoComoLimite);
  document.getElementById('btnExportarRetro').addEventListener('click', exportarCSV);
  document.getElementById('btnImprimirRetro').addEventListener('click', () => {
    if (!RE) { toast('Abra o retrospecto antes de imprimir.', 'error'); return; }
    printDocument(buildRetrospectDoc(RE));
  });
  document.querySelectorAll('#view-retrospect [data-toggle-table]').forEach((btn) =>
    btn.addEventListener('click', () => toggleCardTable(btn.dataset.toggleTable)));
  aplicarPermissoes();
  await render();
}

function aplicarPermissoes() {
  const mostrar = (id, pode) => { document.getElementById(id).style.display = pode ? '' : 'none'; };
  mostrar('btnExportarRetro', can('retrospecto', 'create'));
  mostrar('btnImprimirRetro', can('retrospecto', 'create'));
  mostrar('btnSalvarTetoParams', can('retrospecto', 'update'));
  // "Usar como limite mensal" grava nos DEPARTAMENTOS — exige também o direito
  // de editá-los, senão o botão só produziria um erro do backend.
  mostrar('btnAplicarTeto', can('retrospecto', 'update') && can('departamentos', 'update'));
}

export async function reload() {
  await render();
}

function toggleCardTable(id) {
  const card = document.getElementById(id);
  const svg = card.querySelector('.chart-host');
  const tbl = card.querySelector('.tbl-view');
  const legend = card.querySelector('.legend');
  const mostrandoTabela = tbl.style.display !== 'none';
  tbl.style.display = mostrandoTabela ? 'none' : 'block';
  svg.style.display = mostrandoTabela ? 'block' : 'none';
  if (legend) legend.style.display = mostrandoTabela ? 'flex' : 'none';
  card.querySelector('[data-toggle-table]').textContent = mostrandoTabela ? 'Ver tabela' : 'Ver gráfico';
}

async function render() {
  const panel = document.getElementById('view-retrospect');
  if (!panel.classList.contains('active')) return;

  const ano = anoSelecionado || new Date().getFullYear();
  const source = document.getElementById('retroFonte').value;
  try {
    RE = await api.computeRetrospect({ year: ano, source, nowIso: new Date().toISOString() });
  } catch (e) {
    toast('Erro ao gerar o retrospecto: ' + e, 'error');
    return;
  }

  popularAnos(RE);
  panel.querySelectorAll('.retro-ano').forEach((e) => (e.textContent = RE.year));
  panel.querySelectorAll('.retro-ano-ant').forEach((e) => (e.textContent = RE.prevYear));

  renderNotaFonte(RE);
  renderTiles(RE);
  renderMatrizes(RE);
  renderGraficos(RE);
  renderComparativo(RE);
  renderTeto(RE);
}

/* A lista de anos vem do próprio backend (união do histórico importado com as
   movimentações), então nunca oferece um ano vazio nem esconde um ano que só
   existe na planilha. */
function popularAnos(R) {
  const sel = document.getElementById('retroAno');
  const desejado = String(R.year);
  const atual = R.anos.map(String).join(',');
  if (sel.dataset.anos !== atual) {
    sel.innerHTML = R.anos.map((a) => `<option value="${a}">${a}</option>`).join('');
    sel.dataset.anos = atual;
  }
  sel.value = desejado;
  anoSelecionado = R.year;
}

function renderNotaFonte(R) {
  const nota = document.getElementById('retroFonteNota');
  const avisos = [];
  if (R.source === 'ledger') {
    avisos.push('<b>Fonte: só movimentações do sistema.</b> O histórico importado da planilha está sendo ignorado — ' +
      'use este modo para conferir uma fonte contra a outra.');
  } else if (R.fontes.planilha > 0 && R.fontes.sistema > 0) {
    avisos.push(`<b>Fontes combinadas:</b> ${R.fontes.planilha} ${R.fontes.planilha === 1 ? 'mês veio' : 'meses vieram'} ` +
      `do histórico da planilha e ${R.fontes.sistema} ${R.fontes.sistema === 1 ? 'mês veio' : 'meses vieram'} ` +
      'das movimentações lançadas no sistema. A origem de cada mês aparece no cabeçalho da matriz.');
  } else if (R.fontes.planilha > 0) {
    avisos.push('<b>Fonte: histórico importado da planilha.</b> Meses lançados no sistema passam a aparecer aqui automaticamente.');
  }
  if (!avisos.length) { nota.style.display = 'none'; return; }
  nota.style.display = 'block';
  nota.className = 'info-box';
  nota.innerHTML = avisos.join('<div style="height:6px"></div>');
}

function renderTiles(R) {
  const comp = R.comparativo;
  const mesesComDado = R.ref.origem.filter(Boolean).length;
  const media = mesesComDado > 0 ? R.ref.total / mesesComDado : 0;
  const maior = [...R.ref.linhas].sort((a, b) => b.total - a.total)[0];
  const d = deltaInfo(comp.curTotal, comp.prevTotal);

  const tiles = [
    { label: `Total ${R.year}`, value: fmtBRL(R.ref.total),
      foot: `${mesesComDado} ${mesesComDado === 1 ? 'mês' : 'meses'} com lançamento`,
      help: 'Soma de todos os departamentos nos meses com dado.' },
    { label: `Total ${R.prevYear}`, value: fmtBRL(R.ant.total),
      foot: `${R.ant.origem.filter(Boolean).length} meses com lançamento`,
      help: 'Mesmo cálculo, no ano anterior.' },
    { label: 'Mesmo período', value: fmtBRL(comp.curTotal),
      cls: d.kind === 'up' ? 'is-critical' : d.kind === 'down' ? 'is-good' : '',
      foot: comp.mesLimite >= 0
        ? `${d.text} vs ${fmtBRL(comp.prevTotal)} em jan–${MESES_ABR[comp.mesLimite].toLowerCase()}/${R.prevYear}`
        : 'sem período comparável',
      help: 'Compara o mesmo número de meses nos dois anos.' },
    { label: 'Média mensal', value: fmtBRL(media), foot: `${R.year}, nos meses com dado`,
      help: 'Total do ano ÷ meses com lançamento.' },
    { label: 'Maior departamento', value: maior ? fmtBRL(maior.total) : 'n/d',
      foot: maior ? `${maior.name} — ${R.ref.total > 0 ? fmtPct(maior.total / R.ref.total, 1) : '—'} do total` : 'sem lançamento no ano',
      help: 'Departamento com maior custo acumulado no ano.' },
  ];
  document.getElementById('retroTiles').innerHTML = tiles.map((t) => `
    <div class="stat-tile ${t.cls || ''}">
      <div class="label">${escapeHtml(t.label)}<span class="help" title="${escapeHtml(t.help)}">i</span></div>
      <div class="value">${t.value === 'n/d' ? '<span class="muted">n/d</span>' : escapeHtml(String(t.value))}</div>
      <div class="foot">${escapeHtml(t.foot)}</div>
    </div>`).join('');
}

/* Valor monetário sem o "R$" (a matriz tem 14 colunas; o cabeçalho do painel
   já diz que é em R$) mas SEMPRE com 2 casas — uma coluna de dinheiro com
   "122,5" ao lado de "146,77" lê mal. */
function moeda(v) {
  return (v || 0).toLocaleString('pt-BR', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
}

function matrizHTML(g, ocultarVazios) {
  const linhas = [...g.linhas].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  const mesesVisiveis = [];
  for (let i = 0; i < 12; i++) {
    if (!ocultarVazios || g.origem[i]) mesesVisiveis.push(i);
  }
  if (!linhas.length) {
    return `<thead><tr><th class="col-dept">Departamento</th></tr></thead>
      <tbody><tr class="empty-row"><td>Nenhum lançamento em ${g.year}.</td></tr></tbody>`;
  }

  const cabecalho = mesesVisiveis.map((i) => {
    const origem = g.origem[i];
    const tag = origem === 'planilha' ? 'planilha' : origem === 'sistema' ? 'sistema' : '';
    return `<th class="num">${MESES_ABR[i]}${tag ? `<span class="fonte-tag">${tag}</span>` : ''}</th>`;
  }).join('');

  // "—" (mês que nenhuma fonte cobre) e "0" (mês coberto em que o setor não
  // gastou) são coisas diferentes e precisam ficar diferentes na tela.
  const celula = (v, mes) => {
    if (!g.origem[mes]) return '<td class="num vazio" title="mês sem lançamento em nenhuma fonte">—</td>';
    if (!v) return '<td class="num zero" title="sem consumo neste mês">0</td>';
    return `<td class="num">${moeda(v)}</td>`;
  };

  return `<thead><tr><th class="col-dept">Departamento</th>${cabecalho}<th class="num col-total">Total ${g.year}</th></tr></thead>
    <tbody>${linhas.map((l) => `<tr><td class="col-dept">${escapeHtml(l.name)}</td>
      ${mesesVisiveis.map((i) => celula(l.meses[i], i)).join('')}
      <td class="num col-total">${moeda(l.total)}</td></tr>`).join('')}</tbody>
    <tfoot><tr><td class="col-dept">TOTAL</td>
      ${mesesVisiveis.map((i) => g.origem[i]
        ? `<td class="num">${moeda(g.totaisMes[i])}</td>`
        : '<td class="num vazio">—</td>').join('')}
      <td class="num col-total">${moeda(g.total)}</td></tr></tfoot>`;
}

function renderMatrizes(R) {
  const ocultar = document.getElementById('retroOcultarZeros').checked;
  document.getElementById('retroMatrizRef').innerHTML = matrizHTML(R.ref, ocultar);
  document.getElementById('retroMatrizAnt').innerHTML = matrizHTML(R.ant, ocultar);
}

function renderGraficos(R) {
  document.getElementById('legRetroAnual').innerHTML =
    legendHTML([{ label: String(R.year), color: VIZ.s1 }, { label: String(R.prevYear), color: VIZ.s2 }]);
  drawConsumoAnual(document.getElementById('chartRetroAnual'), R.anual, R.year, R.ref.ultimoMes);
  document.getElementById('tblRetroAnual').innerHTML =
    `<table><thead><tr><th>Mês</th><th class="num">${R.year}</th><th class="num">${R.prevYear}</th><th class="num">Δ %</th></tr></thead>
     <tbody>${R.anual.map((a) => `<tr><td>${MESES[a.mes]}</td>
       <td class="num">${a.ref === null ? `<span class="muted">${a.futuro ? 'não decorrido' : '—'}</span>` : fmtBRL(a.ref)}</td>
       <td class="num">${a.antSemDado ? '<span class="muted">—</span>' : fmtBRL(a.ant)}</td>
       <td class="num">${a.ref === null || a.antSemDado ? '<span class="muted">—</span>' : deltaCell(a.ref, a.ant)}</td></tr>`).join('')}</tbody>
     <tfoot><tr><td>TOTAL</td><td class="num">${fmtBRL(R.ref.total)}</td><td class="num">${fmtBRL(R.ant.total)}</td>
       <td class="num">${deltaCell(R.ref.total, R.ant.total)}</td></tr></tfoot></table>`;

  const rows = [...R.ref.linhas].sort((a, b) => b.total - a.total).map((l) => ({ label: l.name, value: l.total }));
  drawBarrasH(document.getElementById('chartRetroDeptos'), rows,
    { tipLabel: `Total ${R.year}`, aria: 'Total do ano por departamento', empty: `Nenhum lançamento em ${R.year}.` });
  document.getElementById('tblRetroDeptos').innerHTML = rows.length
    ? `<table><thead><tr><th>Departamento</th><th class="num">Total</th><th class="num">%</th></tr></thead>
       <tbody>${rows.map((r) => `<tr><td>${escapeHtml(r.label)}</td><td class="num">${fmtBRL(r.value)}</td>
         <td class="num">${R.ref.total > 0 ? fmtPct(r.value / R.ref.total, 1) : '—'}</td></tr>`).join('')}</tbody>
       <tfoot><tr><td>TOTAL</td><td class="num">${fmtBRL(R.ref.total)}</td><td class="num">100%</td></tr></tfoot></table>`
    : `<div class="chart-empty">Nenhum lançamento em ${R.year}.</div>`;
}

function renderComparativo(R) {
  const comp = R.comparativo;
  const titulo = document.getElementById('retroCompTitulo');
  titulo.textContent = comp.mesLimite >= 0
    ? `Janeiro a ${MESES[comp.mesLimite].toLowerCase()} — ${R.prevYear} vs ${R.year}`
    : 'Sem período comparável';

  const tbody = document.getElementById('retroCompTbody');
  const tfoot = document.getElementById('retroCompTfoot');
  const linhas = [...comp.linhas].sort((a, b) => b.cur - a.cur || b.prev - a.prev);
  if (!linhas.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="5">Nenhum lançamento em ${R.year} para comparar.</td></tr>`;
    tfoot.innerHTML = '';
    return;
  }
  tbody.innerHTML = linhas.map((l) => `<tr><td><b>${escapeHtml(l.name)}</b></td>
    <td class="num">${fmtBRL(l.prev)}</td><td class="num">${fmtBRL(l.cur)}</td>
    <td class="num">${fmtBRL(l.cur - l.prev)}</td><td class="num">${deltaCell(l.cur, l.prev)}</td></tr>`).join('');
  tfoot.innerHTML = `<tr><td>TOTAL</td><td class="num">${fmtBRL(comp.prevTotal)}</td>
    <td class="num">${fmtBRL(comp.curTotal)}</td><td class="num">${fmtBRL(comp.curTotal - comp.prevTotal)}</td>
    <td class="num">${deltaCell(comp.curTotal, comp.prevTotal)}</td></tr>`;
}

function renderTeto(R) {
  document.getElementById('retroMeta').value = Math.round(R.params.metaReducao * 1000) / 10;
  document.getElementById('retroIpca').value = Math.round(R.params.ipca * 1000) / 10;
  document.getElementById('retroPiso').value = R.params.pisoMensal;
  document.getElementById('retroFator').textContent = fmtNum(Math.round(R.params.fator * 10000) / 10000);

  const teto = R.teto;
  const tbody = document.getElementById('retroTetoTbody');
  const tfoot = document.getElementById('retroTetoTfoot');
  const linhas = [...teto.linhas].sort((a, b) => b.base - a.base || a.name.localeCompare(b.name, 'pt-BR'));
  if (!linhas.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="8">Sem base em ${teto.baseYear} para sugerir um teto.</td></tr>`;
    tfoot.innerHTML = '';
    return;
  }
  const sit = {
    estourado: { cls: 'flag-critical', ico: '⛔', txt: 'Estourou' },
    atencao: { cls: 'flag-warn', ico: '⚠', txt: 'Atenção' },
    ok: { cls: 'flag-good', ico: '✓', txt: 'Dentro' },
    'sem-base': { cls: '', ico: '', txt: '—' },
  };
  tbody.innerHTML = linhas.map((l) => {
    const s = sit[l.status] || sit['sem-base'];
    return `<tr><td><b>${escapeHtml(l.name)}</b>${l.temLimite
        ? `<div class="muted" style="font-size:11px;">limite cadastrado: ${fmtBRL(l.limiteMensal)}/mês</div>` : ''}</td>
      <td class="num">${fmtBRL(l.base)}</td>
      <td class="num">${fmtBRL(l.tetoMensal)}${l.noPiso ? ' <span class="muted" title="definido pelo piso mensal">*</span>' : ''}</td>
      <td class="num">${fmtBRL(l.tetoAnual)}</td>
      <td class="num">${fmtBRL(l.realizado)}</td>
      <td class="num">${fmtBRL(l.tetoPeriodo)}</td>
      <td>${meterHTML(l.pctPeriodo)}</td>
      <td>${s.txt === '—' ? '<span class="muted">—</span>' : `<span class="flag ${s.cls}">${s.ico} ${s.txt}</span>`}</td></tr>`;
  }).join('');
  const pctTotal = teto.tetoPeriodoTotal > 0 ? teto.realizadoTotal / teto.tetoPeriodoTotal : null;
  tfoot.innerHTML = `<tr><td>TOTAL (${teto.mesesDecorridos} ${teto.mesesDecorridos === 1 ? 'mês' : 'meses'})</td>
    <td class="num">${fmtBRL(teto.baseTotal)}</td><td class="num">${fmtBRL(teto.tetoMensalTotal)}</td>
    <td class="num">${fmtBRL(teto.tetoAnualTotal)}</td><td class="num">${fmtBRL(teto.realizadoTotal)}</td>
    <td class="num">${fmtBRL(teto.tetoPeriodoTotal)}</td><td>${meterHTML(pctTotal)}</td><td></td></tr>`;
}

async function salvarPremissas() {
  const meta = parseFloat(document.getElementById('retroMeta').value);
  const ipca = parseFloat(document.getElementById('retroIpca').value);
  const piso = parseFloat(document.getElementById('retroPiso').value);
  if (![meta, ipca, piso].every((v) => Number.isFinite(v)) || piso < 0) {
    toast('Preencha meta, inflação e piso com números válidos.', 'error');
    return;
  }
  try {
    await api.saveBudgetParams({ metaReducao: meta / 100, ipca: ipca / 100, pisoMensal: piso });
    await render();
    toast('Premissas do teto salvas.', 'success');
  } catch (e) { toast('Erro ao salvar: ' + e, 'error'); }
}

/* Grava o teto sugerido como limite mensal de cada departamento cadastrado —
   é a ponte entre esta tela (sugestão) e o alerta do Relatório Mensal (que só
   olha departments.monthly_limit). */
async function aplicarTetoComoLimite() {
  if (!RE) return;
  const departamentos = await api.listDepartments();
  const porNome = new Map(departamentos.map((d) => [normalizar(d.name), d]));
  const alvos = RE.teto.linhas
    .map((l) => ({ linha: l, dep: porNome.get(normalizar(l.name)) }))
    .filter((x) => x.dep);

  if (!alvos.length) {
    toast('Nenhum departamento do teto está cadastrado em Departamentos.', 'error');
    return;
  }
  const msg = `Definir o limite mensal de ${alvos.length} departamento(s) com o teto sugerido de ${RE.year}?\n\n` +
    alvos.slice(0, 8).map((x) => `• ${x.dep.name}: ${fmtBRL(x.linha.tetoMensal)}/mês`).join('\n') +
    (alvos.length > 8 ? `\n• ... e mais ${alvos.length - 8}` : '') +
    '\n\nIsto substitui os limites já cadastrados.';
  if (!confirm(msg)) return;

  try {
    for (const { linha, dep } of alvos) {
      await api.updateDepartment({
        id: dep.id, name: dep.name, encarregado: dep.encarregado,
        monthlyLimit: Math.round(linha.tetoMensal * 100) / 100, createdAt: dep.createdAt,
      });
    }
    toast(`Limite mensal aplicado a ${alvos.length} departamento(s).`, 'success');
  } catch (e) { toast('Erro ao aplicar: ' + e, 'error'); }
}

function normalizar(s) {
  return String(s || '').trim().replace(/\s+/g, ' ').toUpperCase();
}

function csvEscape(v) {
  const s = String(v === undefined || v === null ? '' : v);
  return /[";\n,]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
}

function exportarCSV() {
  if (!RE) { toast('Abra o retrospecto antes de exportar.', 'error'); return; }
  const L = [];
  const push = (...cols) => L.push(cols.map(csvEscape).join(';'));
  const money = (v) => (v === null || v === undefined ? '' : Number(v).toFixed(2));

  push('RETROSPECTO - FL CONDOMINIOS');
  push('Ano', RE.year);
  push('Fonte', RE.source === 'ledger' ? 'Somente movimentacoes do sistema' : 'Planilha + movimentacoes');
  push('');
  for (const g of [RE.ref, RE.ant]) {
    push(`CUSTO MENSAL POR DEPARTAMENTO - ${g.year}`);
    push('Departamento', ...MESES, 'TOTAL');
    [...g.linhas].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'))
      .forEach((l) => push(l.name, ...l.meses.map((v, i) => (g.origem[i] ? money(v) : '')), money(l.total)));
    push('TOTAL', ...g.totaisMes.map((v, i) => (g.origem[i] ? money(v) : '')), money(g.total));
    push('');
  }
  push('MESMO PERIODO NOS DOIS ANOS');
  push('Departamento', String(RE.prevYear), String(RE.year), 'Variacao');
  RE.comparativo.linhas.forEach((l) => push(l.name, money(l.prev), money(l.cur), money(l.cur - l.prev)));
  push('TOTAL', money(RE.comparativo.prevTotal), money(RE.comparativo.curTotal),
    money(RE.comparativo.curTotal - RE.comparativo.prevTotal));
  push('');
  push('TETO DE GASTOS');
  push('Meta de reducao', RE.params.metaReducao, 'Inflacao', RE.params.ipca,
    'Fator', RE.params.fator, 'Piso mensal', RE.params.pisoMensal);
  push('Departamento', `Base ${RE.teto.baseYear}`, 'Teto mensal', 'Teto anual', 'Realizado', 'Teto do periodo', 'Situacao');
  RE.teto.linhas.forEach((l) => push(l.name, money(l.base), money(l.tetoMensal), money(l.tetoAnual),
    money(l.realizado), money(l.tetoPeriodo), l.status));

  const blob = new Blob(['﻿' + L.join('\r\n')], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `retrospecto_fl_${RE.year}.csv`;
  document.body.appendChild(a); a.click(); document.body.removeChild(a);
  URL.revokeObjectURL(url);
  toast('Retrospecto exportado.', 'success');
}
