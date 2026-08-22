import { api, errorText } from '../api.js';
import { toast } from './toast.js';
import { setLogo } from '../print.js';
import { isSuperadmin } from '../session.js';

// Logo da FL — uma foto só, global (não por relatório), no canto esquerdo
// da tela (dentro da barra lateral, ver .sidebar-brand em index.html).
// Aparece sempre na barra superior de todo relatório impresso (print.js:
// head() lê o que setLogo() guardou aqui). Sem redimensionamento no
// frontend: a Rust já grava em ≤300×300 WebP (bridge/src/images.rs).
//
// Só o superadministrador troca ou apaga — os demais só veem a logo já
// definida (mesma regra do resto do sistema: imagens/anexos são
// visualização para quem não tem a permissão de editar o cadastro onde
// vivem; aqui, como a logo não pertence a nenhum cadastro específico, a
// permissão é o próprio papel de superadministrador). O comando Tauri
// upload_app_logo/delete_app_logo confere isto de novo no lado Rust (ver
// assert_pode_editar_logo_fl) — esconder o clique aqui é só não oferecer o
// que o backend recusaria.

let wired = false;

export async function initLogoFl() {
  const podeEditar = isSuperadmin();
  if (!wired) {
    wired = true;
    document.getElementById('logoFlInput').addEventListener('change', onArquivoSelecionado);
    document.getElementById('btnRemoverLogoFl').addEventListener('click', onRemover);
  }
  const preview = document.getElementById('logoFlPreview');
  preview.classList.toggle('sidebar-logo-fl-preview--somente-leitura', !podeEditar);
  preview.onclick = podeEditar ? () => document.getElementById('logoFlInput').click() : null;
  preview.title = podeEditar
    ? 'Clique para definir a logo da FL — aparece nos relatórios impressos'
    : 'Logo da FL — só o superadministrador pode alterar';
  try {
    aplicar(await api.getAppLogo());
  } catch (e) {
    aplicar(null);
  }
}

function aplicar(dataUrl) {
  const preview = document.getElementById('logoFlPreview');
  const btnRemover = document.getElementById('btnRemoverLogoFl');
  const podeEditar = isSuperadmin();
  if (dataUrl) {
    preview.innerHTML = `<img src="${dataUrl}" alt="Logo da FL" />`;
    btnRemover.style.display = podeEditar ? '' : 'none';
  } else {
    preview.innerHTML = '<span class="logo-fl-placeholder">+ Logo FL</span>';
    btnRemover.style.display = 'none';
  }
  setLogo(dataUrl);
}

function fileToBase64(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result).split(',')[1] || '');
    reader.onerror = () => reject(new Error('não foi possível ler o arquivo'));
    reader.readAsDataURL(file);
  });
}

const TIPOS_ACEITOS = ['image/jpeg', 'image/jpg', 'image/png', 'image/webp'];

async function onArquivoSelecionado(e) {
  const file = e.target.files && e.target.files[0];
  e.target.value = ''; // permite escolher o mesmo arquivo de novo mais tarde
  if (!file) return;
  if (!TIPOS_ACEITOS.includes(file.type)) {
    toast('Selecione uma imagem JPG, PNG ou WebP.', 'error');
    return;
  }
  try {
    const base64 = await fileToBase64(file);
    aplicar(await api.uploadAppLogo(base64));
    toast('Logo da FL atualizada.', 'success');
  } catch (err) {
    toast('Erro ao salvar a logo: ' + errorText(err), 'error');
  }
}

async function onRemover() {
  try {
    await api.deleteAppLogo();
    aplicar(null);
    toast('Logo removida.', 'success');
  } catch (err) {
    toast('Erro: ' + errorText(err), 'error');
  }
}
