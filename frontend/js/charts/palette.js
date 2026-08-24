// Paleta e primitivas de desenho SVG.
// Espelha a paleta institucional de css/tokens.css (--chart-*) para uso fora
// do CSS: SVG inline não lê custom properties nos atributos fill/stroke.
import { escapeHtml, fmtBRL } from '../format.js';

export const VIZ = {
  // s1/s2: só usados pelo comparativo mensal Y vs Y-1 (groupedBars.js +
  // legendas em dashboard.js/retrospect.js). Azul-marinho (ano de
  // referência) contra laranja (ano anterior): além de pedido, é o par de
  // cores mais seguro para quem não distingue vermelho de verde — a
  // diferença sobrevive em qualquer tipo de daltonismo.
  //
  // s2Ink é o MESMO laranja escurecido, só para os rótulos de valor em cima
  // das barras: a 8px, #D97757 sobre branco fica com contraste baixo demais
  // para texto; a barra, sendo uma área grande e cheia, não tem esse
  // problema e fica no tom pedido.
  s1: '#1B3A5C', s2: '#D97757', s2Ink: '#A6472A',
  abc: { A: '#1B3A5C', B: '#2E6BA6', C: '#6C8CA8' },
  grid: '#E1E4E8', axis: '#C6CCD2', muted: '#6B7280', ink: '#2B2F33',
  surface: '#FFFFFF', deemph: '#AEC2D2',
  // Azul sequencial (magnitude/tempo) — usado nos retrospectos mês a mês,
  // onde o mês ATUAL precisa estar sempre em evidência: os outros meses
  // usam o passo mais claro, o mês corrente usa o mais escuro.
  sequential: { fraco: '#9ec5f4', medio: '#3987e5', forte: '#0d366b' },
};

function mixHex(a, b, t) {
  const ha = a.replace('#', ''), hb = b.replace('#', '');
  const mix = (x, y) => Math.round(x + (y - x) * t);
  const chan = (h, i) => parseInt(h.slice(i, i + 2), 16);
  return `#${[0, 2, 4].map((i) => mix(chan(ha, i), chan(hb, i)).toString(16).padStart(2, '0')).join('')}`;
}

// Cor de uma barra de RANKING (pedido explícito: todo gráfico é um degradê
// de azul do mais escuro ao mais claro, nunca uma paleta multi-matiz por
// identidade) — a barra de maior valor (i=0, ranking já vem ordenado
// decrescente) usa o azul mais escuro, a de menor valor o mais claro.
// Interpola entre os mesmos tons de VIZ.sequential, então ranking e timeline
// na mesma tela ficam na mesma família de azul.
export function rankBlue(i, n) {
  const t = n > 1 ? i / (n - 1) : 0;
  return mixHex(VIZ.sequential.forte, VIZ.sequential.fraco, t);
}

// Clareia um hex misturando com branco — usado pra montar o degradê de cada
// barra (claro → a cor de base) sem precisar de uma segunda cor por série.
export function lighten(hex, pct) {
  const h = hex.replace('#', '');
  const r = parseInt(h.slice(0, 2), 16), g = parseInt(h.slice(2, 4), 16), b = parseInt(h.slice(4, 6), 16);
  const mix = (c) => Math.round(c + (255 - c) * pct);
  return `#${[mix(r), mix(g), mix(b)].map((v) => v.toString(16).padStart(2, '0')).join('')}`;
}

// Gera os <linearGradient> (um por cor distinta) e devolve como referenciar
// cada um via fill="url(#id)" — todo gráfico de barra usa gradiente, nunca
// cor chapada (pedido explícito: "todas as cores devem ter efeito
// gradiente"). `horizontal` decide a direção do degradê (barra deitada vs
// em pé).
let gradSeq = 0;
export function gradientDefs(colors, horizontal) {
  const map = new Map();
  let defs = '';
  colors.forEach((hex) => {
    if (map.has(hex)) return;
    const id = `vizgrad${gradSeq++}`;
    map.set(hex, id);
    const x2 = horizontal ? '1' : '0', y2 = horizontal ? '0' : '1';
    defs += `<linearGradient id="${id}" x1="0" y1="0" x2="${x2}" y2="${y2}">` +
      `<stop offset="0%" stop-color="${lighten(hex, 0.55)}"/>` +
      `<stop offset="100%" stop-color="${hex}"/></linearGradient>`;
  });
  return { defsHTML: defs ? `<defs>${defs}</defs>` : '', urlFor: (hex) => `url(#${map.get(hex)})` };
}

export function hostW(el, fallback) {
  const w = el.getBoundingClientRect().width;
  return w > 40 ? w : fallback || 620;
}

/* Medição real do texto via canvas: estimar por nº de caracteres corta letras
   largas ("MANUTENÇÃO" virava "ANUTENÇÃO"). A mesma medida dimensiona a
   margem e trunca o rótulo, então nada vaza do quadro. */
const VIZ_FONT = '-apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif';
let _medCtx;
export function measureText(s, size, weight) {
  s = String(s);
  if (_medCtx === undefined) {
    try { const c = document.createElement('canvas'); _medCtx = c.getContext ? c.getContext('2d') : null; }
    catch (e) { _medCtx = null; }
  }
  if (_medCtx) {
    _medCtx.font = `${weight || 400} ${size}px ${VIZ_FONT}`;
    const w = _medCtx.measureText(s).width;
    if (w > 0) return w;
  }
  return s.length * size * 0.68;
}
export function truncToWidth(s, maxW, size, weight) {
  s = String(s);
  if (measureText(s, size, weight) <= maxW) return s;
  let lo = 0, hi = s.length;
  while (lo < hi) {
    const mid = Math.ceil((lo + hi) / 2);
    if (measureText(s.slice(0, mid) + '…', size, weight) <= maxW) lo = mid; else hi = mid - 1;
  }
  return lo > 0 ? s.slice(0, lo) + '…' : '';
}

export function niceScale(max, count) {
  count = count || 4;
  if (!(max > 0)) return { max: 1, ticks: [0, 1] };
  const raw = max / count;
  const mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const n = raw / mag;
  const step = (n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10) * mag;
  const top = Math.ceil(max / step) * step;
  const ticks = [];
  for (let v = 0; v <= top + step / 2; v += step) ticks.push(Math.round(v * 1e6) / 1e6);
  return { max: top || 1, ticks };
}

/* Coluna: cantos superiores arredondados, base reta sobre a linha de base */
export function colPath(x, y, w, h, r) {
  if (h <= 0.4) return `M${x},${y + h} L${x + w},${y + h} L${x + w},${y + h} L${x},${y + h} Z`;
  r = Math.min(r, w / 2, h);
  return `M${x},${y + h} L${x},${y + r} Q${x},${y} ${x + r},${y} L${x + w - r},${y} Q${x + w},${y} ${x + w},${y + r} L${x + w},${y + h} Z`;
}
/* Barra horizontal: ponta direita arredondada, base reta na origem */
export function barPath(x, y, w, h, r) {
  if (w <= 0.4) return `M${x},${y} L${x},${y + h} Z`;
  r = Math.min(r, h / 2, w);
  return `M${x},${y} L${x + w - r},${y} Q${x + w},${y} ${x + w},${y + r} L${x + w},${y + h - r} Q${x + w},${y + h} ${x + w - r},${y + h} L${x},${y + h} Z`;
}

export function txt(x, y, s, o) {
  o = o || {};
  // `halo: true` desenha um contorno branco ATRÁS das letras
  // (paint-order=stroke pinta o traço primeiro, o preenchimento por cima).
  // Serve para rótulo que pode cair sobre uma barra: um valor de 8px é mais
  // largo que a barra estreita que ele rotula, então invade a barra vizinha —
  // e texto escuro sobre barra escura some. Com o contorno, o rótulo continua
  // legível sobre qualquer fundo, sem precisar afastá-lo do que ele nomeia.
  const halo = o.halo
    ? ' stroke="#FFFFFF" stroke-width="3" stroke-linejoin="round" paint-order="stroke"'
    : '';
  return `<text x="${x}" y="${y}" fill="${o.fill || VIZ.muted}" font-size="${o.size || 11}" ` +
    `font-weight="${o.weight || 400}" text-anchor="${o.anchor || 'start'}"${halo} ` +
    `${o.tabular === false ? '' : 'style="font-variant-numeric:tabular-nums"'}>${escapeHtml(s)}</text>`;
}
export function emptyChart(host, msg) {
  host.innerHTML = `<div class="chart-empty">${escapeHtml(msg)}</div>`;
}
export function legendHTML(items) {
  return items.map((i) => `<span class="k"><i style="background:${i.color}"></i>${escapeHtml(i.label)}</span>`).join('');
}
export function tipAttr(html) {
  return html.replace(/&/g, '&amp;').replace(/"/g, '&quot;');
}

/* Tooltip compartilhado entre todos os gráficos: realça ao passar o mouse ou
   focar via teclado, nunca é o único caminho pro valor (cada card tem "Ver
   tabela" ao lado). Instalado uma única vez por página. */
let tooltipInstalled = false;
export function installTooltip() {
  if (tooltipInstalled) return;
  tooltipInstalled = true;
  const tip = document.getElementById('vizTip');
  function place(el, x, y) {
    tip.innerHTML = el.getAttribute('data-tip');
    tip.style.display = 'block';
    const r = tip.getBoundingClientRect();
    let left = x + 16, top = y + 16;
    if (left + r.width > window.innerWidth - 8) left = x - r.width - 16;
    if (top + r.height > window.innerHeight - 8) top = y - r.height - 16;
    tip.style.left = Math.max(8, left) + 'px';
    tip.style.top = Math.max(8, top) + 'px';
  }
  const hide = () => { tip.style.display = 'none'; };
  document.addEventListener('mousemove', (e) => {
    const el = e.target && e.target.closest ? e.target.closest('.chart-host [data-tip]') : null;
    if (el) place(el, e.clientX, e.clientY); else hide();
  });
  document.addEventListener('focusin', (e) => {
    const el = e.target && e.target.closest ? e.target.closest('.chart-host [data-tip]') : null;
    if (el) { const b = el.getBoundingClientRect(); place(el, b.left + b.width / 2, b.top); } else hide();
  });
  document.addEventListener('scroll', hide, true);
  window.addEventListener('blur', hide);
}
