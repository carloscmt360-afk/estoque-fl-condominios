import { emptyChart } from './palette.js';
import { fmtBRL, escapeHtml, deltaInfo } from '../format.js';

/* "Acumulado por departamento": tabela com um pontinho colorido na variação
   (verde = gastou menos que no período anterior, vermelho = gastou mais) —
   direto de ler à primeira vista, sem precisar posicionar o mouse como no
   dumbbell chart que este componente substituiu. */
export function drawDeptoAcumulado(host, rows, opts) {
  opts = opts || {};
  if (!rows.length) return emptyChart(host, 'Sem consumo acumulado nos dois anos comparados.');

  const linhas = rows.map((r) => {
    const d = deltaInfo(r.cur, r.prev);
    let cor = 'var(--ink-muted)', texto = d.text;
    if (d.kind === 'up') cor = 'var(--danger)';
    else if (d.kind === 'down') cor = 'var(--success)';
    return `<tr>
      <td>${escapeHtml(r.label)}</td>
      <td class="num">${fmtBRL(r.cur)}</td>
      <td class="num">${fmtBRL(r.prev)}</td>
      <td class="num"><span class="acc-delta"><i class="acc-dot" style="background:${cor}"></i>${escapeHtml(texto)}</span></td>
    </tr>`;
  }).join('');

  host.innerHTML = `<table class="acc-depto-table">
    <thead><tr><th>Departamento</th><th class="num">${escapeHtml(opts.curLabel || 'Atual')}</th>
      <th class="num">${escapeHtml(opts.prevLabel || 'Anterior')}</th><th class="num">Variação</th></tr></thead>
    <tbody>${linhas}</tbody></table>`;
}
