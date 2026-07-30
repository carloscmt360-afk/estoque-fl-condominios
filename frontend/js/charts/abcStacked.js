import { VIZ, hostW, txt, measureText, emptyChart, tipAttr } from './palette.js';
import { fmtBRL, fmtNum, fmtPct } from '../format.js';

/* Barras empilhadas A/B/C: rampa ordinal (uma matiz, clara->escura). */
export function drawABC(host, abc, valorTotal, totalItens) {
  if (!(valorTotal > 0)) return emptyChart(host, 'Sem valor em estoque para classificar.');
  const W = hostW(host);
  const mL = 52, mR = 12, mT = 10, barH = 26, gapY = 46;
  const H = mT + gapY + barH + 8;
  const pw = Math.max(30, W - mL - mR);
  const cls = ['A', 'B', 'C'];
  const linhas = [
    { name: 'Valor', total: valorTotal, get: (c) => abc[c].v, fmt: (v) => fmtBRL(v) },
    { name: 'Itens', total: totalItens, get: (c) => abc[c].n, fmt: (v) => fmtNum(v) + ' itens' },
  ];
  let g = '';
  linhas.forEach((ln, li) => {
    const y = mT + li * gapY;
    g += txt(mL - 8, y + barH / 2 + 4, ln.name, { anchor: 'end', size: 11.5, weight: 700, fill: VIZ.ink, tabular: false });
    let x = mL;
    cls.forEach((c, ci) => {
      const v = ln.get(c);
      const share = ln.total > 0 ? Math.max(0, Math.min(1, v / ln.total)) : 0;
      let w = Math.min(share * pw, mL + pw - x);
      const wDraw = Math.max(0, w - (ci < 2 ? 2 : 0));
      if (w > 0.5) {
        const first = ci === 0, lastSeg = ci === 2;
        g += `<path d="${first || lastSeg
          ? lastSeg
            ? barPath(x, y, wDraw, barH, 4)
            : `M${x + 4},${y} L${x + wDraw},${y} L${x + wDraw},${y + barH} L${x + 4},${y + barH} Q${x},${y + barH} ${x},${y + barH - 4} L${x},${y + 4} Q${x},${y} ${x + 4},${y} Z`
          : `M${x},${y} L${x + wDraw},${y} L${x + wDraw},${y + barH} L${x},${y + barH} Z`
        }" fill="${VIZ.abc[c]}"></path>`;
        const label = fmtPct(share, 0);
        if (wDraw > measureText(label, 11, 700) + 14) {
          g += txt(x + wDraw / 2, y + barH / 2 + 4, label, { anchor: 'middle', size: 11, weight: 700, fill: c === 'C' ? VIZ.ink : '#FFFFFF' });
        }
        g += `<g class="band" tabindex="0" data-tip="<b>Classe ${c} — ${ln.name}</b><br>${ln.fmt(v)}<br>${fmtPct(share, 1)} do total">
          <rect class="bandbg" x="${x}" y="${y}" width="${wDraw}" height="${barH}" fill="transparent"></rect></g>`;
      }
      x += w;
    });
  });
  host.innerHTML = `<svg viewBox="0 0 ${W} ${H}" height="${H}" role="img"
    aria-label="Distribuição do valor e da quantidade de itens entre as classes A, B e C">${g}</svg>`;
}

function barPath(x, y, w, h, r) {
  if (w <= 0.4) return `M${x},${y} L${x},${y + h} Z`;
  r = Math.min(r, h / 2, w);
  return `M${x},${y} L${x + w - r},${y} Q${x + w},${y} ${x + w},${y + r} L${x + w},${y + h - r} Q${x + w},${y + h} ${x + w - r},${y + h} L${x},${y + h} Z`;
}
