import { VIZ, hostW, txt, barPath, measureText, truncToWidth, emptyChart, tipAttr } from './palette.js';
import { fmtBRL, escapeHtml } from '../format.js';

/* Barras horizontais, série única — substitui a pizza de N fatias do
   relatório em planilha (ilegível acima de ~6 categorias). */
export function drawBarrasH(host, rows, opts) {
  opts = opts || {};
  if (!rows.length) return emptyChart(host, opts.empty || 'Sem dados no período.');
  const W = hostW(host);
  const fmtV = opts.fmt || fmtBRL;
  const mR = Math.min(Math.max(...rows.map((r) => measureText(fmtV(r.value), 11, 700))) + 14, W * 0.34);
  const mL = Math.min(Math.max(...rows.map((r) => measureText(r.label, 11.5))) + 12, Math.max(110, W * 0.34));
  const rowH = 26, barH = 15, mT = 6, mB = 6;
  const H = mT + mB + rows.length * rowH;
  const pw = Math.max(30, W - mL - mR);
  const maxV = Math.max(...rows.map((r) => r.value)) || 1;

  let g = '';
  rows.forEach((r, i) => {
    const y = mT + i * rowH;
    const w = (r.value / maxV) * pw;
    g += txt(mL - 8, y + rowH / 2 + 3.5, truncToWidth(r.label, mL - 12, 11.5), { anchor: 'end', size: 11.5, fill: VIZ.ink, tabular: false });
    g += `<path d="${barPath(mL, y + (rowH - barH) / 2, w, barH, 4)}" fill="${opts.color || VIZ.s1}"></path>`;
    g += txt(mL + w + 7, y + rowH / 2 + 3.5, fmtV(r.value), { size: 11, weight: 700, fill: VIZ.ink });
    const tip = `<b>${escapeHtml(r.label)}</b><br>${escapeHtml(opts.tipLabel || 'Valor')}: ${fmtBRL(r.value)}${r.tipExtra || ''}`;
    g += `<g class="band" tabindex="0" data-tip="${tipAttr(tip)}">
      <rect class="bandbg" x="0" y="${y}" width="${W}" height="${rowH}" fill="transparent"></rect></g>`;
  });
  host.innerHTML = `<svg viewBox="0 0 ${W} ${H}" height="${H}" role="img" aria-label="${opts.aria || 'Comparação por categoria'}">${g}</svg>`;
}
