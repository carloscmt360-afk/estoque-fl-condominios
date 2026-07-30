import { api } from '../api.js';
import { escapeHtml, uid, nowIso } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';

let departments = [];

export async function initDepartments() {
  document.getElementById('btnNovoDepartamento').addEventListener('click', () => openDepartmentModal());
  document.getElementById('btnSalvarDepartamento').addEventListener('click', saveDepartment);
  await reload();
}

export async function reload() {
  departments = await api.listDepartments();
  render();
}

function render() {
  const tbody = document.getElementById('departmentsTbody');
  const sorted = [...departments].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  if (!sorted.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="3">Nenhum departamento cadastrado ainda.</td></tr>';
    return;
  }
  tbody.innerHTML = sorted.map((d) => `<tr><td><b>${escapeHtml(d.name)}</b></td><td>${escapeHtml(d.encarregado)}</td>
    <td><div class="row-actions"><button class="btn-sm btn-ghost" data-editar="${d.id}">Editar</button>
    <button class="btn-sm btn-danger" data-excluir="${d.id}">Excluir</button></div></td></tr>`).join('');
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
  } else {
    document.getElementById('depNome').value = '';
    document.getElementById('depEncarregado').value = '';
  }
  openModal('modalDepartamento');
}

async function saveDepartment() {
  const id = document.getElementById('depId').value;
  const name = document.getElementById('depNome').value.trim();
  const encarregado = document.getElementById('depEncarregado').value.trim();
  if (!name || !encarregado) { toast('Preencha nome e encarregado.', 'error'); return; }
  try {
    if (id) await api.updateDepartment({ id, name, encarregado, createdAt: '' });
    else await api.createDepartment({ id: uid('dep_'), name, encarregado, createdAt: nowIso() });
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
