import { VIZ, hostW, txt, niceScale, colPath, measureText, emptyChart, tipAttr } from './palette.js';
import { fmtAxisMoney, fmtBRL, deltaInfo } from '../format.js';

const MESES = ['Janeiro','Fevereiro','Março','Abril','Maio','Junho','Julho','Agosto','Setembro','Outubro','Novembro','Dezembro'];
const MESES_ABR = ['Jan','Fev','Mar','Abr','Mai','Jun','Jul','Ago','Set','Out','Nov','Dez'];

/* Colunas agrupadas: ano de referência (à esquerda, ordem = legenda) x ano
   anterior. Meses futuros ficam sem barra — nunca uma queda de 100%
   inventada pela ausência de dado. */
export function drawConsumoAnual(host, anual, y, mesRefIdx) {
  const W = hostW(host), H = 250;
  const maxV = Math.max(0, ...anual.map((a) => Math.max(a.ref || 0, a.ant || 0)));
  if (maxV <= 0) return emptyChart(host, `Sem consumo registrado em ${y} ou ${y - 1}.`);
  const sc = niceScale(maxV, 4);
  const mL = Math.max(...sc.ticks.map((t) => measureText(fmtAxisMoney(t), 10.5))) + 14;
  const mR = 10, mT = 18, mB = 30;
  const pw = W - mL - mR, ph = H - mT - mB;
  const yOf = (v) => mT + ph - (v / sc.max) * ph;
  const band = pw / 12;
  const barW = Math.max(3, Math.min(band * 0.66, 44) / 1);
  const half = Math.min(band * 0.33, 22);

  let g = '';
  sc.ticks.forEach((t) => {
    g += `<line x1="${mL}" y1="${yOf(t)}" x2="${W - mR}" y2="${yOf(t)}" stroke="${t === 0 ? VIZ.axis : VIZ.grid}" stroke-width="1"></line>`;
    g += txt(mL - 8, yOf(t) + 3.5, fmtAxisMoney(t), { anchor: 'end', size: 10.5 });
  });

  anual.forEach((a, i) => {
    const cx = mL + band * i + band / 2;
    const xRef = cx - half - 1, xAnt = cx + 1;
    if (a.ref !== null && a.ref > 0) g += `<path d="${colPath(xRef, yOf(a.ref), half, mT + ph - yOf(a.ref), 4)}" fill="${VIZ.s1}"></path>`;
    if (a.ant > 0) g += `<path d="${colPath(xAnt, yOf(a.ant), half, mT + ph - yOf(a.ant), 4)}" fill="${VIZ.s2}"></path>`;
    const isRef = i === mesRefIdx;
    g += txt(cx, H - 10, MESES_ABR[i], { anchor: 'middle', size: 10.5, weight: isRef ? 700 : 400, fill: isRef ? VIZ.ink : VIZ.muted });
    if (isRef && a.ref) g += txt(xRef + half / 2, yOf(a.ref) - 6, fmtAxisMoney(a.ref), { anchor: 'middle', size: 10.5, weight: 700, fill: VIZ.ink });
    const dc = deltaInfo(a.ref === null ? 0 : a.ref, a.ant);
    const tip = `<b>${MESES[i]}</b><br>` +
      `<span class='tk' style='background:${VIZ.s1}'></span>${y}: ${a.futuro ? 'mês não decorrido' : fmtBRL(a.ref || 0)}<br>` +
      `<span class='tk' style='background:${VIZ.s2}'></span>${y - 1}: ${fmtBRL(a.ant || 0)}<br>` +
      `Variação: ${a.futuro ? '—' : dc.kind === 'novo' ? 'novo' : dc.text}`;
    g += `<g class="band" tabindex="0" data-tip="${tipAttr(tip)}">
      <rect class="bandbg" x="${mL + band * i}" y="${mT}" width="${band}" height="${ph}" fill="transparent"></rect></g>`;
  });
  host.innerHTML = `<svg viewBox="0 0 ${W} ${H}" height="${H}" role="img"
    aria-label="Consumo mensal comparado entre ${y} e ${y - 1}">${g}</svg>`;
}
