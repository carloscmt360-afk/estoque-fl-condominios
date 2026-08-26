import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';
import { bindMask, maskTelefone } from '../masks.js';

// Gestão SOS > Suprimentos — a equipe de campo, por categoria de função.
// Cadastro simples (sem carteira, sem vínculo com condomínio): só contato +
// Chave PIX de cada pessoa.

const CATEGORIA_LABEL = {
  gestor: 'Gestor',
  assistente: 'Assistente',
  auxiliar: 'Auxiliar',
  vistoriador_predial: 'Vistoriador predial',
};

let suprimentos = [];
let busca = '';
let wired = false;

export async function initSuprimentos() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovoSuprimento').addEventListener('click', () => openSuprimentoModal());
    document.getElementById('btnSalvarSuprimento').addEventListener('click', saveSuprimento);
    document.getElementById('supBusca').addEventListener('input', (e) => {
      busca = e.target.value.toLowerCase();
      render();
    });
    enableRowSelection(document.getElementById('suprimentosTbody'));
    bindMask(document.getElementById('supTelefone'), maskTelefone);
  }
  await reload();
}

export async function reload() {
  try {
    suprimentos = await api.listSuprimentos();
  } catch (e) {
    toast('Erro ao carregar os suprimentos: ' + errorText(e), 'error');
    suprimentos = [];
  }
  document.getElementById('btnNovoSuprimento').style.display = can('suprimentos', 'create') ? '' : 'none';
  render();
}

function bateBusca(s) {
  if (!busca) return true;
  return [s.nome, s.categoriaLabel || CATEGORIA_LABEL[s.categoria] || s.categoria, s.telefone, s.email]
    .filter(Boolean).join(' ').toLowerCase().includes(busca);
}

function render() {
  const tbody = document.getElementById('suprimentosTbody');
  const filtrados = suprimentos.filter(bateBusca);
  document.getElementById('suprimentosTotal').textContent = suprimentos.length;
  document.getElementById('suprimentosFiltrados').textContent = busca ? ` · ${filtrados.length} no filtro` : '';

  if (!filtrados.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="7">${
      suprimentos.length ? 'Nenhum suprimento encontrado com esse filtro.' : 'Nenhum suprimento cadastrado ainda.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = filtrados.map((s) => {
    const acoes = [
      can('suprimentos', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${s.id}">Editar</button>` : '',
      can('suprimentos', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${s.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    return `<tr>
      <td class="num">${s.numero}</td>
      <td><b>${escapeHtml(s.nome)}</b></td>
      <td>${escapeHtml(s.categoriaLabel || CATEGORIA_LABEL[s.categoria] || s.categoria)}</td>
      <td>${s.telefone ? escapeHtml(s.telefone) : '<span class="muted">—</span>'}</td>
      <td>${s.email ? escapeHtml(s.email) : '<span class="muted">—</span>'}</td>
      <td>${s.chavePix ? escapeHtml(s.chavePix) : '<span class="muted">—</span>'}</td>
      <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openSuprimentoModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteSuprimento(b.dataset.excluir)));
}

function openSuprimentoModal(id) {
  const s = id ? suprimentos.find((x) => x.id === id) : null;
  document.getElementById('modalSuprimentoTitulo').textContent = id ? 'Editar Suprimento' : 'Novo Suprimento';
  document.getElementById('supId').value = id || '';
  document.getElementById('supNome').value = s ? s.nome : '';
  document.getElementById('supCategoria').value = s ? s.categoria : 'gestor';
  document.getElementById('supTelefone').value = s ? maskTelefone(s.telefone) : '';
  document.getElementById('supEmail').value = s ? s.email : '';
  document.getElementById('supChavePix').value = s ? s.chavePix : '';
  document.getElementById('supObservacoes').value = s ? s.observacoes : '';
  openModal('modalSuprimento');
}

async function saveSuprimento() {
  const id = document.getElementById('supId').value;
  const nome = document.getElementById('supNome').value.trim();
  if (!nome) { toast('Informe o nome.', 'error'); return; }
  const payload = {
    id: id || uid('sup_'),
    nome,
    categoria: document.getElementById('supCategoria').value,
    telefone: document.getElementById('supTelefone').value.trim(),
    email: document.getElementById('supEmail').value.trim(),
    chavePix: document.getElementById('supChavePix').value.trim(),
    observacoes: document.getElementById('supObservacoes').value.trim(),
    createdAt: id ? '' : nowIso(),
  };
  try {
    if (id) await api.updateSuprimento(payload);
    else await api.createSuprimento(payload);
    await reload();
    closeModal('modalSuprimento');
    toast('Suprimento salvo.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteSuprimento(id) {
  const s = suprimentos.find((x) => x.id === id);
  if (!s) return;
  if (!confirm(`Excluir "${s.nome}"?`)) return;
  try {
    await api.deleteSuprimento(id);
    await reload();
    toast('Suprimento excluído.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
