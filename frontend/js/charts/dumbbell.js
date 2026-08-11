import { VIZ, hostW, txt, niceScale, measureText, truncToWidth, emptyChart, tipAttr } from './palette.js';
import { fmtAxisMoney, fmtBRL, escapeHtml, deltaInfo } from '../format.js';

/* Dumbbell: antes -> depois por item, uma matiz em dois passos (claro =
   período anterior, escuro = período atual). */
export function drawDumbbell(host, rows, opts) {
  if (!rows.length) return emptyChart(host, 'Sem consumo acumulado nos dois anos comparados.');
  const W = hostW(host);
  const mL = Math.min(Math.max(...rows.map((r) => measureText(r.label, 11.5))) + 12, Math.max(110, W * 0.32));
  const mR = 74, rowH = 26, mT = 8, mB = 24;
  const H = mT + mB + rows.length * rowH;
  const pw = Math.max(30, W - mL - mR);
  const maxV = Math.max(...rows.map((r) => Math.max(r.cur, r.prev))) || 1;
  const sc = niceScale(maxV, 3);
  const xOf = (v) => mL + (v / sc.max) * pw;

  let g = '';
  sc.ticks.forEach((t) => {
    g += `<line x1="${xOf(t)}" y1="${mT}" x2="${xOf(t)}" y2="${mT + rows.length * rowH}" stroke="${t === 0 ? VIZ.axis : VIZ.grid}" stroke-width="1"></line>`;
    g += txt(xOf(t), H - 8, fmtAxisMoney(t), { anchor: 'middle', size: 10.5 });
  });
  rows.forEach((r, i) => {
    const y = mT + i * rowH + rowH / 2;
    g += txt(mL - 8, y + 3.5, truncToWidth(r.label, mL - 12, 11.5), { anchor: 'end', size: 11.5, fill: VIZ.ink, tabular: false });
    g += `<line x1="${xOf(r.prev)}" y1="${y}" x2="${xOf(r.cur)}" y2="${y}" stroke="${VIZ.axis}" stroke-width="2" stroke-linecap="round"></line>`;
    g += `<circle cx="${xOf(r.prev)}" cy="${y}" r="5" fill="${VIZ.prior}" stroke="${VIZ.surface}" stroke-width="2"></circle>`;
    g += `<circle cx="${xOf(r.cur)}" cy="${y}" r="5" fill="${VIZ.cur}" stroke="${VIZ.surface}" stroke-width="2"></circle>`;
    const d = deltaInfo(r.cur, r.prev);
    const lbl = d.kind === 'novo' ? 'novo' : d.kind === 'nd' || d.kind === 'flat' ? '—' : d.text;
    g += txt(W - 6, y + 3.5, lbl, { anchor: 'end', size: 11, weight: 700, fill: d.kind === 'up' ? '#8E2A20' : d.kind === 'down' ? '#21643E' : VIZ.muted });
    const tip = `<b>${escapeHtml(r.label)}</b><br>` +
      `<span class='tk' style='background:${VIZ.cur}'></span>${escapeHtml(opts.curLabel)}: ${fmtBRL(r.cur)}<br>` +
      `<span class='tk' style='background:${VIZ.prior}'></span>${escapeHtml(opts.prevLabel)}: ${fmtBRL(r.prev)}<br>` +
      `Diferença: ${fmtBRL(r.cur - r.prev)} (${lbl})`;
    g += `<g class="band" tabindex="0" data-tip="${tipAttr(tip)}">
      <rect class="bandbg" x="0" y="${mT + i * rowH}" width="${W}" height="${rowH}" fill="transparent"></rect></g>`;
  });
  host.innerHTML = `<svg viewBox="0 0 ${W} ${H}" height="${H}" role="img"
    aria-label="Acumulado por departamento, ano corrente comparado ao anterior">${g}</svg>`;
}
