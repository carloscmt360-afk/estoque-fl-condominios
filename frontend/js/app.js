import { api } from './api.js';
import { installOverlayClickToClose } from './components/modal.js';
import { initDashboard, renderReport } from './views/dashboard.js';
import { initProducts, reload as reloadProducts } from './views/products.js';
import { initDepartments, reload as reloadDepartments } from './views/departments.js';
import { initMovements, reload as reloadMovements } from './views/movements.js';
import { initImportExport } from './views/importExport.js';

const VIEWS = ['dashboard', 'products', 'movements', 'departments', 'importExport'];
const initialized = new Set();

async function switchView(view) {
  document.querySelectorAll('.nav-item').forEach((b) => b.classList.toggle('active', b.dataset.view === view));
  document.querySelectorAll('.view').forEach((s) => s.classList.remove('active'));
  document.getElementById('view-' + view).classList.add('active');

  if (!initialized.has(view)) {
    initialized.add(view);
    if (view === 'dashboard') await initDashboard();
    else if (view === 'products') await initProducts();
    else if (view === 'movements') await initMovements();
    else if (view === 'departments') await initDepartments();
    else if (view === 'importExport') initImportExport();
  } else {
    // views já inicializadas recarregam os dados ao voltar a ficar visíveis
    if (view === 'dashboard') await renderReport();
    else if (view === 'products') await reloadProducts();
    else if (view === 'movements') await reloadMovements();
    else if (view === 'departments') await reloadDepartments();
  }
}

function wireSidebar() {
  document.querySelectorAll('.nav-item').forEach((btn) => {
    btn.addEventListener('click', () => switchView(btn.dataset.view));
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
    wireSidebar();

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
    document.getElementById('appShell').style.display = 'grid';
    await switchView('dashboard');
  } catch (err) {
    // Qualquer falha inesperada aqui (ex.: mock de desenvolvimento sem
    // fixtures) nunca deve resultar em tela branca silenciosa — sempre
    // mostra algo acionável, mesmo que a mensagem seja genérica.
    showStartupError(String(err && err.stack ? err.stack : err));
  }
}

function showStartupError(detail) {
  document.getElementById('appShell').style.display = 'none';
  document.getElementById('startupError').style.display = 'block';
  document.getElementById('startupErrorDetail').textContent = detail;
}

boot();
