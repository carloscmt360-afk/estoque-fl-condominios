import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';

// Setorização — os quatro setores do negócio e, dentro de cada um, as
// especialidades (nichos) que o usuário cadastra.
//
// Os setores vêm do backend (catálogo fixo em companies_engine.cpp): esta tela
// não os inventa nem os reordena, só desenha as abas que ele mandou. Assim
// acrescentar um quinto setor no futuro é uma linha em C++, e não uma caçada
// por listas repetidas no JavaScript.

let setores = [];
let setorAtivo = '';
let wired = false;

export async function initSetorizacao() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovaEspecialidade').addEventListener('click', () => openEspecialidadeModal());
    document.getElementById('btnSalvarEspecialidade').addEventListener('click', saveEspecialidade);
  }
  await reload();
}

export async function reload() {
  const dados = await api.listSetorizacao();
  setores = dados.setores || [];
  if (!setores.some((s) => s.key === setorAtivo)) setorAtivo = setores.length ? setores[0].key : '';
  document.getElementById('btnNovaEspecialidade').style.display = can('empresas', 'create') ? '' : 'none';
  renderAbas();
  renderEspecialidades();
}

function setorDe(key) {
  return setores.find((s) => s.key === key) || null;
}

/* As quatro abas, cada uma com a contagem de nichos — dá para ver de relance
   qual setor ainda está vazio, sem precisar clicar em todos. */
function renderAbas() {
  const host = document.getElementById('setAbas');
  host.innerHTML = setores.map((s) => `
    <button class="${s.key === setorAtivo ? 'active' : ''}" data-setor="${s.key}">
      ${escapeHtml(s.label)} <span class="seg-count">${s.especialidades.length}</span>
    </button>`).join('');
  host.querySelectorAll('[data-setor]').forEach((b) => b.addEventListener('click', () => {
    setorAtivo = b.dataset.setor;
    renderAbas();
    renderEspecialidades();
  }));
}

function renderEspecialidades() {
  const setor = setorDe(setorAtivo);
  const tbody = document.getElementById('setEspecialidadesTbody');
  document.getElementById('setSetorAtual').textContent = setor ? setor.label : '—';

  const lista = setor ? setor.especialidades : [];
  if (!lista.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="3">Nenhuma especialidade em
      ${escapeHtml(setor ? setor.label : 'neste setor')} ainda. Use “＋ Nova especialidade”.</td></tr>`;
    return;
  }
  tbody.innerHTML = lista.map((e) => {
    const acoes = [
      can('empresas', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${e.id}">Editar</button>` : '',
      can('empresas', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${e.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    // A contagem de empresas é o que explica por que uma exclusão pode ser
    // recusada — mostrar depois do clique seria tarde.
    const emUso = e.empresas > 0
      ? `<span class="pill pill-accent">${e.empresas} empresa${e.empresas > 1 ? 's' : ''}</span>`
      : '<span class="muted">nenhuma empresa</span>';
    return `<tr>
      <td><b>${escapeHtml(e.nome)}</b></td>
      <td>${emUso}</td>
      <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openEspecialidadeModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteEspecialidade(b.dataset.excluir)));
}

function todasEspecialidades() {
  return setores.flatMap((s) => s.especialidades.map((e) => ({ ...e, setorKey: s.key })));
}

function openEspecialidadeModal(id) {
  const e = id ? todasEspecialidades().find((x) => x.id === id) : null;
  document.getElementById('modalEspecialidadeTitulo').textContent =
    id ? 'Editar Especialidade' : 'Nova Especialidade';
  document.getElementById('espId').value = id || '';
  const sel = document.getElementById('espSetor');
  sel.innerHTML = setores.map((s) => `<option value="${s.key}">${escapeHtml(s.label)}</option>`).join('');
  sel.value = e ? e.setorKey : setorAtivo;
  document.getElementById('espNome').value = e ? e.nome : '';
  openModal('modalEspecialidade');
}

async function saveEspecialidade() {
  const id = document.getElementById('espId').value;
  const setor = document.getElementById('espSetor').value;
  const nome = document.getElementById('espNome').value.trim();
  if (!nome) { toast('Informe o nome da especialidade.', 'error'); return; }
  try {
    if (id) await api.updateEspecialidade({ id, setor, nome });
    else await api.createEspecialidade({ id: uid('esp_'), setor, nome, createdAt: nowIso() });
    // Vai para a aba do setor salvo: editar mudando o setor sem isso deixaria
    // a tela mostrando uma aba onde o item não está mais.
    setorAtivo = setor;
    await reload();
    closeModal('modalEspecialidade');
    toast('Especialidade salva.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteEspecialidade(id) {
  const e = todasEspecialidades().find((x) => x.id === id);
  if (!e) return;
  if (!confirm(`Excluir a especialidade "${e.nome}"?`)) return;
  try {
    await api.deleteEspecialidade(id);
    await reload();
    toast('Especialidade excluída.', 'success');
  } catch (err) { toast('Erro: ' + errorText(err), 'error'); }
}
