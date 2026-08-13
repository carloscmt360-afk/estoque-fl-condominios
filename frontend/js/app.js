import { api, errorText } from './api.js';
import { installOverlayClickToClose, openModal, closeModal } from './components/modal.js';
import { toast } from './components/toast.js';
import { setSession, getSession, currentUser, isSuperadmin, can, roleLabel } from './session.js';
import { initLogin, showLogin, hideLogin } from './views/login.js';
import { initDashboard, renderReport } from './views/dashboard.js';
import { initRetrospect, reload as reloadRetrospect } from './views/retrospect.js';
import { initProducts, reload as reloadProducts } from './views/products.js';
import { initDepartments, reload as reloadDepartments } from './views/departments.js';
import { initMovements, reload as reloadMovements } from './views/movements.js';
import { initImportExport } from './views/importExport.js';
import { initRequests, reload as reloadRequests } from './views/requests.js';
import { initUsers, reload as reloadUsers } from './views/users.js';
import { initPermissions, reload as reloadPermissions } from './views/permissions.js';

// Catálogo de telas e a permissão que cada uma exige. É daqui que sai o menu:
// o usuário só vê o que ele realmente pode abrir. Isso é conveniência — quem
// barra de verdade é o C++, que confere a permissão em cada chamada.
const VIEWS = [
  { id: 'dashboard', label: 'Relatório Mensal', ico: '📊', visivel: () => can('relatorio_mensal', 'read') },
  { id: 'retrospect', label: 'Retrospecto', ico: '📅', visivel: () => can('retrospecto', 'read') },
  { id: 'products', label: 'Produtos', ico: '📦', visivel: () => can('produtos', 'read') },
  { id: 'movements', label: 'Linha do Tempo', ico: '🕒', visivel: () => can('linha_do_tempo', 'read') },
  // Requisições aparece também para quem só pode CRIAR pedido (sem histórico):
  // é a tela onde a criação acontece.
  { id: 'requests', label: 'Requisições', ico: '📝',
    visivel: () => can('requisicoes', 'read') || can('requisicoes', 'create') },
  { id: 'departments', label: 'Departamentos', ico: '🏢', visivel: () => can('departamentos', 'read') },
  { id: 'users', label: 'Usuários', ico: '👤', visivel: () => isSuperadmin() },
  { id: 'permissions', label: 'Permissões', ico: '🔐', visivel: () => isSuperadmin() },
  { id: 'importExport', label: 'Importar / Exportar', ico: '⇄', visivel: () => can('importar_exportar', 'read') },
];

const initialized = new Set();

async function switchView(view) {
  document.querySelectorAll('.nav-item').forEach((b) => b.classList.toggle('active', b.dataset.view === view));
  document.querySelectorAll('.view').forEach((s) => s.classList.remove('active'));
  const secao = document.getElementById('view-' + view);
  if (secao) secao.classList.add('active');

  try {
    if (!initialized.has(view)) {
      initialized.add(view);
      if (view === 'dashboard') await initDashboard();
      else if (view === 'retrospect') await initRetrospect();
      else if (view === 'products') await initProducts();
      else if (view === 'movements') await initMovements();
      else if (view === 'departments') await initDepartments();
      else if (view === 'requests') await initRequests();
      else if (view === 'users') await initUsers();
      else if (view === 'permissions') await initPermissions();
      else if (view === 'importExport') initImportExport();
    } else {
      // views já inicializadas recarregam os dados ao voltar a ficar visíveis
      if (view === 'dashboard') await renderReport();
      else if (view === 'retrospect') await reloadRetrospect();
      else if (view === 'products') await reloadProducts();
      else if (view === 'movements') await reloadMovements();
      else if (view === 'departments') await reloadDepartments();
      else if (view === 'requests') await reloadRequests();
      else if (view === 'users') await reloadUsers();
      else if (view === 'permissions') await reloadPermissions();
      else if (view === 'importExport') initImportExport();
    }
  } catch (e) {
    // Uma view que falha ao carregar não pode derrubar o app inteiro: mostra o
    // motivo e deixa o usuário navegar para outra. Sessão perdida é tratada
    // pelo evento global (ver boot).
    initialized.delete(view);
    toast('Não foi possível carregar esta tela: ' + errorText(e), 'error');
  }
}

function renderSidebar() {
  const permitidas = VIEWS.filter((v) => v.visivel());
  const nav = document.getElementById('sidebarNav');
  nav.innerHTML = permitidas
    .map((v) => `<button class="nav-item" data-view="${v.id}"><span class="ico">${v.ico}</span> ${v.label}</button>`)
    .join('');
  nav.querySelectorAll('.nav-item').forEach((btn) => {
    btn.addEventListener('click', () => switchView(btn.dataset.view));
  });
  return permitidas;
}

function renderUserBox() {
  const u = currentUser();
  if (!u) return;
  const iniciais = u.name.trim().split(/\s+/).slice(0, 2).map((p) => p[0] || '').join('').toUpperCase();
  document.getElementById('userIniciais').textContent = iniciais || '?';
  document.getElementById('userNome').textContent = u.name;
  document.getElementById('userNome').title = u.email;
  document.getElementById('userPapel').textContent =
    roleLabel(u.role) + (u.departmentName ? ' · ' + u.departmentName : '');
}

async function entrarNoApp(sessao) {
  setSession(sessao);
  initialized.clear();
  hideLogin();
  document.getElementById('startupError').style.display = 'none';
  document.getElementById('appShell').style.display = 'grid';
  renderUserBox();
  const permitidas = renderSidebar();
  await switchView(permitidas.length ? permitidas[0].id : 'semAcesso');
}

async function sair() {
  try {
    await api.logout();
  } catch (e) {
    // Falhar ao avisar o backend não pode prender ninguém dentro do app: a
    // tela volta para o login de qualquer jeito.
  }
  encerrarSessao();
}

function encerrarSessao(mensagem) {
  setSession(null);
  initialized.clear();
  document.getElementById('appShell').style.display = 'none';
  showLogin(mensagem);
}

async function trocarSenha() {
  const atual = document.getElementById('senhaAtual').value;
  const nova = document.getElementById('senhaNova').value;
  const repetir = document.getElementById('senhaNovaRepetir').value;
  if (!atual || !nova) { toast('Preencha a senha atual e a nova.', 'error'); return; }
  if (nova.length < 8) { toast('A nova senha precisa ter pelo menos 8 caracteres.', 'error'); return; }
  if (nova !== repetir) { toast('As duas senhas novas não conferem.', 'error'); return; }
  try {
    await api.changeOwnPassword(atual, nova);
    closeModal('modalTrocarSenha');
    toast('Senha alterada.', 'success');
  } catch (e) {
    toast('Erro: ' + errorText(e), 'error');
  }
}

function wireShell() {
  document.getElementById('btnSair').addEventListener('click', sair);
  document.getElementById('btnTrocarSenha').addEventListener('click', () => {
    ['senhaAtual', 'senhaNova', 'senhaNovaRepetir'].forEach((id) => {
      document.getElementById(id).value = '';
    });
    openModal('modalTrocarSenha');
  });
  document.getElementById('btnSalvarTrocaSenha').addEventListener('click', trocarSenha);

  // Qualquer chamada ao backend que volte "[auth]" (app reaberto, usuário
  // desativado, backup restaurado por cima) derruba para o login — uma vez só,
  // aqui, em vez de em cada view.
  //
  // Note que NÃO há um ouvinte de `estoque:dados-alterados` aqui: quem já
  // precisa dele o escuta por conta própria (a Linha do Tempo), e switchView
  // recarrega toda view ao voltar a ela. Um ouvinte genérico que recarregasse
  // a view atual entraria em laço — a própria view de Produtos dispara o
  // evento no fim do seu reload.
  document.addEventListener('estoque:sessao-perdida', () => {
    if (!getSession()) return;
    encerrarSessao('Sua sessão foi encerrada. Entre novamente.');
  });
}

async function boot() {
  try {
    // Fora do Tauri (aberto direto num navegador): instala o mock de
    // desenvolvimento alimentado por fixtures reais, só para revisão visual.
    if (!(window.__TAURI__ && window.__TAURI__.core)) {
      const { installDevMock } = await import('./devMock.js');
      await installDevMock();
    }

    installOverlayClickToClose();
    wireShell();
    initLogin(entrarNoApp);

    try {
      await api.appStatus();
    } catch (err) {
      showStartupError(String(err));
      document.getElementById('btnRetryInit').addEventListener('click', async () => {
        try {
          await api.retryInit();
          location.reload();
        } catch (e2) {
          showStartupError(String(e2));
        }
      });
      return;
    }

    document.getElementById('startupError').style.display = 'none';

    // A sessão vive no backend: reabrir a janela sem ter saído mantém quem
    // estava logado, e fechar o app derruba a sessão (é o comportamento certo
    // para um app de balcão, onde a máquina é compartilhada).
    const sessao = await api.currentSession();
    if (sessao) await entrarNoApp(sessao);
    else showLogin();
  } catch (err) {
    // Qualquer falha inesperada aqui (ex.: mock de desenvolvimento sem
    // fixtures) nunca deve resultar em tela branca silenciosa — sempre
    // mostra algo acionável, mesmo que a mensagem seja genérica.
    showStartupError(String(err && err.stack ? err.stack : err));
  }
}

function showStartupError(detail) {
  document.getElementById('appShell').style.display = 'none';
  document.getElementById('loginScreen').style.display = 'none';
  document.getElementById('startupError').style.display = 'block';
  document.getElementById('startupErrorDetail').textContent = detail;
}

boot();
