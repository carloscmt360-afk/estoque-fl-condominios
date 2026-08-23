import { api, errorText } from '../api.js';
import { escapeHtml, fmtBRL, paraBusca } from '../format.js';
import { toast } from '../components/toast.js';
import { printDocument, buildDeltaSindicosDoc } from '../print.js';

// Gestão SOS > Delta Síndicos — a planilha de comissões dos SÍNDICOS, irmã
// de Serviços. NÃO é mais lançada à mão: é puxada automaticamente de
// Serviços — todo serviço PAGO num condomínio marcado como "atendido pela
// Delta" vira uma linha aqui sozinho (ver listDeltaSindicos em
// commissions_engine.cpp). O que se cadastra AQUI é só QUAIS condomínios são
// esses — um checklist, igual ao padrão de Carteiras.

let lancamentos = [];
let condominios = [];
let filtroMes = '';
let busca = '';
let buscaCondominios = '';
let wired = false;

// Checklist trava por padrão (só leitura) pra evitar clique sem querer
// trocando o condomínio marcado. "Destravar" libera edição; os cliques
// enquanto destravada ficam só em `pendentes` (nada é salvo ainda) até
// "Salvar e travar" gravar as mudanças de uma vez e travar de novo.
let travado = true;
let pendentes = new Map();
let salvando = false;

export async function initDeltaSindicos() {
  if (!wired) {
    wired = true;
    document.getElementById('btnImprimirDeltaSindicos').addEventListener('click', imprimir);
    document.getElementById('dsFiltroMes').addEventListener('input', (e) => {
      filtroMes = e.target.value;
      render();
    });
    document.getElementById('dsBusca').addEventListener('input', (e) => {
      busca = e.target.value.toLowerCase();
      render();
    });
    document.getElementById('dsBuscaCondominios').addEventListener('input', (e) => {
      buscaCondominios = e.target.value;
      renderChecklist();
    });
    document.getElementById('btnDeltaDestravar').addEventListener('click', destravar);
    document.getElementById('btnDeltaSalvar').addEventListener('click', salvarETravar);
  }
  await reload();
}

export async function reload() {
  try {
    const [lista, cond] = await Promise.all([api.listDeltaSindicos(), api.listCondominios()]);
    lancamentos = lista; condominios = cond;
  } catch (e) {
    toast('Erro ao carregar Delta Síndicos: ' + errorText(e), 'error');
    lancamentos = []; condominios = [];
  }
  render();
  atualizaTravaUI();
  renderChecklist();
}

function mesAnoLabel(yyyymm) {
  if (!yyyymm || yyyymm.length !== 7) return '—';
  const [ano, mes] = yyyymm.split('-');
  return `${mes}/${ano}`;
}

function listaFiltrada() {
  let lista = filtroMes ? lancamentos.filter((d) => d.dataReferencia === filtroMes) : lancamentos;
  if (busca) {
    lista = lista.filter((d) =>
      [d.condominioNome, d.sindico, d.gerenteNome].filter(Boolean).join(' ').toLowerCase().includes(busca));
  }
  return lista;
}

function render() {
  const tbody = document.getElementById('deltaSindicosTbody');
  const tfoot = document.getElementById('deltaSindicosTfoot');
  const lista = listaFiltrada();

  document.getElementById('deltaSindicosTotal').textContent = lancamentos.length;
  document.getElementById('deltaSindicosFiltrados').textContent =
    (filtroMes || busca) ? ` · ${lista.length} no filtro` : '';

  if (!lista.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="8">${
      lancamentos.length ? 'Nenhum lançamento neste mês de referência.'
        : 'Nenhum serviço pago ainda em condomínio marcado como atendido pela Delta.'}</td></tr>`;
    tfoot.innerHTML = '';
    return;
  }

  tbody.innerHTML = lista.map((d) => `<tr>
      <td class="num">${d.numero}</td>
      <td>${escapeHtml(d.condominioNome)}</td>
      <td>${d.sindico ? escapeHtml(d.sindico) : '<span class="muted">—</span>'}</td>
      <td>${d.gerenteNome ? escapeHtml(d.gerenteNome) : '<span class="muted">—</span>'}</td>
      <td class="num">${fmtBRL(d.venda)}</td>
      <td class="num">${d.porcentagem.toLocaleString('pt-BR')}%</td>
      <td class="num"><b>${fmtBRL(d.comissao)}</b></td>
      <td>${mesAnoLabel(d.dataReferencia)}</td></tr>`).join('');

  // Totais do que está À VISTA (a lista filtrada), não da planilha inteira.
  const totalVenda = lista.reduce((s, d) => s + d.venda, 0);
  const totalComissao = lista.reduce((s, d) => s + d.comissao, 0);
  tfoot.innerHTML = `<tr>
    <td colspan="4">TOTAL — ${lista.length} lançamento(s)</td>
    <td class="num"><b>${fmtBRL(totalVenda)}</b></td>
    <td></td>
    <td class="num"><b>${fmtBRL(totalComissao)}</b></td>
    <td></td></tr>`;
}

// ---- checklist: quais condomínios a Delta atende como síndica ----

function efetivo(c) {
  return pendentes.has(c.id) ? pendentes.get(c.id) : !!c.deltaSindica;
}

function renderChecklist() {
  const host = document.getElementById('deltaCondominios');
  host.classList.toggle('locked', travado);
  const termo = paraBusca(buscaCondominios).trim();
  const sorted = [...condominios]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .filter((c) => !termo || paraBusca(c.nome).includes(termo));

  if (!condominios.length) {
    host.innerHTML = '<div class="carteira-lista-vazio">Cadastre um condomínio primeiro (Condomínios).</div>';
    atualizaContagemDelta();
    return;
  }
  if (!sorted.length) {
    host.innerHTML = '<div class="carteira-lista-vazio">Nenhum condomínio encontrado para esta busca.</div>';
    atualizaContagemDelta();
    return;
  }

  host.innerHTML = sorted.map((c) => {
    const marcado = efetivo(c);
    return `<label class="carteira-item ${marcado ? 'on' : ''}">
      <input type="checkbox" data-cond="${c.id}" ${marcado ? 'checked' : ''} ${travado ? 'disabled' : ''}>
      ${escapeHtml(c.nome)}
    </label>`;
  }).join('');

  host.querySelectorAll('[data-cond]').forEach((chk) => chk.addEventListener('change', () => toggleDelta(chk)));
  atualizaContagemDelta();
}

function atualizaContagemDelta() {
  const n = condominios.filter((c) => efetivo(c)).length;
  document.getElementById('deltaContagem').textContent =
    n === 0 ? 'nenhum condomínio marcado' : `${n} condomínio${n > 1 ? 's' : ''} marcado${n > 1 ? 's' : ''}`;
}

function atualizaTravaUI() {
  document.getElementById('deltaTravaStatus').textContent =
    travado ? '🔒 Lista travada' : '🔓 Editando — clique em "Salvar e travar" ao terminar';
  document.getElementById('btnDeltaDestravar').disabled = salvando;
  document.getElementById('btnDeltaSalvar').disabled = travado || salvando;
}

// Enquanto destravada, o clique só fica pendente em memória — nada é
// gravado até "Salvar e travar" (ver salvarETravar). Isso evita gravar uma
// a uma a cada clique, o que tornaria fácil salvar um clique acidental.
function toggleDelta(chk) {
  if (travado) return;
  const id = chk.dataset.cond;
  const c = condominios.find((x) => x.id === id);
  if (!c) return;
  pendentes.set(id, chk.checked);
  chk.closest('.carteira-item').classList.toggle('on', chk.checked);
  atualizaContagemDelta();
}

async function destravar() {
  if (salvando) return;
  await reload();
  pendentes.clear();
  travado = false;
  atualizaTravaUI();
  renderChecklist();
}

async function salvarETravar() {
  if (travado || salvando) return;
  salvando = true;
  atualizaTravaUI();
  try {
    const mudancas = [...pendentes.entries()].filter(([id, val]) => {
      const c = condominios.find((x) => x.id === id);
      return c && !!c.deltaSindica !== val;
    });
    for (const [id, val] of mudancas) {
      const c = condominios.find((x) => x.id === id);
      // updateCondominio regrava o registro inteiro — leva todos os campos
      // atuais, só trocando deltaSindica (mesmo critério de saveCarteira).
      await api.updateCondominio({ ...c, deltaSindica: val });
    }
    pendentes.clear();
    travado = true;
    await reload();
    if (mudancas.length) toast('Condomínios da Delta atualizados.', 'success');
  } catch (e) {
    toast('Erro ao salvar: ' + errorText(e), 'error');
  } finally {
    salvando = false;
    atualizaTravaUI();
    renderChecklist();
  }
}

function imprimir() {
  const lista = listaFiltrada();
  if (!lista.length) { toast('Nada para imprimir com este filtro.', 'error'); return; }
  printDocument(buildDeltaSindicosDoc(lista, filtroMes ? mesAnoLabel(filtroMes) : 'Todos os meses'));
}
