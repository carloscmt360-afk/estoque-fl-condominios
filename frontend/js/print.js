// Impressão dos relatórios.
//
// Em vez de mandar a tela para a impressora, cada relatório monta um
// documento próprio (só cabeçalho + tabelas) dentro de #printArea; o
// css/print.css esconde o app e mostra só esse bloco. Motivos: a sidebar, os
// filtros e os gráficos SVG não têm função no papel; o WebView do Tauri
// bloqueia window.open (então não dá para abrir uma segunda janela); e uma
// tabela impressa continua legível em preto e branco, o que um gráfico de
// barras coloridas não garante.
import { fmtBRL, fmtNum, fmtPct, fmtDateBR, fmtDateTimeBR, escapeHtml, deltaInfo } from './format.js';

const MESES = ['Janeiro','Fevereiro','Março','Abril','Maio','Junho','Julho','Agosto','Setembro','Outubro','Novembro','Dezembro'];
const MESES_ABR = ['Jan','Fev','Mar','Abr','Mai','Jun','Jul','Ago','Set','Out','Nov','Dez'];

const SIT = {
  ruptura: { txt: 'Ruptura', cls: 'pr-st-critical' },
  baixo: { txt: 'Abaixo do mínimo', cls: 'pr-st-warn' },
  parado: { txt: 'Capital parado', cls: 'pr-st-serious' },
  ok: { txt: 'Normal', cls: 'pr-st-good' },
};
function sitBadge(situacao) {
  const d = SIT[situacao];
  return d ? `<span class="${d.cls}">${escapeHtml(d.txt)}</span>` : escapeHtml(situacao);
}

const LIMITE_STATUS = {
  estourado: 'pr-st-critical', atencao: 'pr-st-warn', ok: 'pr-st-good',
};
function limiteBadge(status, txt) {
  const cls = LIMITE_STATUS[status];
  return cls ? `<span class="${cls}">${escapeHtml(txt)}</span>` : escapeHtml(txt);
}

function head(titulo, sub, extras) {
  return `<div class="pr-head">
    <h1>${escapeHtml(titulo)}</h1>
    <div class="pr-sub"><b>Estoque FL Condomínios</b> — ${escapeHtml(sub)}</div>
    <div class="pr-meta">Emitido em ${fmtDateTimeBR(new Date().toISOString())}${extras ? ' · ' + escapeHtml(extras) : ''}</div>
  </div>`;
}

function assinaturas() {
  return `<div class="pr-sign"><div>Responsável pelo almoxarifado</div><div>Gerência</div></div>`;
}

function rodape(nota) {
  return `<div class="pr-foot">${escapeHtml(nota)}</div>`;
}

function kpiGrid(itens) {
  return `<div class="pr-kpis">${itens.map((i) => `<div class="pr-kpi">
    <div class="k">${escapeHtml(i.k)}</div><div class="v">${escapeHtml(i.v)}</div>
    <div class="f">${escapeHtml(i.f || '')}</div></div>`).join('')}</div>`;
}

// Dinheiro sem o "R$" (a matriz tem 14 colunas) e sempre com 2 casas.
function moedaSimples(v) {
  return (v || 0).toLocaleString('pt-BR', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
}

function pctTxt(v, dec) {
  return v === null || v === undefined ? '—' : fmtPct(v, dec === undefined ? 1 : dec);
}

function deltaTxt(cur, prev) {
  const d = deltaInfo(cur, prev);
  return d.kind === 'nd' ? '—' : d.text;
}

/* Envia `html` para a impressora. O conteúdo é descartado depois para o
   documento não ficar preso no DOM entre uma impressão e outra. */
export function printDocument(html) {
  const area = document.getElementById('printArea');
  area.innerHTML = html;
  const cleanup = () => {
    area.innerHTML = '';
    window.removeEventListener('afterprint', cleanup);
  };
  window.addEventListener('afterprint', cleanup);
  window.print();
  // Nem todo WebView dispara afterprint; a rede de segurança evita segurar o
  // documento para sempre sem cortar a impressão de quem dispara certo.
  setTimeout(cleanup, 60000);
}

// ------------------------------------------------------- relatório de estoque

export function buildEstoqueDoc(R) {
  const ref = `${MESES[R.m]}/${R.y}`;
  const itens = [...R.itens].sort((a, b) => b.valor - a.valor || a.name.localeCompare(b.name, 'pt-BR'));
  const k = R.kpi;

  const linhas = itens.map((i) => `<tr>
    <td>${escapeHtml(i.name)}</td>
    <td>${escapeHtml(i.category || '—')}</td>
    <td>${escapeHtml(i.unit)}</td>
    <td class="num">${fmtNum(i.qty)}</td>
    <td class="num">${fmtNum(i.minStock)}</td>
    <td class="num">${fmtBRL(i.avgCost)}</td>
    <td class="num">${fmtBRL(i.valor)}</td>
    <td class="num">${R.valorTotal > 0 ? fmtPct(i.valor / R.valorTotal, 1) : '—'}</td>
    <td class="pr-abc pr-abc-${i.classe}">${escapeHtml(i.classe)}</td>
    <td class="num">${i.consumoMesQtd > 0 ? fmtNum(Math.round(i.consumoMesQtd * 100) / 100) : '—'}</td>
    <td class="num">${i.cobertura === null ? '—' : (i.cobertura > 99 ? '99+' : fmtNum(Math.round(i.cobertura * 10) / 10)) + ' m'}</td>
    <td>${sitBadge(i.situacao)}</td></tr>`).join('');

  return head('Relatório de Estoque', `Posição de fechamento de ${ref}`,
      R.deptFilter ? `Departamento: ${R.deptFilter}` : '') +
    kpiGrid([
      { k: 'Valor em estoque', v: fmtBRL(k.valorEstoque), f: `${fmtNum(R.itens.length)} materiais cadastrados` },
      { k: 'Itens em ruptura', v: fmtNum(k.ruptura), f: `${fmtNum(k.baixo)} abaixo do mínimo` },
      { k: 'Capital parado', v: fmtBRL(k.paradoValor), f: `${fmtNum(k.paradoItens.length)} sem consumo em ${R.janela} meses` },
      { k: 'Cobertura', v: k.cobertura === null ? 'n/d' : fmtNum(Math.round(k.cobertura * 10) / 10) + ' meses', f: `ritmo dos últimos ${R.janela} meses` },
      { k: 'Giro anualizado', v: k.giro === null ? 'n/d' : fmtNum(Math.round(k.giro * 100) / 100) + '×', f: 'consumo 12m ÷ estoque médio' },
      { k: 'Acuracidade', v: k.acuracidade === null ? 'n/d' : fmtPct(k.acuracidade, 1), f: `${fmtNum(k.ajustes.length)} ajuste(s) no mês` },
    ]) +
    `<div class="pr-sec">Posição de estoque por item</div>
     <table>
       <thead><tr><th>Material</th><th>Categoria</th><th>Un.</th><th class="num">Saldo</th><th class="num">Mín.</th>
         <th class="num">Custo médio</th><th class="num">Valor</th><th class="num">% valor</th><th>ABC</th>
         <th class="num">Cons./mês</th><th class="num">Cobert.</th><th>Situação</th></tr></thead>
       <tbody>${linhas || '<tr><td colspan="12">Nenhum produto cadastrado.</td></tr>'}</tbody>
       <tfoot><tr><td colspan="6">TOTAL GERAL</td><td class="num">${fmtBRL(R.valorTotal)}</td>
         <td class="num">100%</td><td colspan="4"></td></tr></tfoot>
     </table>` +
    rodape('Saldo e custo médio reconstruídos cronologicamente pelas movimentações registradas. ' +
      'Cobertura = saldo ÷ consumo médio mensal da janela.') +
    assinaturas();
}

// ----------------------------------------------------- relatório analítico

export function buildAnaliticoDoc(R) {
  const ref = `${MESES[R.m]}/${R.y}`;
  const k = R.kpi;
  const lim = R.limites || { linhas: [], comLimite: 0, atencao: 0, estourado: 0, tetoTotal: 0, gastoTotal: 0 };
  const byId = new Map(R.itens.map((i) => [i.id, i]));

  // --- avisos de limite: no papel também vêm primeiro ---
  const estourados = lim.linhas.filter((l) => l.status === 'estourado');
  const emAtencao = lim.linhas.filter((l) => l.status === 'atencao');
  let avisos = '';
  if (estourados.length) {
    avisos += `<div class="pr-alert">LIMITE MENSAL ESTOURADO — ${estourados.map((l) =>
      `${escapeHtml(l.name)} (${fmtBRL(l.gasto)} de ${fmtBRL(l.limite)}, ${fmtPct(l.pct, 0)})`).join(' · ')}</div>`;
  }
  if (emAtencao.length) {
    avisos += `<div class="pr-alert pr-alert-warn">LIMITE MENSAL EM ATENÇÃO (90% OU MAIS) — ${emAtencao.map((l) =>
      `${escapeHtml(l.name)} (${fmtBRL(l.gasto)} de ${fmtBRL(l.limite)}, ${fmtPct(l.pct, 0)})`).join(' · ')}</div>`;
  }

  // --- consumo mensal ano x ano anterior ---
  const anual = R.anual.map((a) => `<tr><td>${MESES[a.mes]}</td>
    <td class="num">${a.futuro ? '—' : fmtBRL(a.ref)}</td><td class="num">${fmtBRL(a.ant)}</td>
    <td class="num">${a.futuro ? '—' : deltaTxt(a.ref || 0, a.ant)}</td></tr>`).join('');

  // --- custo por departamento ---
  const nomes = new Set([...Object.keys(R.deptMes), ...Object.keys(R.deptYTD)]);
  const deptos = [...nomes].map((n) => {
    const yy = R.deptYTD[n] || { cur: 0, prev: 0 };
    return { name: n, mes: R.deptMes[n] || 0, cur: yy.cur, prev: yy.prev };
  }).filter((r) => r.mes || r.cur || r.prev).sort((a, b) => b.cur - a.cur || b.mes - a.mes);
  const totMes = deptos.reduce((s, r) => s + r.mes, 0);

  // --- limites ---
  const limLinhas = lim.linhas.filter((l) => l.temLimite)
    .sort((a, b) => (b.pct || 0) - (a.pct || 0));

  // --- top consumo ---
  const top = R.itens.filter((i) => i.consumoRef.val > 0)
    .sort((a, b) => b.consumoRef.val - a.consumoRef.val).slice(0, 15);

  // --- pedidos ---
  const pedidos = [...R.pedidos].sort((a, b) => a._ts - b._ts);
  let totalPedidos = 0;
  const pedidosRows = pedidos.map((p) => {
    const prod = byId.get(p.productId);
    const val = p.qty * p.unitPrice;
    totalPedidos += val;
    return `<tr><td>${fmtDateBR(p._ts)}</td><td>${escapeHtml(p.recipient || '(sem departamento)')}</td>
      <td>${escapeHtml(prod ? prod.name : '(produto excluído)')}</td>
      <td class="num">${fmtNum(p.qty)} ${escapeHtml(prod ? prod.unit : '')}</td>
      <td class="num">${fmtBRL(p.unitPrice)}</td><td class="num">${fmtBRL(val)}</td>
      <td>${escapeHtml(p.requester || 'não informado')}</td></tr>`;
  }).join('');

  const escopo = [];
  if (R.deptFilter) escopo.push(`Departamento: ${R.deptFilter}`);
  if (R.refEmCurso) escopo.push(`Mês em curso (${R.diasDecorridos} de ${R.diasNoMes} dias)`);

  return head('Relatório Analítico', `Referência ${ref}`, escopo.join(' · ')) +
    avisos +
    `<div class="pr-sec">1. Indicadores do período</div>` +
    kpiGrid([
      { k: 'Consumo do mês', v: fmtBRL(k.consumo), f: `${deltaTxt(k.consumo, k.consumoAnt)} vs mês anterior` },
      { k: 'Compras no mês', v: fmtBRL(k.compras), f: `${deltaTxt(k.compras, k.comprasAnt)} vs mês anterior` },
      { k: 'Valor em estoque', v: fmtBRL(k.valorEstoque), f: `${deltaTxt(k.valorEstoque, k.valorEstoqueAnt)} vs mês anterior` },
      { k: 'Pedidos atendidos', v: fmtNum(k.pedidos), f: `${fmtNum(k.itensDistintos)} materiais distintos` },
      { k: 'Ticket médio', v: fmtBRL(k.ticket), f: 'consumo ÷ pedidos' },
      { k: 'Acumulado no ano', v: fmtBRL(R.ytdCur), f: `${deltaTxt(R.ytdCur, R.ytdPrev)} vs ${R.y - 1} (mesmo período)` },
      { k: 'Itens em ruptura', v: fmtNum(k.ruptura), f: `${fmtNum(k.baixo)} abaixo do mínimo` },
      { k: 'Capital parado', v: fmtBRL(k.paradoValor), f: `${fmtNum(k.paradoItens.length)} itens sem consumo` },
      { k: 'Acuracidade', v: k.acuracidade === null ? 'n/d' : fmtPct(k.acuracidade, 1), f: `${fmtNum(k.ajustes.length)} ajuste(s) no mês` },
    ]) +

    `<div class="pr-sec">2. Consumo mensal — ${R.y} vs ${R.y - 1}</div>
     <table><thead><tr><th>Mês</th><th class="num">${R.y}</th><th class="num">${R.y - 1}</th><th class="num">Δ</th></tr></thead>
       <tbody>${anual}</tbody></table>` +

    `<div class="pr-sec">3. Custo por departamento</div>
     <table><thead><tr><th>Departamento</th><th class="num">${MESES_ABR[R.m]}/${R.y}</th><th class="num">% do mês</th>
       <th class="num">Acum. ${R.y}</th><th class="num">Acum. ${R.y - 1}</th><th class="num">Variação</th><th class="num">Δ</th></tr></thead>
       <tbody>${deptos.map((r) => `<tr><td>${escapeHtml(r.name)}</td><td class="num">${fmtBRL(r.mes)}</td>
         <td class="num">${totMes > 0 ? fmtPct(r.mes / totMes, 1) : '—'}</td>
         <td class="num">${fmtBRL(r.cur)}</td><td class="num">${fmtBRL(r.prev)}</td>
         <td class="num">${fmtBRL(r.cur - r.prev)}</td><td class="num">${deltaTxt(r.cur, r.prev)}</td></tr>`).join('')
         || '<tr><td colspan="7">Nenhuma saída registrada.</td></tr>'}</tbody>
       <tfoot><tr><td>TOTAL</td><td class="num">${fmtBRL(totMes)}</td><td class="num">100%</td>
         <td class="num">${fmtBRL(R.ytdCur)}</td><td class="num">${fmtBRL(R.ytdPrev)}</td>
         <td class="num">${fmtBRL(R.ytdCur - R.ytdPrev)}</td><td class="num">${deltaTxt(R.ytdCur, R.ytdPrev)}</td></tr></tfoot>
     </table>` +

    `<div class="pr-sec">4. Limite mensal por departamento</div>` +
    (limLinhas.length
      ? `<table><thead><tr><th>Departamento</th><th>Encarregado</th><th class="num">Limite</th>
           <th class="num">Gasto</th><th class="num">Saldo</th><th class="num">% do limite</th><th>Situação</th></tr></thead>
         <tbody>${limLinhas.map((l) => `<tr><td>${escapeHtml(l.name)}</td><td>${escapeHtml(l.encarregado || '—')}</td>
           <td class="num">${fmtBRL(l.limite)}</td><td class="num">${fmtBRL(l.gasto)}</td>
           <td class="num">${fmtBRL(l.saldo)}</td><td class="num">${pctTxt(l.pct, 0)}</td>
           <td>${limiteBadge(l.status, l.status === 'estourado' ? 'ESTOUROU' : l.status === 'atencao' ? 'Atenção (≥90%)' : 'Dentro do limite')}</td></tr>`).join('')}</tbody>
         <tfoot><tr><td colspan="2">TOTAL</td><td class="num">${fmtBRL(lim.tetoTotal)}</td>
           <td class="num">${fmtBRL(lim.gastoTotal)}</td><td class="num">${fmtBRL(lim.tetoTotal - lim.gastoTotal)}</td>
           <td class="num">${lim.tetoTotal > 0 ? fmtPct(lim.gastoTotal / lim.tetoTotal, 0) : '—'}</td><td></td></tr></tfoot>
       </table>`
      : '<div class="pr-note">Nenhum departamento tem limite mensal definido. Configure em Departamentos.</div>') +

    `<div class="pr-sec">5. Curva ABC do estoque</div>
     <table><thead><tr><th>Classe</th><th class="num">Itens</th><th class="num">Valor</th><th class="num">% do valor</th></tr></thead>
       <tbody>${['A', 'B', 'C'].map((c) => {
         const tot = R.abc.A.v + R.abc.B.v + R.abc.C.v;
         return `<tr><td>Classe ${c}</td><td class="num">${fmtNum(R.abc[c].n)}</td>
           <td class="num">${fmtBRL(R.abc[c].v)}</td><td class="num">${tot > 0 ? fmtPct(R.abc[c].v / tot, 1) : '—'}</td></tr>`;
       }).join('')}</tbody></table>` +

    `<div class="pr-sec">6. Materiais mais consumidos em ${ref}</div>
     <table><thead><tr><th>Material</th><th class="num">Quantidade</th><th class="num">Valor</th></tr></thead>
       <tbody>${top.map((i) => `<tr><td>${escapeHtml(i.name)}</td>
         <td class="num">${fmtNum(i.consumoRef.qty)} ${escapeHtml(i.unit)}</td>
         <td class="num">${fmtBRL(i.consumoRef.val)}</td></tr>`).join('')
         || '<tr><td colspan="3">Nenhum material consumido no período.</td></tr>'}</tbody></table>` +

    `<div class="pr-sec pr-break">7. Pedidos atendidos em ${ref}</div>
     <table><thead><tr><th>Data</th><th>Departamento</th><th>Material</th><th class="num">Qtd.</th>
       <th class="num">Vl. unit.</th><th class="num">Vl. total</th><th>Solicitante</th></tr></thead>
       <tbody>${pedidosRows || '<tr><td colspan="7">Nenhuma saída no mês de referência.</td></tr>'}</tbody>
       <tfoot><tr><td colspan="5">TOTAL — ${fmtNum(pedidos.length)} pedido(s)</td>
         <td class="num">${fmtBRL(totalPedidos)}</td><td></td></tr></tfoot></table>` +

    rodape('Saídas valorizadas pelo custo médio ponderado vigente na data de cada lançamento. ' +
      'O acumulado compara o mesmo número de meses nos dois anos.') +
    assinaturas();
}

// -------------------------------------------------------------- retrospecto

export function buildRetrospectDoc(RE) {
  const anoTxt = String(RE.year);
  const linhaMatriz = (g) => {
    const linhas = [...g.linhas].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
    return `<table><thead><tr><th>Departamento</th>${MESES_ABR.map((m) => `<th class="num">${m}</th>`).join('')}
        <th class="num">Total</th></tr></thead>
      <tbody>${linhas.map((l) => `<tr><td>${escapeHtml(l.name)}</td>
        ${l.meses.map((v, i) => `<td class="num">${g.origem[i] ? (v ? moedaSimples(v) : '0') : '—'}</td>`).join('')}
        <td class="num">${moedaSimples(l.total)}</td></tr>`).join('')
        || `<tr><td colspan="14">Sem lançamentos em ${g.year}.</td></tr>`}</tbody>
      <tfoot><tr><td>TOTAL</td>${g.totaisMes.map((v, i) => `<td class="num">${g.origem[i] ? moedaSimples(v) : '—'}</td>`).join('')}
        <td class="num">${moedaSimples(g.total)}</td></tr></tfoot></table>`;
  };

  const comp = RE.comparativo;
  const teto = RE.teto;

  return head('Retrospecto de Custos', `Ano ${anoTxt} — custo mensal por departamento`,
      RE.source === 'ledger' ? 'Fonte: só movimentações do sistema' : 'Fonte: planilha + movimentações') +
    kpiGrid([
      { k: `Total ${anoTxt}`, v: fmtBRL(RE.ref.total), f: `${RE.ref.ultimoMes + 1} mês(es) com lançamento` },
      { k: `Total ${RE.prevYear}`, v: fmtBRL(RE.ant.total), f: 'ano completo' },
      { k: 'Mesmo período', v: fmtBRL(comp.curTotal), f: `${deltaTxt(comp.curTotal, comp.prevTotal)} vs ${fmtBRL(comp.prevTotal)} em ${RE.prevYear}` },
    ]) +
    `<div class="pr-sec">Custo mensal por departamento — ${anoTxt} (R$)</div>` + linhaMatriz(RE.ref) +
    `<div class="pr-sec">Custo mensal por departamento — ${RE.prevYear} (R$)</div>` + linhaMatriz(RE.ant) +
    `<div class="pr-sec pr-break">Mesmo período nos dois anos${comp.mesLimite >= 0 ? ` (janeiro a ${MESES[comp.mesLimite].toLowerCase()})` : ''}</div>
     <table><thead><tr><th>Departamento</th><th class="num">${RE.prevYear}</th><th class="num">${anoTxt}</th>
       <th class="num">Variação</th><th class="num">Δ</th></tr></thead>
       <tbody>${comp.linhas.map((l) => `<tr><td>${escapeHtml(l.name)}</td><td class="num">${fmtBRL(l.prev)}</td>
         <td class="num">${fmtBRL(l.cur)}</td><td class="num">${fmtBRL(l.cur - l.prev)}</td>
         <td class="num">${deltaTxt(l.cur, l.prev)}</td></tr>`).join('')
         || '<tr><td colspan="5">Sem período comparável.</td></tr>'}</tbody>
       <tfoot><tr><td>TOTAL</td><td class="num">${fmtBRL(comp.prevTotal)}</td><td class="num">${fmtBRL(comp.curTotal)}</td>
         <td class="num">${fmtBRL(comp.curTotal - comp.prevTotal)}</td><td class="num">${deltaTxt(comp.curTotal, comp.prevTotal)}</td></tr></tfoot>
     </table>` +
    `<div class="pr-sec">Teto de gastos ${anoTxt}</div>
     <div class="pr-note">Base: gasto de ${teto.baseYear}. Fator de ajuste ${fmtNum(Math.round(RE.params.fator * 10000) / 10000)}
       = (1 + ${fmtPct(RE.params.ipca, 1)} de inflação) × (1 − ${fmtPct(RE.params.metaReducao, 1)} de meta de redução).
       Piso mensal de ${fmtBRL(RE.params.pisoMensal)} por departamento.</div>
     <table><thead><tr><th>Departamento</th><th class="num">Base ${teto.baseYear}</th><th class="num">Teto mensal</th>
       <th class="num">Teto anual</th><th class="num">Realizado</th><th class="num">Teto do período</th>
       <th class="num">% do período</th><th>Situação</th></tr></thead>
       <tbody>${teto.linhas.map((l) => `<tr><td>${escapeHtml(l.name)}</td><td class="num">${fmtBRL(l.base)}</td>
         <td class="num">${fmtBRL(l.tetoMensal)}${l.noPiso ? ' *' : ''}</td><td class="num">${fmtBRL(l.tetoAnual)}</td>
         <td class="num">${fmtBRL(l.realizado)}</td><td class="num">${fmtBRL(l.tetoPeriodo)}</td>
         <td class="num">${pctTxt(l.pctPeriodo, 0)}</td>
         <td>${l.status ? limiteBadge(l.status, l.status === 'estourado' ? 'ESTOUROU' : l.status === 'atencao' ? 'Atenção' : 'Dentro') : '—'}</td></tr>`).join('')}</tbody>
       <tfoot><tr><td>TOTAL</td><td class="num">${fmtBRL(teto.baseTotal)}</td><td class="num">${fmtBRL(teto.tetoMensalTotal)}</td>
         <td class="num">${fmtBRL(teto.tetoAnualTotal)}</td><td class="num">${fmtBRL(teto.realizadoTotal)}</td>
         <td class="num">${fmtBRL(teto.tetoPeriodoTotal)}</td>
         <td class="num">${teto.tetoPeriodoTotal > 0 ? fmtPct(teto.realizadoTotal / teto.tetoPeriodoTotal, 0) : '—'}</td><td></td></tr></tfoot>
     </table>` +
    rodape('* teto definido pelo piso mensal, não pelo cálculo sobre a base. ' +
      '"Teto do período" é o teto mensal multiplicado pelos meses já decorridos — é contra ele que a situação é avaliada. ' +
      'Nas matrizes, 0 significa que o setor não consumiu nada no mês; o traço (—) significa que nenhuma fonte cobre ' +
      'aquele mês, o que é diferente de zero.') +
    assinaturas();
}
