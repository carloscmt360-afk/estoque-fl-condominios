// Formatação — portada verbatim do app web anterior (já testada extensivamente
// contra os dados reais do usuário). Nenhuma dependência de DOM/estado global.

export function fmtBRL(v) {
  return (v || 0).toLocaleString('pt-BR', { style: 'currency', currency: 'BRL' });
}
export function fmtNum(v) {
  return (v || 0).toLocaleString('pt-BR', { maximumFractionDigits: 3 });
}
export function fmtPct(v, dec) {
  return (v * 100).toLocaleString('pt-BR', { minimumFractionDigits: dec || 0, maximumFractionDigits: dec || 0 }) + '%';
}
export function fmtAxisMoney(v) {
  const a = Math.abs(v);
  if (a >= 1000000) return (v / 1000000).toLocaleString('pt-BR', { maximumFractionDigits: 1 }) + ' mi';
  if (a >= 1000) return (v / 1000).toLocaleString('pt-BR', { maximumFractionDigits: a >= 10000 ? 0 : 1 }) + ' mil';
  return v.toLocaleString('pt-BR', { maximumFractionDigits: 0 });
}
export function fmtDateBR(msOrIso) {
  const d = typeof msOrIso === 'number' ? new Date(msOrIso) : new Date(msOrIso);
  return d.toLocaleDateString('pt-BR');
}
export function fmtDateTimeBR(msOrIso) {
  const d = typeof msOrIso === 'number' ? new Date(msOrIso) : new Date(msOrIso);
  return d.toLocaleString('pt-BR', { day: '2-digit', month: '2-digit', year: 'numeric', hour: '2-digit', minute: '2-digit' });
}
export function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}
export function trunc(s, n) {
  s = String(s);
  return s.length > n ? s.slice(0, n - 1) + '…' : s;
}

/* Regras de honestidade das comparações: base zero é indefinida ("novo"),
   ausência de base é "—" (nunca -100% ou infinito por falta de dado). */
export function deltaInfo(cur, prev) {
  if (prev === null || prev === undefined) return { kind: 'nd', text: '—' };
  if (prev === 0 && cur === 0) return { kind: 'flat', text: 'sem movimento' };
  if (prev === 0) return { kind: 'novo', text: 'novo' };
  const p = (cur - prev) / Math.abs(prev);
  if (Math.abs(p) <= 0.0005) return { kind: 'flat', pct: 0, text: 'estável' };
  return { kind: p > 0 ? 'up' : 'down', pct: p, text: (p > 0 ? '+' : '') + fmtPct(p, 1) };
}

/* upIsGood: true (subir é bom) · false (subir é ruim) · null (neutro) */
export function deltaHTML(cur, prev, upIsGood, label) {
  const d = deltaInfo(cur, prev);
  if (d.kind === 'nd') return `<span class="delta muted">—</span> <span class="muted">sem base comparável</span>`;
  let cls = 'muted', ico = '■';
  if (d.kind === 'up') { ico = '▲'; cls = upIsGood === null ? 'muted' : upIsGood ? 'flag-good' : 'flag-critical'; }
  else if (d.kind === 'down') { ico = '▼'; cls = upIsGood === null ? 'muted' : upIsGood ? 'flag-critical' : 'flag-good'; }
  else if (d.kind === 'novo') ico = '';
  return `<span class="delta ${cls}">${ico ? `<span class="ico">${ico}</span>` : ''}${d.text}</span>` +
    (label ? ` <span class="muted">${escapeHtml(label)}</span>` : '');
}
export function deltaCell(cur, prev) {
  const d = deltaInfo(cur, prev);
  if (d.kind === 'nd' || d.kind === 'flat') return '<span class="muted">—</span>';
  if (d.kind === 'novo') return '<span class="flag flag-critical">novo</span>';
  const cls = d.kind === 'up' ? 'flag-critical' : 'flag-good';
  return `<span class="flag ${cls}">${d.kind === 'up' ? '▲' : '▼'} ${d.text}</span>`;
}

/* Medidor de uso de um teto (limite mensal do departamento, teto de gastos do
   retrospecto). A barra satura em 100% — uma barra 3× mais longa que a caixa
   não cabe —, mas o número ao lado sempre mostra o valor real, e a barra nunca
   é a única forma de ler o dado. */
export function meterHTML(pct) {
  if (pct === null || pct === undefined) return '<span class="muted">—</span>';
  const cls = pct >= 1 ? 'is-critical' : pct >= 0.9 ? 'is-warn' : '';
  const w = Math.max(0, Math.min(1, pct)) * 100;
  return `<div class="meter-row"><span class="meter ${cls}"><i style="width:${w.toFixed(1)}%"></i></span>
    <span class="pctv">${fmtPct(pct, 0)}</span></div>`;
}

export function uid(prefix) {
  return (prefix || 'id_') + Date.now().toString(36) + Math.random().toString(36).slice(2, 8);
}
export function nowIso() {
  return new Date().toISOString();
}
export function nowLocalInputValue() {
  const d = new Date();
  d.setMinutes(d.getMinutes() - d.getTimezoneOffset());
  return d.toISOString().slice(0, 16);
}
