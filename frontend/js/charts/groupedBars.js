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
  // mT um pouco maior que o padrão dos outros gráficos: sobra para o rótulo
  // da barra mais alta subir quando ele precisa desviar do vizinho (ver a
  // separação de rótulos colididos mais abaixo).
  const mR = 10, mT = 26, mB = 30;
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
    const hasRef = a.ref !== null && a.ref > 0;
    const hasAnt = a.ant > 0;
    if (hasRef) g += `<path d="${colPath(xRef, yOf(a.ref), half, mT + ph - yOf(a.ref), 4)}" fill="${VIZ.s1}"></path>`;
    if (hasAnt) g += `<path d="${colPath(xAnt, yOf(a.ant), half, mT + ph - yOf(a.ant), 4)}" fill="${VIZ.s2}"></path>`;
    const isRef = i === mesRefIdx;
    g += txt(cx, H - 10, MESES_ABR[i], { anchor: 'middle', size: 10.5, weight: isRef ? 700 : 400, fill: isRef ? VIZ.ink : VIZ.muted });
    // Valor acima de CADA barra (não só a do mês em destaque) — dá pra ler o
    // gráfico inteiro sem precisar passar o mouse (o tooltip continua
    // disponível para o detalhe). O mês de referência continua em destaque
    // (maior e em negrito) para não perder a hierarquia visual.
    //
    // Os dois rótulos do mês são centrados em pontos a ~22px um do outro, mas
    // um "116 mil" a 8px ocupa uns 30px — quando as duas barras têm altura
    // parecida (ex.: 84 mil contra 80 mil), os textos ficam na mesma linha e
    // se sobrepõem, virando um borrão do tipo "84 mi80 mil". Nesse caso o
    // rótulo da barra MAIS ALTA sobe até abrir espaço: é o que tem céu livre
    // acima, enquanto descer o outro o jogaria para dentro da própria barra.
    let yRefLbl = yOf(a.ref) - (isRef ? 6 : 4);
    let yAntLbl = yOf(a.ant) - 4;
    const alturaLinha = isRef ? 13 : 11;
    if (hasRef && hasAnt && Math.abs(yRefLbl - yAntLbl) < alturaLinha) {
      if (yRefLbl <= yAntLbl) yRefLbl = yAntLbl - alturaLinha;
      else yAntLbl = yRefLbl - alturaLinha;
    }
    if (hasRef) g += txt(xRef + half / 2, yRefLbl, fmtAxisMoney(a.ref),
      { anchor: 'middle', size: isRef ? 10.5 : 8, weight: isRef ? 700 : 600,
        fill: isRef ? VIZ.ink : VIZ.s1, halo: true });
    if (hasAnt) g += txt(xAnt + half / 2, yAntLbl, fmtAxisMoney(a.ant),
      { anchor: 'middle', size: 8, weight: 600, fill: VIZ.s2Ink, halo: true });
    const dc = deltaInfo(a.ref === null ? 0 : a.ref, a.ant);
    // `semDado`/`antSemDado` só existem no retrospecto (onde um mês pode não
    // estar em nenhuma fonte). No relatório mensal são undefined, e aí um zero
    // é zero de verdade — o razão cobre o mês inteiro.
    const semRef = a.futuro ? 'mês não decorrido' : a.semDado ? 'sem lançamento' : null;
    const tip = `<b>${MESES[i]}</b><br>` +
      `<span class='tk' style='background:${VIZ.s1}'></span>${y}: ${semRef || fmtBRL(a.ref || 0)}<br>` +
      `<span class='tk' style='background:${VIZ.s2}'></span>${y - 1}: ${a.antSemDado ? 'sem lançamento' : fmtBRL(a.ant || 0)}<br>` +
      `Variação: ${semRef || a.antSemDado ? '—' : dc.kind === 'novo' ? 'novo' : dc.text}`;
    g += `<g class="band" tabindex="0" data-tip="${tipAttr(tip)}">
      <rect class="bandbg" x="${mL + band * i}" y="${mT}" width="${band}" height="${ph}" fill="transparent"></rect></g>`;
  });
  host.innerHTML = `<svg viewBox="0 0 ${W} ${H}" height="${H}" role="img"
    aria-label="Consumo mensal comparado entre ${y} e ${y - 1}">${g}</svg>`;
}
