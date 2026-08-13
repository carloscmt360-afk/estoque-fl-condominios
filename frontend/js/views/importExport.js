import { api } from '../api.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';

let wired = false;

export function initImportExport() {
  if (!wired) {
    wired = true;
    document.getElementById('btnExportarBackup').addEventListener('click', exportBackup);
    document.getElementById('btnImportarBackup').addEventListener('click', () => document.getElementById('importFile').click());
    document.getElementById('importFile').addEventListener('change', importBackup);
  }
  // Exportar e importar são direitos distintos: importar SUBSTITUI tudo, então
  // não pode vir de brinde para quem só precisa tirar uma cópia.
  document.getElementById('btnExportarBackup').style.display = can('importar_exportar', 'create') ? '' : 'none';
  document.getElementById('btnImportarBackup').style.display = can('importar_exportar', 'update') ? '' : 'none';
}

function downloadFile(filename, content, mime) {
  const blob = new Blob([content], { type: mime });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = filename;
  document.body.appendChild(a); a.click(); document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

async function exportBackup() {
  try {
    const data = await api.backup();
    const stamp = new Date().toISOString().slice(0, 10);
    downloadFile(`backup_estoque_fl_${stamp}.json`, JSON.stringify(data, null, 2), 'application/json');
    toast('Backup exportado.', 'success');
  } catch (e) { toast('Erro ao exportar: ' + e, 'error'); }
}

async function importBackup(event) {
  const file = event.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = async (e) => {
    try {
      const payload = e.target.result;
      const parsed = JSON.parse(payload);
      if (!parsed.products || !parsed.movements) throw new Error('Formato inválido');
      if (!confirm('Importar este backup vai SUBSTITUIR todos os dados atuais. Continuar?')) return;
      await api.restoreBackup(payload);
      toast('Backup importado com sucesso. Recarregue as demais telas.', 'success');
    } catch (err) {
      toast('Arquivo de backup inválido: ' + err, 'error');
    }
  };
  reader.readAsText(file);
  event.target.value = '';
}
