// Impressão dos relatórios.
//
// Em vez de mandar a tela para a impressora, cada relatório monta um
// documento próprio (só cabeçalho + tabelas, e no relatório de custo por
// departamento também um gráfico) dentro de #printArea; o css/print.css
// esconde o app e mostra só esse bloco. Motivos: a sidebar, os filtros e os
// demais gráficos interativos não têm função no papel; o WebView do Tauri
// bloqueia window.open (então não dá para abrir uma segunda janela). O
// gráfico embutido usa barrasHSVG (versão de deptHBars.js que devolve a
// string do SVG em vez de desenhar num host do DOM), porque o documento é
// montado inteiro antes de existir qualquer elemento em #printArea.
import { fmtBRL, fmtNum, fmtPct, fmtDateBR, fmtDateTimeBR, fmtMesAnoBR, escapeHtml, deltaInfo, porCurvaDepoisNome } from './format.js';
import { barrasHSVG } from './charts/deptHBars.js';

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

// Logo da FL — uma foto só, configurada no canto da barra lateral (ver
// components/logoFl.js), guardada aqui em memória (setLogo é chamada uma
// vez no login) porque head() monta o documento inteiro ANTES de existir
// qualquer elemento no DOM, sem tempo pra um await no meio do caminho.
let logoDataUrl = null;
export function setLogo(dataUrl) {
  logoDataUrl = dataUrl || null;
}

function head(titulo, sub, extras) {
  return `<div class="pr-head">
    ${logoDataUrl ? `<img class="pr-logo" src="${logoDataUrl}" alt="Logo FL" />` : ''}
    <h1>${escapeHtml(titulo)}</h1>
    <div class="pr-sub"><b>Estoque FL Condomínios</b> — ${escapeHtml(sub)}</div>
    <div class="pr-meta">Emitido em ${fmtDateTimeBR(new Date().toISOString())}${extras ? ' · ' + escapeHtml(extras) : ''}</div>
  </div>`;
}

function assinaturas() {
  return `<div class="pr-sign"><div>Responsável pelo almoxarifado</div><div>Gerência</div></div>` +
    `<div class="pr-copy">© 2026 CMT360 • Soluções Inteligentes. Todos os direitos reservados.</div>`;
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
   documento não ficar preso no DOM entre uma impressão e outra.
   `opts.landscape` liga a página nomeada "pr-wide" (A4 paisagem, ver
   css/print.css) — usada pelo Retrospecto, cuja matriz de 14 colunas não
   cabe em retrato. */
export function printDocument(html, opts) {
  opts = opts || {};
  const area = document.getElementById('printArea');
  area.innerHTML = html;
  area.classList.toggle('pr-landscape', !!opts.landscape);
  const cleanup = () => {
    area.innerHTML = '';
    area.classList.remove('pr-landscape');
    window.removeEventListener('afterprint', cleanup);
  };
  window.addEventListener('afterprint', cleanup);
  window.print();
  // Nem todo WebView dispara afterprint; a rede de segurança evita segurar o
  // documento para sempre sem cortar a impressão de quem dispara certo.
  setTimeout(cleanup, 60000);
}

// ------------------------------------------------------- relatório de estoque

export function buildEstoqueDoc(R, opts) {
  opts = opts || {};
  const ref = `${MESES[R.m]}/${R.y}`;
  const base = opts.soComEstoque ? R.itens.filter((i) => i.qty > 0) : R.itens;
  const itens = [...base].sort(porCurvaDepoisNome);
  const k = R.kpi;

  const linhas = itens.map((i) => `<tr>
    <td>${escapeHtml(i.sku || '—')}</td>
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

  const escopo = [];
  if (R.deptFilter) escopo.push(`Departamento: ${R.deptFilter}`);
  if (opts.soComEstoque) escopo.push('Somente itens com estoque (zerados omitidos)');

  return head('Relatório de Estoque', `Posição de fechamento de ${ref}`, escopo.join(' · ')) +
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
       <thead><tr><th>SKU</th><th>Material</th><th>Categoria</th><th>Un.</th><th class="num">Saldo</th><th class="num">Mín.</th>
         <th class="num">Custo médio</th><th class="num">Valor</th><th class="num">% valor</th><th>ABC</th>
         <th class="num">Cons./mês</th><th class="num">Cobert.</th><th>Situação</th></tr></thead>
       <tbody>${linhas || `<tr><td colspan="13">${opts.soComEstoque && R.itens.length ? 'Nenhum item com saldo em estoque.' : 'Nenhum produto cadastrado.'}</td></tr>`}</tbody>
       <tfoot><tr><td colspan="7">TOTAL GERAL</td><td class="num">${fmtBRL(R.valorTotal)}</td>
         <td class="num">100%</td><td colspan="4"></td></tr></tfoot>
     </table>` +
    rodape('Saldo e custo médio reconstruídos cronologicamente pelas movimentações registradas. ' +
      'Cobertura = saldo ÷ consumo médio mensal da janela.') +
    assinaturas();
}

// ----------------------------------------------------- relatório analítico

// O filtro "só com estoque" (opts.soComEstoque) não se aplica aqui: nenhuma
// seção deste relatório lista a posição de estoque item a item — "Materiais
// mais consumidos" é sobre movimentação do período, não saldo atual, e
// removê-los pelo saldo de hoje esconderia justamente o que mais girou.
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
      <td>${escapeHtml((prod && prod.sku) || '') || '—'}</td>
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
     <table><thead><tr><th>SKU</th><th>Material</th><th class="num">Quantidade</th><th class="num">Valor</th></tr></thead>
       <tbody>${top.map((i) => `<tr><td>${escapeHtml(i.sku || '—')}</td><td>${escapeHtml(i.name)}</td>
         <td class="num">${fmtNum(i.consumoRef.qty)} ${escapeHtml(i.unit)}</td>
         <td class="num">${fmtBRL(i.consumoRef.val)}</td></tr>`).join('')
         || '<tr><td colspan="4">Nenhum material consumido no período.</td></tr>'}</tbody></table>` +

    `<div class="pr-sec pr-break">7. Pedidos atendidos em ${ref}</div>
     <table><thead><tr><th>Data</th><th>Departamento</th><th>SKU</th><th>Material</th><th class="num">Qtd.</th>
       <th class="num">Vl. unit.</th><th class="num">Vl. total</th><th>Solicitante</th></tr></thead>
       <tbody>${pedidosRows || '<tr><td colspan="8">Nenhuma saída no mês de referência.</td></tr>'}</tbody>
       <tfoot><tr><td colspan="6">TOTAL — ${fmtNum(pedidos.length)} pedido(s)</td>
         <td class="num">${fmtBRL(totalPedidos)}</td><td></td></tr></tfoot></table>` +

    rodape('Saídas valorizadas pelo custo médio ponderado vigente na data de cada lançamento. ' +
      'O acumulado compara o mesmo número de meses nos dois anos.') +
    assinaturas();
}

// ------------------------------------------- custo mensal por departamento

export function buildCustoDeptoDoc(R) {
  const ref = `${MESES[R.m]}/${R.y}`;
  const k = R.kpi;
  const byId = new Map(R.itens.map((i) => [i.id, i]));

  // --- resumo por departamento: mesmo cálculo da tela (Relatório Mensal) ---
  const nomes = new Set([...Object.keys(R.deptMes), ...Object.keys(R.deptYTD)]);
  const resumo = [...nomes].map((n) => {
    const yy = R.deptYTD[n] || { cur: 0, prev: 0 };
    return { name: n, mes: R.deptMes[n] || 0, cur: yy.cur, prev: yy.prev };
  }).filter((r) => r.mes || r.cur || r.prev).sort((a, b) => b.mes - a.mes || a.name.localeCompare(b.name, 'pt-BR'));
  const totMes = resumo.reduce((s, r) => s + r.mes, 0);

  // --- detalhamento: cada pedido do mês, agrupado por departamento (mesma
  // fonte da seção "Pedidos" dos outros relatórios, só que reagrupada) ---
  const porDepto = new Map();
  for (const p of R.pedidos) {
    const dep = p.recipient || '(sem departamento)';
    if (!porDepto.has(dep)) porDepto.set(dep, []);
    porDepto.get(dep).push(p);
  }
  const grupos = [...porDepto.entries()].map(([dep, ps]) => {
    const linhas = [...ps].sort((a, b) => a._ts - b._ts);
    const subtotal = linhas.reduce((s, p) => s + p.qty * p.unitPrice, 0);
    return { dep, linhas, subtotal };
  }).sort((a, b) => b.subtotal - a.subtotal || a.dep.localeCompare(b.dep, 'pt-BR'));

  const detalhamento = grupos.map((g) => {
    const linhas = g.linhas.map((p) => {
      const prod = byId.get(p.productId);
      const val = p.qty * p.unitPrice;
      return `<tr><td>${fmtDateBR(p._ts)}</td>
        <td>${escapeHtml((prod && prod.sku) || '') || '—'}</td>
        <td>${escapeHtml(prod ? prod.name : '(produto excluído)')}</td>
        <td class="num">${fmtNum(p.qty)} ${escapeHtml(prod ? prod.unit : '')}</td>
        <td class="num">${fmtBRL(p.unitPrice)}</td><td class="num">${fmtBRL(val)}</td>
        <td>${escapeHtml(p.requester || 'não informado')}</td></tr>`;
    }).join('');
    // Cada departamento é o próprio <tbody> (ver css/print.css
    // #printArea tbody.pr-group-body): um <tr> sozinho não pode ser
    // impedido de se separar de um grupo de outros <tr> na paginação, mas
    // um <tbody> inteiro pode — é o que garante que a última linha de um
    // departamento nunca continue na página seguinte sem o cabeçalho do
    // grupo, sem contexto de a quem ela pertence.
    return `<tbody class="pr-group-body"><tr class="pr-group"><td colspan="7"><b>${escapeHtml(g.dep)}</b> — ` +
      `${fmtNum(g.linhas.length)} pedido(s), ${fmtBRL(g.subtotal)}</td></tr>${linhas}</tbody>`;
  }).join('');

  const rows = resumo.map((r) => ({ label: r.name, value: r.mes }));
  const grafico = rows.length
    ? barrasHSVG(rows, 680, { tipLabel: `Custo em ${ref}`, aria: 'Custo por departamento no mês', interactive: false })
    : `<div class="chart-empty">Nenhuma saída no mês de referência.</div>`;

  const escopo = [];
  if (R.deptFilter) escopo.push(`Departamento: ${R.deptFilter}`);
  if (R.refEmCurso) escopo.push(`Mês em curso (${R.diasDecorridos} de ${R.diasNoMes} dias)`);

  return head('Custo Mensal por Departamento', `Referência ${ref}`, escopo.join(' · ')) +
    kpiGrid([
      { k: 'Custo total do mês', v: fmtBRL(k.consumo), f: `${fmtNum(resumo.length)} departamento(s) com saída` },
      { k: 'Pedidos atendidos', v: fmtNum(k.pedidos), f: `${fmtNum(k.itensDistintos)} materiais distintos` },
      { k: 'Ticket médio', v: fmtBRL(k.ticket), f: 'custo do mês ÷ pedidos' },
    ]) +

    `<div class="pr-sec">1. Resumo por departamento</div>
     <table><thead><tr><th>Departamento</th><th class="num">${ref}</th><th class="num">% do mês</th>
       <th class="num">Acum. ${R.y}</th><th class="num">Acum. ${R.y - 1}</th><th class="num">Δ</th></tr></thead>
       <tbody>${resumo.map((r) => `<tr><td>${escapeHtml(r.name)}</td><td class="num">${fmtBRL(r.mes)}</td>
         <td class="num">${totMes > 0 ? fmtPct(r.mes / totMes, 1) : '—'}</td>
         <td class="num">${fmtBRL(r.cur)}</td><td class="num">${fmtBRL(r.prev)}</td>
         <td class="num">${deltaTxt(r.cur, r.prev)}</td></tr>`).join('')
         || '<tr><td colspan="6">Nenhuma saída registrada.</td></tr>'}</tbody>
       <tfoot><tr><td>TOTAL</td><td class="num">${fmtBRL(totMes)}</td><td class="num">100%</td>
         <td class="num">${fmtBRL(R.ytdCur)}</td><td class="num">${fmtBRL(R.ytdPrev)}</td>
         <td class="num">${deltaTxt(R.ytdCur, R.ytdPrev)}</td></tr></tfoot></table>` +

    `<div class="pr-sec pr-break">2. Detalhamento por departamento — ${ref}</div>
     <table><thead><tr><th>Data</th><th>SKU</th><th>Material</th><th class="num">Qtd.</th>
       <th class="num">Vl. unit.</th><th class="num">Vl. total</th><th>Solicitante</th></tr></thead>
       ${detalhamento || '<tbody><tr><td colspan="7">Nenhuma saída no mês de referência.</td></tr></tbody>'}
       <tfoot><tr><td colspan="5">TOTAL — ${fmtNum(R.pedidos.length)} pedido(s)</td>
         <td class="num">${fmtBRL(k.consumo)}</td><td></td></tr></tfoot></table>` +

    `<div class="pr-sec">3. Gráfico — custo por departamento em ${ref}</div>
     <div class="chart-host pr-chart">${grafico}</div>` +

    rodape('Saídas valorizadas pelo custo médio ponderado vigente na data de cada lançamento.') +
    assinaturas();
}

// -------------------------------------------------------------- retrospecto

export function buildRetrospectDoc(RE) {
  const anoTxt = String(RE.year);
  const linhaMatriz = (g) => {
    const linhas = [...g.linhas].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
    return `<table class="pr-matrix"><thead><tr><th>Departamento</th>${MESES_ABR.map((m) => `<th class="num">${m}</th>`).join('')}
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

// ------------------------------------------------ lista de separação (picking)

/* Folha que vai para o almoxarifado separar os pedidos. Duas partes, porque
   são duas tarefas diferentes com a mesma informação:

   1. CONSOLIDADO POR MATERIAL — quanto tirar de cada prateleira, somando
      todos os pedidos. É por aqui que se anda pelo estoque uma vez só, em
      vez de uma vez por pedido.
   2. DETALHE POR DEPARTAMENTO — como dividir o monte separado em pilhas, com
      o número do pedido e quem solicitou.

   Cada linha tem uma quadradinho para marcar à caneta. O conteúdo é sempre o
   dos pedidos EM ABERTO (pendentes e aprovados): pedido entregue já foi
   separado, e rejeitado/cancelado não tem o que separar — imprimi-los seria
   mandar alguém buscar material que ninguém vai receber. */
const SEPARACAO_ABERTOS = ['pendente', 'aprovado'];

function quadradinho() {
  return '<span class="pr-check"></span>';
}

export function buildSeparacaoDoc(requests, availability) {
  const abertos = (requests || []).filter((r) => SEPARACAO_ABERTOS.includes(r.status));
  const porProduto = new Map((availability || []).map((a) => [a.productId, a]));

  if (!abertos.length) {
    return head('Lista de Separação', 'Pedidos em aberto', 'nenhum pedido a separar') +
      '<div class="pr-vazio">Nenhum pedido pendente ou aprovado no momento — não há nada a separar.</div>' +
      assinaturas();
  }

  // ---- 1. consolidado por material ----
  const consolidado = new Map();
  abertos.forEach((r) => (r.items || []).forEach((it) => {
    const atual = consolidado.get(it.productId) || { qty: 0, pedidos: new Set(), item: it };
    atual.qty += it.qty;
    atual.pedidos.add(r.id);
    consolidado.set(it.productId, atual);
  }));

  const linhasConsolidado = [...consolidado.entries()]
    .map(([productId, c]) => {
      const prod = porProduto.get(productId);
      return {
        sku: (prod && prod.sku) || '',
        nome: c.item.productName,
        categoria: (prod && prod.category) || '',
        unidade: c.item.unit || (prod && prod.unit) || '',
        qty: c.qty,
        pedidos: c.pedidos.size,
        saldo: prod ? prod.qty : null,
      };
    })
    // Ordena por categoria e depois por nome: no almoxarifado o material está
    // arrumado por categoria, então essa ordem é a do percurso pelas
    // prateleiras, não a ordem em que os pedidos chegaram.
    .sort((a, b) => a.categoria.localeCompare(b.categoria, 'pt-BR') || a.nome.localeCompare(b.nome, 'pt-BR'));

  const tabelaConsolidado = `<table>
    <thead><tr><th style="width:22pt;">OK</th><th>SKU</th><th>Material</th><th>Categoria</th>
      <th>Un.</th><th class="num">Qtd. a separar</th><th class="num">Saldo</th>
      <th class="num">Pedidos</th></tr></thead>
    <tbody>${linhasConsolidado.map((l) => `<tr>
      <td>${quadradinho()}</td>
      <td>${escapeHtml(l.sku) || '—'}</td>
      <td><b>${escapeHtml(l.nome)}</b></td>
      <td>${escapeHtml(l.categoria) || '—'}</td>
      <td>${escapeHtml(l.unidade)}</td>
      <td class="num"><b>${fmtNum(l.qty)}</b></td>
      <td class="num">${l.saldo === null ? '—' : fmtNum(l.saldo)}</td>
      <td class="num">${fmtNum(l.pedidos)}</td></tr>`).join('')}</tbody>
    <tfoot><tr><td colspan="5">TOTAL — ${fmtNum(linhasConsolidado.length)} material(is) distinto(s)</td>
      <td class="num">${fmtNum(linhasConsolidado.reduce((s, l) => s + l.qty, 0))}</td>
      <td colspan="2"></td></tr></tfoot>
  </table>`;

  // ---- 2. detalhe por departamento ----
  const porDepto = new Map();
  abertos.forEach((r) => {
    const chave = r.departmentName || '(sem departamento)';
    if (!porDepto.has(chave)) porDepto.set(chave, []);
    porDepto.get(chave).push(r);
  });

  const blocos = [...porDepto.entries()]
    .sort((a, b) => a[0].localeCompare(b[0], 'pt-BR'))
    .map(([depto, pedidos]) => {
      const cards = pedidos
        .sort((a, b) => String(a.createdAt).localeCompare(String(b.createdAt)))
        .map((r) => {
          const codigo = '#' + String(r.id).slice(-6).toUpperCase();
          const situacao = r.status === 'aprovado'
            ? '<span class="pr-st-good">Aprovado</span>'
            : '<span class="pr-st-warn">Aguardando aprovação</span>';
          const itens = (r.items || []).map((it) => {
            const prod = porProduto.get(it.productId);
            return `<tr><td>${quadradinho()}</td>
              <td>${escapeHtml((prod && prod.sku) || '') || '—'}</td>
              <td>${escapeHtml(it.productName)}</td>
              <td class="num"><b>${fmtNum(it.qty)}</b> ${escapeHtml(it.unit || '')}</td></tr>`;
          }).join('');
          return `<div class="pr-keep pr-pedido">
            <div class="pr-pedido-cab"><b>${codigo}</b> · ${escapeHtml(r.requesterName)} ·
              ${escapeHtml(fmtDateTimeBR(r.createdAt))} · ${situacao}
              ${r.obs ? `<div class="pr-pedido-obs">Obs.: ${escapeHtml(r.obs)}</div>` : ''}</div>
            <table><thead><tr><th style="width:22pt;">OK</th><th style="width:60pt;">SKU</th>
              <th>Material</th><th class="num" style="width:80pt;">Qtd.</th></tr></thead>
              <tbody>${itens}</tbody></table>
          </div>`;
        }).join('');
      const totalItens = pedidos.reduce((s, r) => s + (r.items || []).length, 0);
      return `<div class="pr-sec">${escapeHtml(depto)} — ${fmtNum(pedidos.length)} pedido(s), ` +
        `${fmtNum(totalItens)} linha(s)</div>${cards}`;
    }).join('');

  const totalPedidos = abertos.length;
  const totalSetores = porDepto.size;
  return head('Lista de Separação', 'Pedidos em aberto (pendentes e aprovados)',
      `${fmtNum(totalPedidos)} pedido(s) · ${fmtNum(totalSetores)} setor(es)`) +
    `<div class="pr-sec">1. Materiais a separar — consolidado</div>` +
    `<div class="pr-nota">Some tudo de uma vez: esta é a quantidade total a retirar de cada prateleira, ` +
    `juntando todos os pedidos. Ordenado por categoria, na ordem do percurso pelo estoque.</div>` +
    tabelaConsolidado +
    `<div class="pr-sec pr-break">2. Separação por departamento</div>` +
    `<div class="pr-nota">Agora divida o que foi separado, pedido a pedido.</div>` +
    blocos +
    rodape('Confira cada linha ao separar. A baixa no estoque só acontece quando a entrega for ' +
      'confirmada no sistema — esta folha não dá baixa em nada.') +
    assinaturas();
}

// --------------------------------------------------- catálogo de empresas

/* `grupos` é exatamente a estrutura que catalogoAgrupado() (catalogoEmpresas.js)
   já filtrou por setor/busca — o mesmo que está na tela vira o papel, sem uma
   segunda regra de filtro que pudesse divergir da primeira. Uma linha por
   par (especialidade, empresa): quem atende duas especialidades do mesmo
   setor aparece duas vezes, igual à tela. */
export function buildCatalogoEmpresasDoc(grupos, filtroTxt) {
  const totalEmpresas = new Set(
    grupos.flatMap((g) => g.especialidades.flatMap((e) => e.empresas.map((x) => x.id)))).size;

  const blocos = grupos.map((g) => {
    const linhas = g.especialidades.flatMap(({ esp, empresas }) => empresas.map((e) => {
      // Mesmo critério da tela (cartaoEmpresa em catalogoEmpresas.js): o nome
      // fantasia é quem aparece em destaque, a razão social vira o detalhe.
      const nomeCompleto = e.nomeFantasia && e.nomeFantasia !== e.nome
        ? `${escapeHtml(e.nomeFantasia)} <span class="muted">(${escapeHtml(e.nome)})</span>`
        : escapeHtml(e.nomeFantasia || e.nome);
      const local = [e.cidade, e.estado].filter(Boolean).join('/');
      return `<tr>
        <td>${escapeHtml(esp.nome)}</td>
        <td><b>${nomeCompleto}</b></td>
        <td>${local ? escapeHtml(local) : '—'}</td>
        <td>${e.telefone ? escapeHtml(e.telefone) : '—'}</td>
        <td>${e.emails ? escapeHtml(e.emails) : '—'}</td></tr>`;
    }));
    if (!linhas.length) return '';
    return `<div class="pr-sec">${escapeHtml(g.setor.label)}</div>
      <table><thead><tr><th>Especialidade</th><th>Empresa</th><th>Cidade/UF</th>
        <th>Telefone</th><th>E-mail</th></tr></thead>
        <tbody>${linhas.join('')}</tbody></table>`;
  }).join('');

  if (!blocos) {
    return head('Catálogo de Empresas', filtroTxt, 'nenhum resultado') +
      '<div class="pr-vazio">Nenhuma empresa encontrada para este filtro.</div>';
  }

  return head('Catálogo de Empresas', filtroTxt, `${totalEmpresas} empresa(s)`) + blocos;
}

// ------------------------------------------------------------- parceiros

/* `lista` é exatamente o que parceirosFiltrados() (parceiros.js) já filtrou
   por setor/busca — mesmo critério de buildCatalogoEmpresasDoc acima: o que
   está na tela é o que vira papel, sem uma segunda regra de filtro. */
export function buildParceirosDoc(lista, filtroTxt) {
  if (!lista.length) {
    return head('Parceiros', filtroTxt, 'nenhum resultado') +
      '<div class="pr-vazio">Nenhum parceiro encontrado para este filtro.</div>';
  }

  const linhas = lista.map((e) => {
    const nomeCompleto = e.nomeFantasia && e.nomeFantasia !== e.nome
      ? `${escapeHtml(e.nomeFantasia)} <span class="muted">(${escapeHtml(e.nome)})</span>`
      : escapeHtml(e.nomeFantasia || e.nome);
    const local = [e.cidade, e.estado].filter(Boolean).join('/');
    const esp = e.especialidades.map((x) => escapeHtml(x.nome)).join(', ');
    return `<tr>
      <td><b>${nomeCompleto}</b></td>
      <td>${esp || '—'}</td>
      <td>${local ? escapeHtml(local) : '—'}</td>
      <td>${e.telefone ? escapeHtml(e.telefone) : '—'}</td>
      <td>${e.emails ? escapeHtml(e.emails) : '—'}</td></tr>`;
  }).join('');

  return head('Parceiros', filtroTxt, `${lista.length} parceiro(s)`) +
    `<table><thead><tr><th>Empresa</th><th>Especialidades</th><th>Cidade/UF</th>
      <th>Telefone</th><th>E-mail</th></tr></thead>
      <tbody>${linhas}</tbody></table>` +
    rodape('Empresas parceiras, para acionar num chamado emergencial.');
}

// -------------------------------------------- gestão sos: delta síndicos

/* `lista` é exatamente o que a tela já filtrou por mês (deltaSindicos.js) —
   mesmo critério dos demais documentos: o papel nunca refiltra por conta
   própria, só formata o que está à vista. */
export function buildDeltaSindicosDoc(lista, filtroTxt) {
  const totalVenda = lista.reduce((s, d) => s + d.venda, 0);
  const totalComissao = lista.reduce((s, d) => s + d.comissao, 0);

  const linhas = lista.map((d) => `<tr>
    <td class="num">${d.numero}</td>
    <td>${escapeHtml(d.condominioNome)}</td>
    <td>${d.sindico ? escapeHtml(d.sindico) : '—'}</td>
    <td>${d.gerenteNome ? escapeHtml(d.gerenteNome) : '—'}</td>
    <td class="num">${fmtBRL(d.venda)}</td>
    <td class="num">${fmtPct(d.porcentagem / 100, 2)}</td>
    <td class="num">${fmtBRL(d.comissao)}</td>
    <td>${fmtMesAnoBR(d.dataReferencia)}</td></tr>`).join('');

  return head('Delta Síndicos', filtroTxt, `${lista.length} lançamento(s)`) +
    kpiGrid([
      { k: 'Total de vendas', v: fmtBRL(totalVenda) },
      { k: 'Total de comissão', v: fmtBRL(totalComissao) },
      { k: 'Lançamentos', v: fmtNum(lista.length) },
    ]) +
    `<table><thead><tr><th class="num">ID</th><th>Condomínio</th><th>Síndico</th><th>Gerente</th>
      <th class="num">Venda</th><th class="num">%</th><th class="num">Comissão</th>
      <th>Mês/Ano</th></tr></thead>
      <tbody>${linhas}</tbody>
      <tfoot><tr><td colspan="3">TOTAL</td><td class="num">${fmtBRL(totalVenda)}</td><td></td>
        <td class="num">${fmtBRL(totalComissao)}</td><td></td></tr></tfoot></table>` +
    rodape('Comissão = venda × porcentagem ÷ 100, calculada por lançamento. O síndico é o gravado ' +
      'no próprio lançamento — trocar o síndico do condomínio depois não altera o que já foi lançado.') +
    assinaturas();
}

// ---------------------------------------------------- gestão sos: painel

/* `D` já vem inteiramente calculado pela tela (sosPainel.js) — mesmo critério
   dos demais documentos deste arquivo: o papel nunca refiltra ou recalcula
   por conta própria, só formata o que já está na tela. */
export function buildSosPainelDoc(D) {
  const totalVenda = D.lista.reduce((s, x) => s + x.venda, 0);
  const totalComissao = D.lista.reduce((s, x) => s + x.comissao, 0);
  const pctMedia = totalVenda > 0 ? totalComissao / totalVenda : 0;

  const tabelaRanking = (titulo, linhas, rotuloValor) => `<div class="pr-sec">${escapeHtml(titulo)}</div>
    <table><thead><tr><th>#</th><th>Nome</th><th class="num">${escapeHtml(rotuloValor)}</th></tr></thead>
      <tbody>${linhas.length ? linhas.map((l, i) => `<tr><td>${i + 1}</td>
        <td>${escapeHtml(l.label)}</td><td class="num">${fmtBRL(l.value)}</td></tr>`).join('')
        : '<tr><td colspan="3">Nenhum lançamento no período.</td></tr>'}</tbody></table>`;

  const evolucao = `<div class="pr-sec">Evolução mensal de comissão — ${D.evolucaoAno}</div>
    <table><thead><tr>${D.evolucaoMeses.map((m) => `<th class="num">${MESES_ABR[MESES.indexOf(m.label)]}</th>`).join('')}
      <th class="num">Total</th></tr></thead>
      <tbody><tr>${D.evolucaoMeses.map((m) => `<td class="num">${m.value ? moedaSimples(m.value) : '0'}</td>`).join('')}
        <td class="num">${moedaSimples(D.evolucaoMeses.reduce((s, m) => s + m.value, 0))}</td></tr></tbody></table>`;

  return head('Painel — Gestão SOS', D.filtroTxt, `${D.lista.length} serviço(s)`) +
    kpiGrid([
      { k: 'Total de vendas', v: fmtBRL(totalVenda) },
      { k: 'Total de comissão', v: fmtBRL(totalComissao) },
      { k: 'Serviços lançados', v: fmtNum(D.lista.length) },
      { k: '% média de comissão', v: D.lista.length ? fmtPct(pctMedia, 1) : '—' },
    ]) +
    evolucao +
    tabelaRanking('Ranking por Gerente', D.rankGerente, 'Comissão') +
    tabelaRanking('Ranking por Parceiro', D.rankParceiro, 'Comissão') +
    tabelaRanking('Ranking por Condomínio', D.rankCondominio, 'Venda') +
    rodape('Comissão = venda × porcentagem ÷ 100, somada por lançamento. Um serviço já fechado continua contando aqui — fechar só trava a edição.') +
    assinaturas();
}

export function buildServicosDoc(lista, filtroTxt) {
  const totalVenda = lista.reduce((s, x) => s + x.venda, 0);
  const totalComissao = lista.reduce((s, x) => s + x.comissao, 0);
  const comissaoPaga = lista.reduce((s, x) => s + (x.pago ? x.comissao : 0), 0);
  const comissaoPendente = totalComissao - comissaoPaga;

  const linhas = lista.map((s) => `<tr>
    <td class="num">${s.numero}</td>
    <td>${s.codigo ? escapeHtml(s.codigo) : '—'}</td>
    <td>${escapeHtml(s.condominioNome)}</td>
    <td>${s.gerenteNome ? escapeHtml(s.gerenteNome) : '—'}</td>
    <td>${s.parceiroNome ? escapeHtml(s.parceiroNome) : '—'}</td>
    <td class="num">${fmtBRL(s.venda)}</td>
    <td class="num">${fmtPct(s.porcentagem / 100, 2)}</td>
    <td class="num">${fmtBRL(s.comissao)}</td>
    <td>${s.pago ? 'Pago' : 'Pendente'}</td>
    <td>${fmtMesAnoBR(s.dataReferencia)}</td></tr>`).join('');

  return head('Serviços — Gestão SOS', filtroTxt, `${lista.length} serviço(s)`) +
    kpiGrid([
      { k: 'Total de vendas', v: fmtBRL(totalVenda) },
      { k: 'Comissão paga', v: fmtBRL(comissaoPaga) },
      { k: 'Comissão pendente (não pago)', v: fmtBRL(comissaoPendente) },
      { k: 'Serviços no filtro', v: fmtNum(lista.length) },
    ]) +
    `<table><thead><tr><th class="num">ID</th><th>Código</th><th>Condomínio</th><th>Gerente</th>
      <th>Parceiro</th><th class="num">Venda</th><th class="num">%</th><th class="num">Comissão</th>
      <th>Status</th><th>Mês/Ano</th></tr></thead>
      <tbody>${linhas}</tbody>
      <tfoot><tr><td colspan="5"><b>Total</b></td><td class="num"><b>${fmtBRL(totalVenda)}</b></td>
        <td></td><td class="num"><b>${fmtBRL(totalComissao)}</b></td><td></td><td></td></tr></tfoot></table>` +
    rodape('Comissão = venda × porcentagem ÷ 100, somada por lançamento. Um serviço já fechado continua contando aqui.') +
    assinaturas();
}

export function buildDashboardFechamentoDoc(dash, filtroTxt) {
  // Quem fica com comissão zero no mês não soma nada ao relatório — só
  // polui o papel. A tela de trabalho continua mostrando todo mundo (é
  // onde se corrige um % zerado por engano); só o relatório impresso some
  // com essas linhas.
  const gerentesComComissao = dash.gerentes.filter((g) => Math.abs(g.comissao) > 0.004);
  const gerentesTabela = `<div class="pr-sec">Gerentes</div>
    <table><thead><tr><th>Gerente</th><th class="num">Recebido</th>
      <th class="num">Carteira</th><th class="num">Meta</th><th class="num">Eficácia</th><th class="num">(%)</th>
      <th class="num">Descontos</th><th class="num">Comissão</th><th class="num">Retido p/ FL</th></tr></thead>
    <tbody>${gerentesComComissao.length ? gerentesComComissao.map((g) => `<tr>
        <td>${escapeHtml(g.gerenteNome)}</td>
        <td class="num">${moedaSimples(g.recebido)}</td>
        <td class="num">${g.carteira}</td><td class="num">${moedaSimples(g.meta)}</td>
        <td class="num">${fmtPct(g.eficacia / 100, 0)}</td>
        <td class="num">${fmtPct(g.porcentagem / 100, 0)}</td><td class="num">${moedaSimples(g.descontos)}</td>
        <td class="num">${moedaSimples(g.comissao)}</td><td class="num">${moedaSimples(g.retido)}</td></tr>`).join('')
      : `<tr><td colspan="9">${dash.gerentes.length ? 'Nenhum gerente com comissão neste mês.' : 'Nenhum gerente cadastrado.'}</td></tr>`}</tbody>
    <tfoot><tr><td colspan="7"><b>Gerência líquido</b></td>
      <td class="num"><b>${moedaSimples(dash.gerenciaLiquido)}</b></td>
      <td class="num"><b>${moedaSimples(dash.retido)}</b></td></tr></tfoot></table>`;

  // Mesmo critério dos Gerentes acima: parceira sem nenhum recebido no mês
  // não soma nada ao relatório — só interessa entrar aqui quem tem dado de
  // produtividade de verdade. A tela de trabalho continua mostrando todas.
  const empresasComRecebido = dash.empresas.filter((e) => Math.abs(e.recebidos) > 0.004);
  const empresasTabela = `<div class="pr-sec">Empresas parceiras</div>
    <table><thead><tr><th>Empresa</th><th class="num">Recebidos</th></tr></thead>
    <tbody>${empresasComRecebido.length ? empresasComRecebido.map((e) => `<tr>
        <td>${escapeHtml(e.empresaNome)}</td><td class="num">${moedaSimples(e.recebidos)}</td></tr>`).join('')
      : `<tr><td colspan="2">${dash.empresas.length ? 'Nenhuma parceira com movimento neste mês.' : 'Nenhuma parceira cadastrada.'}</td></tr>`}</tbody></table>`;

  const totalVendaDelta = dash.deltaSindicos.reduce((s, d) => s + d.venda, 0);
  const totalComissaoDelta = dash.deltaSindicos.reduce((s, d) => s + d.comissao, 0);
  const deltaTabela = dash.deltaSindicos.length ? `<div class="pr-sec">Delta Síndicos do mês</div>
    <table><thead><tr><th>Condomínio</th><th>Síndico</th><th>Gerente</th>
      <th class="num">Venda</th><th class="num">%</th><th class="num">Comissão</th></tr></thead>
    <tbody>${dash.deltaSindicos.map((d) => `<tr>
        <td>${escapeHtml(d.condominioNome)}</td><td>${escapeHtml(d.sindico || '')}</td>
        <td>${escapeHtml(d.gerenteNome || '')}</td><td class="num">${moedaSimples(d.venda)}</td>
        <td class="num">${fmtPct(d.porcentagem / 100, 0)}</td>
        <td class="num">${moedaSimples(d.comissao)}</td></tr>`).join('')}</tbody>
    <tfoot><tr><td colspan="3"><b>Total</b></td>
      <td class="num"><b>${moedaSimples(totalVendaDelta)}</b></td><td></td>
      <td class="num"><b>${moedaSimples(totalComissaoDelta)}</b></td></tr></tfoot></table>` : '';

  const rateioTabela = `<div class="pr-sec">Distribuição de Suprimentos</div>
    <table><thead><tr><th>Destino</th><th class="num">Valor</th></tr></thead>
    <tbody>${dash.distribuicaoCompras.map((l) => `<tr>
        <td>${escapeHtml(l.rotulo)}</td><td class="num">${moedaSimples(l.valor)}</td></tr>`).join('')}</tbody></table>`;

  return head('Dashboard de Fechamento', filtroTxt) +
    kpiGrid([
      { k: 'Arrecadado', v: fmtBRL(dash.arrecadado) },
      { k: 'Liberado p/ comissão', v: fmtBRL(dash.liberadoParaComissao) },
      { k: 'Gerência líquido', v: fmtBRL(dash.gerenciaLiquido) },
      { k: 'Retido para a FL', v: fmtBRL(dash.retido) },
    ]) +
    gerentesTabela +
    rateioTabela +
    empresasTabela +
    deltaTabela +
    rodape('Retido = fatia da comissão perdida por eficácia abaixo de 100% da meta — nunca é paga a ninguém, fica com a FL.') +
    assinaturas();
}

// -------------------------------------------------- gestão sos: pagamentos

const TIPO_PAGAMENTO_LABEL = { gerente: 'Gerente', suprimento: 'Suprimentos', delta: 'Delta' };

// `linhas` já vem filtrada só com o que foi AUTORIZADO (ver
// programarPagamento.js/historicoPagamentos.js) — o impresso nunca lista
// quem não foi autorizado, mesmo que tivesse um valor sugerido.
export function buildPagamentosDoc(linhas, mesLabel) {
  const total = linhas.reduce((s, l) => s + l.valor, 0);
  const corpo = linhas.map((l) => `<tr>
      <td>${escapeHtml(l.nome)}</td>
      <td>${TIPO_PAGAMENTO_LABEL[l.tipo] || escapeHtml(l.tipo)}</td>
      <td>${l.chavePix ? escapeHtml(l.chavePix) : '—'}</td>
      <td class="num">${fmtBRL(l.valor)}</td></tr>`).join('');

  return head('Lista de Pagamentos', mesLabel, `${linhas.length} pagamento(s) autorizado(s)`) +
    kpiGrid([
      { k: 'Total autorizado', v: fmtBRL(total) },
      { k: 'Pagamentos', v: fmtNum(linhas.length) },
    ]) +
    `<table><thead><tr><th>Nome</th><th>Tipo</th><th>Chave PIX</th><th class="num">Valor</th></tr></thead>
      <tbody>${corpo}</tbody>
      <tfoot><tr><td colspan="3">TOTAL</td><td class="num">${fmtBRL(total)}</td></tr></tfoot></table>` +
    rodape('Lista apenas com os pagamentos autorizados em Programar pagamento — quem não foi autorizado não aparece aqui.') +
    assinaturas();
}

// ------------------------------------------------------ orçamentos: mapa

// `propostas` já vem filtrada pela tela (modalEnviarCliente/orcamentos.js) —
// só as escolhidas pelo usuário para o cliente ver, mesmo critério dos
// demais documentos deste arquivo: o papel nunca refiltra por conta própria.
// É o primeiro passo do fluxo de "Enviar para o cliente": o mapa existe pra
// o cliente comparar e decidir ANTES de qualquer e-mail sair.
export function buildOrcamentoMapaDoc(ordem, propostas) {
  const ordenadas = [...propostas].sort((a, b) => b.valor - a.valor);
  const linhas = ordenadas.map((p) => `<tr>
      <td>${escapeHtml(p.empresaNome)}</td>
      <td class="num">${fmtBRL(p.valor)}</td>
      <td>${p.recomendada ? '<span class="pill pill-ok">★ Recomendada pela FL</span>' : ''}</td></tr>`).join('');

  return head('Mapa de Orçamentos', ordem.condominioNome || ordem.descricao,
      `${propostas.length} proposta(s) — Pedido: ${ordem.descricao}`) +
    `<table><thead><tr><th>Empresa</th><th class="num">Valor</th><th>Observação</th></tr></thead>
      <tbody>${linhas || '<tr><td colspan="3">Nenhuma proposta selecionada.</td></tr>'}</tbody></table>` +
    rodape('Mapa comparativo para análise e decisão — ordenado do mais caro pro mais barato.') +
    assinaturas();
}
