import { VIZ, hostW, txt, niceScale, measureText, emptyChart, tipAttr } from './palette.js';
import { fmtAxisMoney, fmtBRL } from '../format.js';

const MESES = ['Janeiro','Fevereiro','Março','Abril','Maio','Junho','Julho','Agosto','Setembro','Outubro','Novembro','Dezembro'];
const MESES_ABR = ['Jan','Fev','Mar','Abr','Mai','Jun','Jul','Ago','Set','Out','Nov','Dez'];

/* Linha: valor do estoque nos últimos 12 meses (série única). */
export function drawValorEstoque(host, serie) {
  const W = hostW(host), H = 250;
  const maxV = Math.max(...serie.map((s) => s.valorEstoque));
  if (!(maxV > 0)) return emptyChart(host, 'Sem posição de estoque nos últimos 12 meses.');
  const sc = niceScale(maxV, 4);
  const fimTxt = fmtAxisMoney(serie[serie.length - 1].valorEstoque);
  const mL = Math.max(...sc.ticks.map((t) => measureText(fmtAxisMoney(t), 10.5))) + 14;
  const mR = measureText(fimTxt, 11.5, 700) + 16;
  const mT = 18, mB = 30;
  const pw = W - mL - mR, ph = H - mT - mB;
  const yOf = (v) => mT + ph - (v / sc.max) * ph;
  const stepX = pw / Math.max(1, serie.length - 1);
  const pts = serie.map((s, i) => [mL + i * stepX, yOf(s.valorEstoque)]);

  let g = '';
  sc.ticks.forEach((t) => {
    g += `<line x1="${mL}" y1="${yOf(t)}" x2="${W - mR}" y2="${yOf(t)}" stroke="${t === 0 ? VIZ.axis : VIZ.grid}" stroke-width="1"></line>`;
    g += txt(mL - 8, yOf(t) + 3.5, fmtAxisMoney(t), { anchor: 'end', size: 10.5 });
  });
  const d = pts.map((p, i) => (i ? 'L' : 'M') + p[0].toFixed(1) + ',' + p[1].toFixed(1)).join(' ');
  g += `<path d="${d} L${pts[pts.length - 1][0].toFixed(1)},${mT + ph} L${mL},${mT + ph} Z" fill="${VIZ.s1}" opacity="0.10"></path>`;
  g += `<path d="${d}" fill="none" stroke="${VIZ.s1}" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"></path>`;

  serie.forEach((s, i) => {
    const [px, py] = pts[i];
    const label = MESES_ABR[s.m] + (s.m === 0 ? '/' + String(s.y).slice(2) : '');
    if (i % 2 === 0 || i === serie.length - 1) g += txt(px, H - 10, label, { anchor: 'middle', size: 10.5 });
    const tip = `<b>${MESES[s.m]} ${s.y}</b><br>Estoque: ${fmtBRL(s.valorEstoque)}<br>Consumo no mês: ${fmtBRL(s.consumo)}`;
    g += `<g class="pt" tabindex="0" data-tip="${tipAttr(tip)}">
      <line class="guide" x1="${px.toFixed(1)}" y1="${mT}" x2="${px.toFixed(1)}" y2="${mT + ph}" stroke="${VIZ.axis}" stroke-width="1"></line>
      <circle class="dot" cx="${px.toFixed(1)}" cy="${py.toFixed(1)}" r="4.5" fill="${VIZ.s1}" stroke="${VIZ.surface}" stroke-width="2"></circle>
      <rect class="hit" x="${(Math.max(0, px - stepX / 2)).toFixed(1)}" y="${mT}" width="${(Math.min(W, px + stepX / 2) - Math.max(0, px - stepX / 2)).toFixed(1)}" height="${ph}"></rect></g>`;
  });
  const last = pts[pts.length - 1];
  g += `<circle cx="${last[0].toFixed(1)}" cy="${last[1].toFixed(1)}" r="4.5" fill="${VIZ.s1}" stroke="${VIZ.surface}" stroke-width="2"></circle>`;
  g += txt(last[0] + 9, last[1] + 4, fimTxt, { size: 11.5, weight: 700, fill: VIZ.ink });
  host.innerHTML = `<svg viewBox="0 0 ${W} ${H}" height="${H}" role="img"
    aria-label="Evolução do valor do estoque nos últimos 12 meses">${g}</svg>`;
}
