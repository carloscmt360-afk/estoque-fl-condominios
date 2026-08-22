import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtNum, fmtDateTimeBR } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { currentUser } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';

// Gestão de usuários — tela exclusiva do superadministrador (o backend recusa
// qualquer chamada daqui vinda de outro papel; ver api.cpp).
//
// Repare no que esta tela NÃO tem: caixinhas de permissão por pessoa. A
// permissão vem do departamento (tela de Permissões) — aqui só se define em
// qual setor a pessoa está, que é o que determina o que ela enxerga.

let users = [];
let departments = [];
let wired = false;

export async function initUsers() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovoUsuario').addEventListener('click', () => openUserModal());
    document.getElementById('btnSalvarUsuario').addEventListener('click', saveUser);
    document.getElementById('btnSalvarSenhaUsuario').addEventListener('click', saveUserPassword);
    document.getElementById('usrPapel').addEventListener('change', updatePermHint);
    document.getElementById('usrDepartamento').addEventListener('change', updatePermHint);
    ['filtroUsuarioBusca', 'filtroUsuarioPapel', 'filtroUsuarioSituacao'].forEach((id) => {
      document.getElementById(id).addEventListener('input', render);
    });
    enableRowSelection(document.getElementById('usersTbody'));
  }
  await reload();
}

export async function reload() {
  [users, departments] = await Promise.all([api.listUsers(), api.listDepartments()]);
  renderStatGrid();
  render();
}

function deptName(id) {
  const d = departments.find((x) => x.id === id);
  return d ? d.name : '';
}

function renderStatGrid() {
  const ativos = users.filter((u) => u.active).length;
  const admins = users.filter((u) => u.role === 'superadmin' && u.active).length;
  const semSetor = users.filter((u) => u.active && u.role !== 'superadmin' && !u.departmentId).length;
  document.getElementById('usersStatGrid').innerHTML = [
    { label: 'Usuários cadastrados', value: fmtNum(users.length) },
    { label: 'Ativos', value: fmtNum(ativos) },
    { label: 'Superadministradores', value: fmtNum(admins) },
    // Sem setor não é erro de cadastro, mas é gente que não herda permissão
    // nenhuma e não consegue requisitar — vale sinalizar.
    { label: 'Sem departamento', value: fmtNum(semSetor), cls: semSetor > 0 ? 'is-warn' : 'is-good' },
  ].map((t) => `<div class="stat-tile ${t.cls || ''}"><div class="label">${escapeHtml(t.label)}</div>
      <div class="value">${t.value}</div></div>`).join('');
}

function render() {
  const busca = (document.getElementById('filtroUsuarioBusca').value || '').toLowerCase();
  const papel = document.getElementById('filtroUsuarioPapel').value;
  const situacao = document.getElementById('filtroUsuarioSituacao').value;
  const eu = currentUser();

  let lista = [...users].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  if (busca) {
    lista = lista.filter((u) => (u.name + ' ' + u.email).toLowerCase().includes(busca));
  }
  if (papel) lista = lista.filter((u) => u.role === papel);
  if (situacao) lista = lista.filter((u) => (situacao === 'ativo' ? u.active : !u.active));

  const tbody = document.getElementById('usersTbody');
  if (!lista.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="7">${
      users.length ? 'Nenhum usuário com esses filtros.' : 'Nenhum usuário cadastrado.'
    }</td></tr>`;
    return;
  }

  tbody.innerHTML = lista.map((u) => {
    const souEu = eu && eu.id === u.id;
    const setor = u.departmentName || deptName(u.departmentId);
    return `<tr>
      <td><b>${escapeHtml(u.name)}</b>${souEu ? ' <span class="pill pill-accent">você</span>' : ''}</td>
      <td>${escapeHtml(u.email)}</td>
      <td>${u.role === 'superadmin'
        ? '<span class="pill pill-accent">Superadministrador</span>'
        : '<span class="muted">Usuário</span>'}</td>
      <td>${setor ? escapeHtml(setor) : '<span class="muted">— sem setor —</span>'}</td>
      <td><span class="pill ${u.active ? 'pill-ok' : 'pill-low'}">${u.active ? 'Ativo' : 'Inativo'}</span></td>
      <td>${u.lastLoginAt ? escapeHtml(fmtDateTimeBR(u.lastLoginAt)) : '<span class="muted">nunca entrou</span>'}</td>
      <td><div class="row-actions">
        <button class="btn-sm btn-ghost" data-editar="${u.id}">Editar</button>
        <button class="btn-sm btn-outline" data-senha="${u.id}">Senha</button>
        <button class="btn-sm btn-danger" data-excluir="${u.id}" ${souEu ? 'disabled title="Você não pode excluir o próprio usuário"' : ''}>Excluir</button>
      </div></td></tr>`;
  }).join('');

  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openUserModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-senha]').forEach((b) =>
    b.addEventListener('click', () => openPasswordModal(b.dataset.senha)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteUser(b.dataset.excluir)));
}

function fillDepartmentSelect(selectedId) {
  const sel = document.getElementById('usrDepartamento');
  const ordenados = [...departments].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  sel.innerHTML = '<option value="">— sem departamento —</option>' +
    ordenados.map((d) => `<option value="${d.id}">${escapeHtml(d.name)}</option>`).join('');
  sel.value = selectedId || '';
}

function updatePermHint() {
  const papel = document.getElementById('usrPapel').value;
  const depId = document.getElementById('usrDepartamento').value;
  const box = document.getElementById('usrPermInfo');
  if (papel === 'superadmin') {
    box.innerHTML = 'Superadministrador tem <b>acesso total</b> e valida as requisições — o grupo de ' +
      'permissão do departamento não se aplica a ele.';
    return;
  }
  if (!depId) {
    box.innerHTML = 'Sem departamento, este usuário <b>não herda permissão nenhuma</b> e verá o sistema ' +
      'vazio. Vincule-o a um setor para que ele receba as permissões do grupo daquele setor.';
    return;
  }
  box.innerHTML = `Este usuário herdará as permissões do grupo vinculado a <b>${escapeHtml(deptName(depId))}</b> ` +
    '(configurável na tela de Permissões).';
}

function openUserModal(id) {
  const editando = !!id;
  document.getElementById('modalUsuarioTitulo').textContent = editando ? 'Editar usuário' : 'Novo usuário';
  document.getElementById('usrId').value = id || '';
  document.getElementById('usrSenhaBox').style.display = editando ? 'none' : '';
  document.getElementById('usrSenha').value = '';

  const u = editando ? users.find((x) => x.id === id) : null;
  document.getElementById('usrNome').value = u ? u.name : '';
  document.getElementById('usrEmail').value = u ? u.email : '';
  document.getElementById('usrPapel').value = u ? u.role : 'usuario';
  document.getElementById('usrAtivo').checked = u ? !!u.active : true;
  fillDepartmentSelect(u ? u.departmentId : '');
  updatePermHint();
  openModal('modalUsuario');
}

async function saveUser() {
  const id = document.getElementById('usrId').value;
  const user = {
    id: id || uid('usr_'),
    name: document.getElementById('usrNome').value.trim(),
    email: document.getElementById('usrEmail').value.trim(),
    role: document.getElementById('usrPapel').value,
    departmentId: document.getElementById('usrDepartamento').value,
    active: document.getElementById('usrAtivo').checked,
    createdAt: id ? '' : nowIso(),
    password: id ? '' : document.getElementById('usrSenha').value,
  };

  if (!user.name || !user.email) { toast('Preencha nome e e-mail.', 'error'); return; }
  if (!id && user.password.length < 8) { toast('A senha precisa ter pelo menos 8 caracteres.', 'error'); return; }

  try {
    if (id) await api.updateUser(user);
    else await api.createUser(user);
    await reload();
    closeModal('modalUsuario');
    toast('Usuário salvo.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

function openPasswordModal(id) {
  const u = users.find((x) => x.id === id);
  if (!u) return;
  document.getElementById('senhaUsrId').value = id;
  document.getElementById('senhaUsrResumo').innerHTML =
    `Nova senha para <b>${escapeHtml(u.name)}</b> (${escapeHtml(u.email)}).`;
  document.getElementById('senhaUsrNova').value = '';
  document.getElementById('senhaUsrRepetir').value = '';
  openModal('modalSenhaUsuario');
}

async function saveUserPassword() {
  const id = document.getElementById('senhaUsrId').value;
  const nova = document.getElementById('senhaUsrNova').value;
  const repetir = document.getElementById('senhaUsrRepetir').value;
  if (nova.length < 8) { toast('A senha precisa ter pelo menos 8 caracteres.', 'error'); return; }
  if (nova !== repetir) { toast('As duas senhas não conferem.', 'error'); return; }
  try {
    await api.resetUserPassword(id, nova);
    closeModal('modalSenhaUsuario');
    toast('Senha redefinida.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteUser(id) {
  const u = users.find((x) => x.id === id);
  if (!u) return;
  if (!confirm(`Excluir o usuário "${u.name}"? As requisições feitas por ele permanecem no histórico ` +
               'com o nome preservado.')) return;
  try {
    await api.deleteUser(id);
    await reload();
    toast('Usuário excluído.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
