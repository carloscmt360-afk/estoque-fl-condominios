import { VIZ, hostW, txt, emptyChart, tipAttr } from './palette.js';
import { fmtBRL } from '../format.js';

const MESES = ['Janeiro','Fevereiro','Março','Abril','Maio','Junho','Julho','Agosto','Setembro','Outubro','Novembro','Dezembro'];

/* Sparkline: 12 pontos na cor de fundo, mês de referência em destaque. */
export function drawSpark(host, serie) {
  const W = hostW(host, 320), H = 68, pad = 6;
  if (!serie.some((s) => s.consumo > 0)) return emptyChart(host, 'Sem consumo nos últimos 12 meses.');
  const max = Math.max(...serie.map((s) => s.consumo)) || 1;
  const stepX = (W - pad * 2) / Math.max(1, serie.length - 1);
  const pts = serie.map((s, i) => [pad + i * stepX, H - pad - (s.consumo / max) * (H - pad * 2)]);
  const d = pts.map((p, i) => (i ? 'L' : 'M') + p[0].toFixed(1) + ',' + p[1].toFixed(1)).join(' ');
  const last = pts[pts.length - 1];
  const bands = serie.map((s, i) => {
    const x0 = Math.max(0, pad + i * stepX - stepX / 2);
    const x1 = Math.min(W, pad + i * stepX + stepX / 2);
    return `<g class="pt" tabindex="0" data-tip="${tipAttr(`<b>${MESES[s.m]} ${s.y}</b><br>Consumo: ${fmtBRL(s.consumo)}`)}">
      <rect class="hit" x="${x0}" y="0" width="${x1 - x0}" height="${H}"></rect>
      <circle class="dot" cx="${pts[i][0].toFixed(1)}" cy="${pts[i][1].toFixed(1)}" r="4"
              fill="${VIZ.s1}" stroke="${VIZ.surface}" stroke-width="2"></circle>
    </g>`;
  }).join('');
  host.innerHTML = `<svg viewBox="0 0 ${W} ${H}" height="${H}" role="img"
    aria-label="Consumo mensal dos últimos 12 meses">
    <path d="${d} L${last[0].toFixed(1)},${H - pad} L${pad},${H - pad} Z" fill="${VIZ.s1}" opacity="0.10"></path>
    <path d="${d}" fill="none" stroke="${VIZ.deemph}" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"></path>
    <circle cx="${last[0].toFixed(1)}" cy="${last[1].toFixed(1)}" r="4.5" fill="${VIZ.s1}"
            stroke="${VIZ.surface}" stroke-width="2"></circle>
    ${bands}
  </svg>`;
}
