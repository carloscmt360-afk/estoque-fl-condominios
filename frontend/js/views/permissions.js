import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtNum } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';

// Grupos de permissão e o vínculo com os departamentos.
//
// O modelo (que é o pedido do cliente) em uma linha:
//   grupo -> DEPARTAMENTO -> usuários do departamento.
// Ninguém recebe permissão direto; quem recebe é o setor.
//
// Os rótulos da matriz (o que "criar/ver/editar/excluir" libera em cada
// função) vêm do C++ junto com os dados — esta tela não reescreve a regra em
// JavaScript, senão as duas versões divergiriam na primeira mudança.

const ACOES = [
  { key: 'create', col: 'create', label: 'Criar' },
  { key: 'read', col: 'read', label: 'Ver' },
  { key: 'update', col: 'update', label: 'Editar' },
  { key: 'delete', col: 'delete', label: 'Excluir' },
];
const SIGLA = { create: 'C', read: 'V', update: 'E', delete: 'X' };

let groups = [];
let departments = [];
let features = [];
let wired = false;

export async function initPermissions() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovoGrupo').addEventListener('click', () => openGroupModal());
    document.getElementById('btnSalvarGrupo').addEventListener('click', saveGroup);
  }
  await reload();
}

export async function reload() {
  const data = await api.listPermissions();
  groups = data.groups || [];
  departments = data.departments || [];
  features = data.features || [];
  renderGroups();
  renderDeptGroups();
}

function groupById(id) {
  return groups.find((g) => g.id === id) || null;
}

function deptsOfGroup(groupId) {
  return departments.filter((d) => d.permissionGroupId === groupId);
}

/* Resumo em chips: "Produtos CV", "Requisições CVE" — leitura rápida de quem
   pode o quê sem abrir o modal. */
function permSummaryHtml(group) {
  const chips = features
    .map((f) => {
      const p = group.perms[f.key] || {};
      const letras = ACOES.filter((a) => p[a.col]).map((a) => SIGLA[a.col]).join('');
      return letras ? `<span class="chip">${escapeHtml(f.label)}<i>${letras}</i></span>` : '';
    })
    .filter(Boolean);
  if (!chips.length) return '<span class="muted">nenhuma permissão marcada</span>';
  return `<div class="perm-summary">${chips.join('')}</div>`;
}

function renderGroups() {
  const tbody = document.getElementById('groupsTbody');
  if (!groups.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="5">Nenhum grupo de permissão criado ainda. ' +
      'Crie um grupo e vincule-o a um departamento na tabela abaixo.</td></tr>';
    return;
  }
  tbody.innerHTML = groups.map((g) => {
    const usados = deptsOfGroup(g.id);
    return `<tr>
      <td><b>${escapeHtml(g.name)}</b></td>
      <td>${g.description ? escapeHtml(g.description) : '<span class="muted">—</span>'}</td>
      <td>${permSummaryHtml(g)}</td>
      <td>${usados.length
        ? usados.map((d) => escapeHtml(d.name)).join(', ')
        : '<span class="muted">nenhum</span>'}</td>
      <td><div class="row-actions">
        <button class="btn-sm btn-ghost" data-editar="${g.id}">Editar</button>
        <button class="btn-sm btn-danger" data-excluir="${g.id}">Excluir</button>
      </div></td></tr>`;
  }).join('');

  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openGroupModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteGroup(b.dataset.excluir)));
}

function renderDeptGroups() {
  const tbody = document.getElementById('deptGroupsTbody');
  if (!departments.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="4">Nenhum departamento cadastrado. ' +
      'Cadastre os setores primeiro — é a eles que a permissão é atribuída.</td></tr>';
    return;
  }
  const opcoes = (selecionado) =>
    '<option value="">— sem grupo (nenhuma permissão) —</option>' +
    groups.map((g) => `<option value="${g.id}" ${g.id === selecionado ? 'selected' : ''}>${escapeHtml(g.name)}</option>`).join('');

  tbody.innerHTML = departments.map((d) => `<tr>
      <td><b>${escapeHtml(d.name)}</b></td>
      <td>${d.encarregado ? escapeHtml(d.encarregado) : '<span class="muted">—</span>'}</td>
      <td class="num">${fmtNum(d.activeUsers || 0)}</td>
      <td><select data-dept="${d.id}">${opcoes(d.permissionGroupId || '')}</select>
        ${!d.permissionGroupId && d.activeUsers > 0
          ? '<div class="muted" style="font-size:10.5px;">Os usuários deste setor não enxergam nada enquanto não houver grupo.</div>'
          : ''}</td>
    </tr>`).join('');

  tbody.querySelectorAll('[data-dept]').forEach((sel) =>
    sel.addEventListener('change', () => setDeptGroup(sel.dataset.dept, sel.value)));
}

async function setDeptGroup(departmentId, groupId) {
  try {
    await api.setDepartmentPermissionGroup(departmentId, groupId);
    await reload();
    toast('Permissões do departamento atualizadas.', 'success');
  } catch (e) {
    toast('Erro: ' + errorText(e), 'error');
    await reload();  // devolve o select ao valor que o banco realmente tem
  }
}

// ------------------------------------------------------------------ modal

function renderMatrix(perms) {
  document.getElementById('grpMatrizTbody').innerHTML = features.map((f) => {
    const p = perms[f.key] || {};
    const celulas = ACOES.map((a) => {
      const hint = f[a.col];
      // Rótulo vazio no catálogo = a ação não existe naquela função. Melhor
      // desabilitar e dizer isso do que oferecer uma caixinha sem efeito.
      if (!hint) return '<td class="na" title="Esta ação não existe nesta função">—</td>';
      return `<td><input type="checkbox" data-feature="${f.key}" data-acao="${a.col}"
        ${p[a.col] ? 'checked' : ''} title="${escapeHtml(hint)}" aria-label="${escapeHtml(f.label + ' — ' + a.label)}" /></td>`;
    }).join('');
    const hints = ACOES.filter((a) => f[a.col]).map((a) => `<b>${a.label}:</b> ${escapeHtml(f[a.col])}`).join(' · ');
    return `<tr><td><div class="feat-nome">${escapeHtml(f.label)}</div>
      <div class="feat-hint">${hints}</div></td>${celulas}</tr>`;
  }).join('');
}

function openGroupModal(id) {
  const g = id ? groupById(id) : null;
  document.getElementById('modalGrupoTitulo').textContent = g ? 'Editar grupo de permissão' : 'Novo grupo de permissão';
  document.getElementById('grpId').value = id || '';
  document.getElementById('grpNome').value = g ? g.name : '';
  document.getElementById('grpDescricao').value = g ? g.description : '';
  renderMatrix(g ? g.perms : {});
  openModal('modalGrupo');
}

function collectMatrix() {
  const perms = {};
  features.forEach((f) => {
    perms[f.key] = { create: false, read: false, update: false, delete: false };
  });
  document.querySelectorAll('#grpMatrizTbody input[type="checkbox"]').forEach((cb) => {
    perms[cb.dataset.feature][cb.dataset.acao] = cb.checked;
  });
  return perms;
}

async function saveGroup() {
  const id = document.getElementById('grpId').value;
  const group = {
    id: id || uid('grp_'),
    name: document.getElementById('grpNome').value.trim(),
    description: document.getElementById('grpDescricao').value.trim(),
    createdAt: id ? '' : nowIso(),
    perms: collectMatrix(),
  };
  if (!group.name) { toast('Informe o nome do grupo.', 'error'); return; }

  try {
    if (id) await api.updatePermissionGroup(group);
    else await api.createPermissionGroup(group);
    await reload();
    closeModal('modalGrupo');
    toast('Grupo salvo. Os usuários dos setores vinculados já usam a nova matriz.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteGroup(id) {
  const g = groupById(id);
  if (!g) return;
  const usados = deptsOfGroup(id);
  const aviso = usados.length
    ? `\n\n${usados.length} departamento(s) usam este grupo (${usados.map((d) => d.name).join(', ')}) ` +
      'e ficarão SEM permissão nenhuma.'
    : '';
  if (!confirm(`Excluir o grupo "${g.name}"?${aviso}`)) return;
  try {
    await api.deletePermissionGroup(id);
    await reload();
    toast('Grupo excluído.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
