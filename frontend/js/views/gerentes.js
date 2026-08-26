import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';
import { bindMask, maskTelefone } from '../masks.js';

// Gestão SOS > Gerentes — cadastro simples de gerentes, mesmo formato de
// Cadastro de Condomínios. É esta lista que alimenta a tela de Carteiras (o
// clique num gerente ali abre a marcação de condomínios).

let gerentes = [];
let busca = '';
let wired = false;

export async function initGerentes() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovoGerente').addEventListener('click', () => openGerenteModal());
    document.getElementById('btnSalvarGerente').addEventListener('click', saveGerente);
    document.getElementById('gerBusca').addEventListener('input', (e) => {
      busca = e.target.value.toLowerCase();
      render();
    });
    enableRowSelection(document.getElementById('gerentesTbody'));
    bindMask(document.getElementById('gerTelefone'), maskTelefone);
  }
  await reload();
}

export async function reload() {
  gerentes = await api.listGerentes();
  document.getElementById('btnNovoGerente').style.display = can('gerentes', 'create') ? '' : 'none';
  render();
}

function bateBusca(g) {
  if (!busca) return true;
  return [g.nome, g.telefone, g.email, g.chavePix].filter(Boolean).join(' ').toLowerCase().includes(busca);
}

function render() {
  const tbody = document.getElementById('gerentesTbody');
  const sorted = [...gerentes].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'));
  const filtrados = sorted.filter(bateBusca);

  document.getElementById('gerentesTotal').textContent = sorted.length;
  document.getElementById('gerentesFiltrados').textContent = busca ? ` · ${filtrados.length} no filtro` : '';

  if (!filtrados.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="7">${
      sorted.length ? 'Nenhum gerente encontrado com esse filtro.' : 'Nenhum gerente cadastrado ainda.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = filtrados.map((g) => {
    const acoes = [
      can('gerentes', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${g.id}">Editar</button>` : '',
      can('gerentes', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${g.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    return `<tr>
      <td class="num">${g.numero}</td>
      <td><b>${escapeHtml(g.nome)}</b></td>
      <td>${g.telefone ? escapeHtml(g.telefone) : '<span class="muted">—</span>'}</td>
      <td>${g.email ? escapeHtml(g.email) : '<span class="muted">—</span>'}</td>
      <td class="num">${(g.condominios || []).length}</td>
      <td>${g.chavePix ? escapeHtml(g.chavePix) : '<span class="muted">—</span>'}</td>
      <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openGerenteModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteGerente(b.dataset.excluir)));
}

function openGerenteModal(id) {
  document.getElementById('modalGerenteTitulo').textContent = id ? 'Editar Gerente' : 'Novo Gerente';
  document.getElementById('gerId').value = id || '';
  const g = id ? gerentes.find((x) => x.id === id) : null;
  document.getElementById('gerNome').value = g ? g.nome : '';
  document.getElementById('gerTelefone').value = g ? maskTelefone(g.telefone) : '';
  document.getElementById('gerEmail').value = g ? g.email : '';
  document.getElementById('gerChavePix').value = g ? g.chavePix : '';
  document.getElementById('gerObservacoes').value = g ? g.observacoes : '';
  openModal('modalGerente');
}

async function saveGerente() {
  const id = document.getElementById('gerId').value;
  const nome = document.getElementById('gerNome').value.trim();
  const telefone = document.getElementById('gerTelefone').value.trim();
  const email = document.getElementById('gerEmail').value.trim();
  const chavePix = document.getElementById('gerChavePix').value.trim();
  const observacoes = document.getElementById('gerObservacoes').value.trim();
  if (!nome) { toast('Informe o nome do gerente.', 'error'); return; }
  try {
    // A carteira (condominioIds) não é editada aqui — só em "Carteiras", que
    // reenvia o gerente inteiro junto com a lista marcada. Ao salvar por
    // aqui, preserva a carteira já existente em vez de zerá-la.
    const atual = id ? gerentes.find((x) => x.id === id) : null;
    const condominioIds = atual ? (atual.condominios || []).map((c) => c.id) : [];
    if (id) await api.updateGerente({ id, nome, telefone, email, chavePix, observacoes, condominioIds, createdAt: '' });
    else await api.createGerente({ id: uid('ger_'), nome, telefone, email, chavePix, observacoes, condominioIds, createdAt: nowIso() });
    await reload();
    closeModal('modalGerente');
    toast('Gerente salvo.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteGerente(id) {
  const g = gerentes.find((x) => x.id === id);
  if (!g) return;
  if (!confirm(`Excluir "${g.nome}"? A carteira dele (condomínios marcados em Carteiras) também será desfeita.`)) return;
  try {
    await api.deleteGerente(id);
    await reload();
    toast('Gerente excluído.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
