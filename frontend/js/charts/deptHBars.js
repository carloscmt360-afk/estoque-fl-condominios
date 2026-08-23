import { VIZ, hostW, txt, barPath, measureText, truncToWidth, emptyChart, tipAttr, gradientDefs } from './palette.js';
import { fmtBRL, escapeHtml } from '../format.js';

/* Barras horizontais — substitui a pizza de N fatias do relatório em
   planilha (ilegível acima de ~6 categorias).

   Cor de cada barra, em ordem de prioridade: `row.color` explícito > modo
   "timeline" (série única no tempo — todas as barras na mesma cor, exceto a
   marcada `row.atual`, que usa o tom mais forte, sempre em evidência) >
   categórica (cada barra recebe o próximo tom fixo da paleta — identidade,
   nunca por valor/ranking). Todas em degradê (claro → a cor), nunca cor
   chapada. */
export function drawBarrasH(host, rows, opts) {
  opts = opts || {};
  if (!rows.length) return emptyChart(host, opts.empty || 'Sem dados no período.');
  host.innerHTML = barrasHSVG(rows, hostW(host), opts);
}

function corDaLinha(r, i, opts) {
  if (r.color) return r.color;
  if (opts.mode === 'timeline') {
    return r.atual ? VIZ.sequential.forte : (opts.color || VIZ.sequential.medio);
  }
  return opts.color || VIZ.categorical[i % VIZ.categorical.length];
}

/* Mesmo desenho, mas como string — usada pelos relatórios impressos, que
   montam o documento inteiro antes de existir qualquer elemento no DOM (não
   há host para medir largura real, por isso W vem explícito). */
export function barrasHSVG(rows, W, opts) {
  opts = opts || {};
  if (!rows.length) return `<div class="chart-empty">${escapeHtml(opts.empty || 'Sem dados no período.')}</div>`;
  const fmtV = opts.fmt || fmtBRL;
  const mR = Math.min(Math.max(...rows.map((r) => measureText(fmtV(r.value), 11, 700))) + 14, W * 0.34);
  const mL = Math.min(Math.max(...rows.map((r) => measureText(r.label, 11.5))) + 12, Math.max(110, W * 0.34));
  const rowH = 26, barH = 15, mT = 6, mB = 6;
  const H = mT + mB + rows.length * rowH;
  const pw = Math.max(30, W - mL - mR);
  const maxV = Math.max(...rows.map((r) => r.value)) || 1;

  const cores = rows.map((r, i) => corDaLinha(r, i, opts));
  const grad = gradientDefs(cores, true);

  let g = grad.defsHTML;
  rows.forEach((r, i) => {
    const y = mT + i * rowH;
    const w = (r.value / maxV) * pw;
    const destacar = opts.mode === 'timeline' && r.atual;
    g += txt(mL - 8, y + rowH / 2 + 3.5, truncToWidth(r.label, mL - 12, 11.5),
      { anchor: 'end', size: 11.5, weight: destacar ? 700 : 400, fill: VIZ.ink, tabular: false });
    g += `<path d="${barPath(mL, y + (rowH - barH) / 2, w, barH, 4)}" fill="${grad.urlFor(cores[i])}"></path>`;
    g += txt(mL + w + 7, y + rowH / 2 + 3.5, fmtV(r.value), { size: 11, weight: 700, fill: VIZ.ink });
    if (opts.interactive !== false) {
      const tip = `<b>${escapeHtml(r.label)}</b>${destacar ? ' (mês atual)' : ''}<br>` +
        `${escapeHtml(opts.tipLabel || 'Valor')}: ${fmtBRL(r.value)}${r.tipExtra || ''}`;
      g += `<g class="band" tabindex="0" data-tip="${tipAttr(tip)}">
        <rect class="bandbg" x="0" y="${y}" width="${W}" height="${rowH}" fill="transparent"></rect></g>`;
    }
  });
  return `<svg viewBox="0 0 ${W} ${H}" height="${H}" role="img" aria-label="${opts.aria || 'Comparação por categoria'}">${g}</svg>`;
}
