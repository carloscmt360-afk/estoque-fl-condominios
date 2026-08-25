import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtDateBR } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';
import { printDocument, buildPrazosRelatorioDoc } from '../print.js';

// Gestão de Prazos: tipos de serviço (catálogo com prazo de vencimento),
// vínculos (qual condomínio contratou qual serviço, e quando renovou pela
// última vez) e o histórico de renovações.
//
// O vencimento e a situação vêm PRONTOS do backend a cada `reload()` — este
// arquivo nunca calcula data, só exibe o que o C++ mandou (ver
// core-cpp/include/estoque/dates_engine.hpp). Assim a regra de "quando vence"
// existe uma vez só, e não em duas linguagens que divergem na primeira
// mudança.
//
// O núcleo guarda e calcula tudo em DIAS (prazoDias); só a tela de Tipo de
// Serviço fala em MESES com o usuário, convertendo 1 mês = 30 dias na
// entrada/saída — ver mesesParaDias/diasParaMeses abaixo.

const DIAS_POR_MES = 30;
const diasParaMeses = (dias) => Math.round(dias / DIAS_POR_MES);
const mesesParaDias = (meses) => meses * DIAS_POR_MES;

const STATUS_LABEL = { ok: 'Em dia', atencao: 'Atenção', vencido: 'Vencido' };
const STATUS_PILL = { ok: 'pill-ok', atencao: 'pill-warn', vencido: 'pill-low' };

let condominios = [];
let tiposServico = [];
let vinculos = [];
let historico = [];
let filtroStatus = '';
let buscaVinculos = '';
let wired = false;

export async function initPrazos() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovoTipoServico').addEventListener('click', () => openTipoServicoModal());
    document.getElementById('btnSalvarTipoServico').addEventListener('click', saveTipoServico);
    document.getElementById('btnNovoVinculo').addEventListener('click', () => openVinculoModal());
    document.getElementById('btnSalvarVinculo').addEventListener('click', saveVinculo);
    document.getElementById('btnSalvarRenovacao').addEventListener('click', saveRenovacao);
    document.getElementById('gpFiltroStatus').addEventListener('change', (e) => {
      filtroStatus = e.target.value;
      renderVinculos();
    });
    document.getElementById('gpVinculosBusca').addEventListener('input', (e) => {
      buscaVinculos = e.target.value.toLowerCase();
      renderVinculos();
    });
    document.getElementById('btnImprimirPrazos').addEventListener('click', imprimirRelatorioPrazos);
    enableRowSelection(document.getElementById('gpVinculosTbody'));
    enableRowSelection(document.getElementById('gpTiposTbody'));
    enableRowSelection(document.getElementById('gpHistoricoTbody'));
  }
  await reload();
}

export async function reload() {
  [condominios, tiposServico, vinculos, historico] = await Promise.all([
    api.listCondominios(),
    api.listTiposServico(),
    api.listServicosCondominio(),
    api.listRenovacoes(),
  ]);
  document.getElementById('btnNovoTipoServico').style.display = can('gestao_datas', 'create') ? '' : 'none';
  document.getElementById('btnNovoVinculo').style.display = can('gestao_datas', 'create') ? '' : 'none';
  renderStats();
  renderVinculos();
  renderTiposServico();
  renderHistorico();
}

function renderStats() {
  const contagem = { ok: 0, atencao: 0, vencido: 0 };
  vinculos.forEach((v) => { contagem[v.status] = (contagem[v.status] || 0) + 1; });
  const tiles = [
    { label: 'Serviços acompanhados', value: vinculos.length, cls: '' },
    { label: 'Em dia', value: contagem.ok, cls: 'is-good' },
    { label: 'Vencem em até 30 dias', value: contagem.atencao, cls: 'is-warn' },
    { label: 'Vencidos', value: contagem.vencido, cls: 'is-critical' },
  ];
  document.getElementById('gpStatGrid').innerHTML = tiles.map((t) => `
    <div class="stat-tile ${t.cls}"><div class="label">${escapeHtml(t.label)}</div><div class="value">${t.value}</div></div>
  `).join('');
}

/* O texto diz o prazo em dias; a cor só reforça. Quem não distingue as cores
   continua lendo "Vencido há 12 dias" na própria pílula. */
function statusPillHtml(v) {
  const cls = STATUS_PILL[v.status] || '';
  const texto = v.status === 'vencido'
    ? `Vencido há ${Math.abs(v.diasRestantes)} dia(s)`
    : `${STATUS_LABEL[v.status] || v.status} · ${v.diasRestantes} dia(s)`;
  return `<span class="pill ${cls}">${escapeHtml(texto)}</span>`;
}

// Mesmo filtro (situação + busca por condomínio/serviço/empresa) usado na
// tela e no relatório impresso — o botão "Imprimir relatório" sempre traz
// exatamente o que está na tela, nunca uma lista à parte.
function vinculosFiltrados() {
  let lista = [...vinculos];
  if (filtroStatus) lista = lista.filter((v) => v.status === filtroStatus);
  if (buscaVinculos) {
    lista = lista.filter((v) =>
      [v.condominioNome, v.tipoServicoNome, v.empresaContratada].filter(Boolean).join(' ').toLowerCase().includes(buscaVinculos));
  }
  // O mais urgente primeiro: quem já venceu (dias negativos) encabeça a lista.
  lista.sort((a, b) => a.diasRestantes - b.diasRestantes);
  return lista;
}

function renderVinculos() {
  const tbody = document.getElementById('gpVinculosTbody');
  const lista = vinculosFiltrados();

  if (!lista.length) {
    const vazio = !vinculos.length
      ? (condominios.length && tiposServico.length
        ? 'Nenhum serviço vinculado ainda. Use “Novo vínculo” para acompanhar o vencimento de um serviço.'
        : 'Cadastre um condomínio (em Cadastro de Condomínios) e um tipo de serviço abaixo para começar.')
      : 'Nenhum serviço nesta situação.';
    tbody.innerHTML = `<tr class="empty-row"><td colspan="7">${escapeHtml(vazio)}</td></tr>`;
    return;
  }
  tbody.innerHTML = lista.map((v) => {
    const acoes = [
      can('gestao_datas', 'update') ? `<button class="btn-sm btn-primary" data-renovar="${v.id}">Renovar</button>` : '',
      can('gestao_datas', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${v.id}">Editar</button>` : '',
      can('gestao_datas', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${v.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    return `<tr>
      <td><b>${escapeHtml(v.condominioNome)}</b></td>
      <td><span class="dot-cor" style="background:${escapeHtml(v.tipoServicoCor || '#6c8ca8')}"></span>${escapeHtml(v.tipoServicoNome)}</td>
      <td>${v.empresaContratada ? escapeHtml(v.empresaContratada) : '<span class="muted">—</span>'}</td>
      <td>${fmtDateBR(v.dataUltimaRenovacao)}</td>
      <td>${fmtDateBR(v.dataVencimento)}</td>
      <td>${statusPillHtml(v)}</td>
      <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');

  tbody.querySelectorAll('[data-renovar]').forEach((b) =>
    b.addEventListener('click', () => openRenovarModal(b.dataset.renovar)));
  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openVinculoModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteVinculo(b.dataset.excluir)));
}

function imprimirRelatorioPrazos() {
  const lista = vinculosFiltrados();
  if (!lista.length) { toast('Nenhum serviço para imprimir com esses filtros.', 'error'); return; }
  printDocument(buildPrazosRelatorioDoc(lista, {
    status: STATUS_LABEL[filtroStatus] || 'Todas as situações',
    busca: buscaVinculos,
  }));
}

function renderTiposServico() {
  const tbody = document.getElementById('gpTiposTbody');
  const sorted = [...tiposServico].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'));
  if (!sorted.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="4">Nenhum tipo de serviço cadastrado ainda.</td></tr>';
    return;
  }
  tbody.innerHTML = sorted.map((t) => {
    const acoes = [
      can('gestao_datas', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${t.id}">Editar</button>` : '',
      can('gestao_datas', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${t.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    return `<tr>
      <td><span class="dot-cor" style="background:${escapeHtml(t.cor)}"></span><b>${escapeHtml(t.nome)}</b></td>
      <td class="num">${diasParaMeses(t.prazoDias)}</td>
      <td class="num">${vinculos.filter((v) => v.tipoServicoId === t.id).length}</td>
      <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openTipoServicoModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteTipoServico(b.dataset.excluir)));
}

function renderHistorico() {
  const tbody = document.getElementById('gpHistoricoTbody');
  if (!historico.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="5">Nenhuma renovação registrada ainda.</td></tr>';
    return;
  }
  tbody.innerHTML = historico.map((r) => `<tr>
    <td>${escapeHtml(r.condominioNome)}</td>
    <td>${escapeHtml(r.tipoServicoNome)}</td>
    <td>${fmtDateBR(r.dataRenovacao)}</td>
    <td>${r.empresaContratada ? escapeHtml(r.empresaContratada) : '<span class="muted">—</span>'}</td>
    <td class="num">${diasParaMeses(r.prazoDiasAplicado)} ${diasParaMeses(r.prazoDiasAplicado) === 1 ? 'mês' : 'meses'}</td>
  </tr>`).join('');
}

// ------------------------------------------------------------- Tipo de serviço

function openTipoServicoModal(id) {
  document.getElementById('modalTipoServicoTitulo').textContent = id ? 'Editar Tipo de Serviço' : 'Novo Tipo de Serviço';
  document.getElementById('tsId').value = id || '';
  const t = id ? tiposServico.find((x) => x.id === id) : null;
  document.getElementById('tsNome').value = t ? t.nome : '';
  document.getElementById('tsPrazoMeses').value = t ? diasParaMeses(t.prazoDias) : 12;
  document.getElementById('tsCor').value = t ? t.cor : '#2e6ba6';
  openModal('modalTipoServico');
}

async function saveTipoServico() {
  const id = document.getElementById('tsId').value;
  const nome = document.getElementById('tsNome').value.trim();
  const prazoMeses = parseInt(document.getElementById('tsPrazoMeses').value, 10);
  const cor = document.getElementById('tsCor').value || '#2e6ba6';
  if (!nome) { toast('Informe o nome do serviço.', 'error'); return; }
  if (!(prazoMeses > 0)) { toast('O prazo precisa ser maior que zero.', 'error'); return; }
  const prazoDias = mesesParaDias(prazoMeses);
  try {
    if (id) await api.updateTipoServico({ id, nome, prazoDias, cor, createdAt: '' });
    else await api.createTipoServico({ id: uid('ts_'), nome, prazoDias, cor, createdAt: nowIso() });
    await reload();
    closeModal('modalTipoServico');
    toast('Tipo de serviço salvo.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteTipoServico(id) {
  const t = tiposServico.find((x) => x.id === id);
  if (!t) return;
  if (!confirm(`Excluir o tipo de serviço "${t.nome}"?`)) return;
  try {
    await api.deleteTipoServico(id);
    await reload();
    toast('Tipo de serviço excluído.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

// --------------------------------------------------------------------- Vínculo

function popularSelectsVinculo(selecionado) {
  const selCond = document.getElementById('vinCondominioId');
  const selTipo = document.getElementById('vinTipoServicoId');
  selCond.innerHTML = [...condominios]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((c) => `<option value="${c.id}">${escapeHtml(c.nome)}</option>`).join('');
  selTipo.innerHTML = [...tiposServico]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((t) => `<option value="${t.id}">${escapeHtml(t.nome)} (${diasParaMeses(t.prazoDias)} ${diasParaMeses(t.prazoDias) === 1 ? 'mês' : 'meses'})</option>`).join('');
  if (selecionado) {
    selCond.value = selecionado.condominioId;
    selTipo.value = selecionado.tipoServicoId;
  }
}

function openVinculoModal(id) {
  if (!condominios.length) { toast('Cadastre um condomínio primeiro (Cadastro de Condomínios).', 'error'); return; }
  if (!tiposServico.length) { toast('Cadastre um tipo de serviço primeiro.', 'error'); return; }

  document.getElementById('modalVinculoTitulo').textContent = id ? 'Editar Vínculo' : 'Novo Vínculo';
  document.getElementById('vinId').value = id || '';
  const v = id ? vinculos.find((x) => x.id === id) : null;
  popularSelectsVinculo(v);
  document.getElementById('vinDataUltimaRenovacao').value =
    v ? v.dataUltimaRenovacao.slice(0, 10) : new Date().toISOString().slice(0, 10);
  document.getElementById('vinEmpresaContratada').value = v ? v.empresaContratada : '';
  document.getElementById('vinObservacoes').value = v ? v.observacoes : '';
  openModal('modalVinculo');
}

async function saveVinculo() {
  const id = document.getElementById('vinId').value;
  const condominioId = document.getElementById('vinCondominioId').value;
  const tipoServicoId = document.getElementById('vinTipoServicoId').value;
  const dataUltimaRenovacao = document.getElementById('vinDataUltimaRenovacao').value;
  const empresaContratada = document.getElementById('vinEmpresaContratada').value.trim();
  const observacoes = document.getElementById('vinObservacoes').value.trim();
  if (!condominioId || !tipoServicoId) { toast('Selecione o condomínio e o tipo de serviço.', 'error'); return; }
  if (!dataUltimaRenovacao) { toast('Informe a data da última renovação.', 'error'); return; }
  try {
    if (id) await api.updateServicoCondominio({ id, condominioId, tipoServicoId, dataUltimaRenovacao, empresaContratada, observacoes });
    else await api.createServicoCondominio({ id: uid('sc_'), condominioId, tipoServicoId, dataUltimaRenovacao, empresaContratada, observacoes, createdAt: nowIso() });
    await reload();
    closeModal('modalVinculo');
    toast('Vínculo salvo.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteVinculo(id) {
  const v = vinculos.find((x) => x.id === id);
  if (!v) return;
  if (!confirm(`Excluir "${v.condominioNome} — ${v.tipoServicoNome}"? O histórico de renovações dele também será excluído.`)) return;
  try {
    await api.deleteServicoCondominio(id);
    await reload();
    toast('Vínculo excluído.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

// ------------------------------------------------------------------- Renovar

function openRenovarModal(servicoCondominioId) {
  const v = vinculos.find((x) => x.id === servicoCondominioId);
  if (!v) return;
  document.getElementById('renServicoCondominioId').value = servicoCondominioId;
  document.getElementById('renResumo').innerHTML =
    `<b>${escapeHtml(v.condominioNome)}</b> — ${escapeHtml(v.tipoServicoNome)}<br/>` +
    `Vencimento atual: ${fmtDateBR(v.dataVencimento)} · novo prazo de ${diasParaMeses(v.prazoDias)} ${diasParaMeses(v.prazoDias) === 1 ? 'mês' : 'meses'} a partir da data informada.`;
  document.getElementById('renDataRenovacao').value = new Date().toISOString().slice(0, 10);
  document.getElementById('renEmpresaContratada').value = v.empresaContratada || '';
  document.getElementById('renObservacoes').value = '';
  openModal('modalRenovar');
}

async function saveRenovacao() {
  const servicoCondominioId = document.getElementById('renServicoCondominioId').value;
  const dataRenovacao = document.getElementById('renDataRenovacao').value;
  const empresaContratada = document.getElementById('renEmpresaContratada').value.trim();
  const observacoes = document.getElementById('renObservacoes').value.trim();
  if (!dataRenovacao) { toast('Informe a data da renovação.', 'error'); return; }
  try {
    await api.renovarServico({ id: uid('ren_'), servicoCondominioId, dataRenovacao, empresaContratada, observacoes, createdAt: nowIso() });
    await reload();
    closeModal('modalRenovar');
    toast('Renovação registrada.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
