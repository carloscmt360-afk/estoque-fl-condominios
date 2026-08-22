import { api } from '../api.js';
import { escapeHtml, uid, nowIso, fmtBRL } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';

let departments = [];
let busca = '';
let wired = false;

export async function initDepartments() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovoDepartamento').addEventListener('click', () => openDepartmentModal());
    document.getElementById('btnSalvarDepartamento').addEventListener('click', saveDepartment);
    document.getElementById('depBusca').addEventListener('input', (e) => {
      busca = e.target.value.toLowerCase();
      render();
    });
    enableRowSelection(document.getElementById('departmentsTbody'));
  }
  await reload();
}

export async function reload() {
  departments = await api.listDepartments();
  document.getElementById('btnNovoDepartamento').style.display =
    can('departamentos', 'create') ? '' : 'none';
  render();
}

function render() {
  const tbody = document.getElementById('departmentsTbody');
  const sorted = [...departments].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  const filtrados = busca
    ? sorted.filter((d) => [d.name, d.encarregado].filter(Boolean).join(' ').toLowerCase().includes(busca))
    : sorted;
  if (!filtrados.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="4">${
      sorted.length ? 'Nenhum departamento encontrado com esse filtro.' : 'Nenhum departamento cadastrado ainda.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = filtrados.map((d) => {
    const acoes = [
      can('departamentos', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${d.id}">Editar</button>` : '',
      can('departamentos', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${d.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    return `<tr><td><b>${escapeHtml(d.name)}</b></td><td>${escapeHtml(d.encarregado)}</td>
    <td class="num">${d.monthlyLimit > 0 ? fmtBRL(d.monthlyLimit) : '<span class="muted">sem limite</span>'}</td>
    <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-editar]').forEach((b) => b.addEventListener('click', () => openDepartmentModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) => b.addEventListener('click', () => deleteDepartment(b.dataset.excluir)));
}

function openDepartmentModal(id) {
  document.getElementById('modalDepartamentoTitulo').textContent = id ? 'Editar Departamento' : 'Novo Departamento';
  document.getElementById('depId').value = id || '';
  if (id) {
    const d = departments.find((x) => x.id === id);
    document.getElementById('depNome').value = d.name;
    document.getElementById('depEncarregado').value = d.encarregado;
    document.getElementById('depLimite').value = d.monthlyLimit || 0;
  } else {
    document.getElementById('depNome').value = '';
    document.getElementById('depEncarregado').value = '';
    document.getElementById('depLimite').value = 0;
  }
  openModal('modalDepartamento');
}

async function saveDepartment() {
  const id = document.getElementById('depId').value;
  const name = document.getElementById('depNome').value.trim();
  const encarregado = document.getElementById('depEncarregado').value.trim();
  const monthlyLimit = parseFloat(document.getElementById('depLimite').value) || 0;
  if (!name || !encarregado) { toast('Preencha nome e encarregado.', 'error'); return; }
  if (monthlyLimit < 0) { toast('O limite mensal não pode ser negativo.', 'error'); return; }
  try {
    if (id) await api.updateDepartment({ id, name, encarregado, monthlyLimit, createdAt: '' });
    else await api.createDepartment({ id: uid('dep_'), name, encarregado, monthlyLimit, createdAt: nowIso() });
    await reload();
    closeModal('modalDepartamento');
    toast('Departamento salvo.', 'success');
  } catch (e) { toast('Erro: ' + e, 'error'); }
}

async function deleteDepartment(id) {
  const d = departments.find((x) => x.id === id);
  if (!d) return;
  if (!confirm(`Excluir "${d.name}"? O histórico de saídas será mantido.`)) return;
  await api.deleteDepartment(id);
  await reload();
  toast('Departamento excluído.', 'success');
}
