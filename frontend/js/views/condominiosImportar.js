import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';

// Importar Condomínios em lote — cola uma lista de nomes, um por linha (o
// caso real de trazer uma leva de condomínios de outro sistema sem redigitar
// um a um). Só o NOME é importado de propósito: os demais campos (CNPJ,
// endereço, localização...) o usuário completa depois editando cada
// condomínio — pedir tudo isso no momento do lote faria a colagem simples
// deixar de servir para o caso comum (uma lista de nomes, só isso).
//
// Nome já cadastrado (comparado sem acento/caixa, mesmo critério de
// casarPorNome em servicosImportar.js) é sinalizado como "já cadastrado" e
// não entra na importação — permite colar a mesma lista de novo (ex.: depois
// de adicionar linhas novas no fim) sem duplicar quem já existe.

let getCondominios = null;  // () => condominios[]
let aoImportar = null;      // chamado (async) depois de importar com sucesso ao menos 1 linha
let linhas = [];
let wired = false;

export function initImportarCondominios(contexto, callbackAoImportar) {
  getCondominios = contexto;
  aoImportar = callbackAoImportar;
  if (wired) return;
  wired = true;
  document.getElementById('btnImportarCondominios').addEventListener('click', abrir);
  document.getElementById('impCondominiosTexto').addEventListener('input', reprocessar);
  document.getElementById('btnImportarCondominiosConfirmar').addEventListener('click', executar);
}

function abrir() {
  document.getElementById('impCondominiosTexto').value = '';
  linhas = [];
  renderPreview();
  openModal('modalImportarCondominios');
}

function normaliza(s) {
  return String(s || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase().trim();
}

function parseLinhas(texto) {
  return texto.split(/\r?\n/).map((l) => l.trim()).filter(Boolean)
    .map((nome, i) => ({ numero: i + 1, nome }));
}

function jaCadastrado(nome) {
  const alvo = normaliza(nome);
  return getCondominios().some((c) => normaliza(c.nome) === alvo);
}

function reprocessar() {
  linhas = parseLinhas(document.getElementById('impCondominiosTexto').value);
  renderPreview();
}

function renderPreview() {
  const tbody = document.getElementById('impCondominiosTbody');
  const resumo = document.getElementById('impCondominiosResumo');
  const btn = document.getElementById('btnImportarCondominiosConfirmar');

  if (!linhas.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="3">Cole os nomes acima para ver a prévia.</td></tr>';
    resumo.textContent = '';
    btn.disabled = true;
    btn.textContent = 'Importar';
    return;
  }

  // Duplicata DENTRO do próprio lote colado (não só contra o que já existe):
  // colar a mesma lista duas vezes num só lote não pode criar dois iguais.
  const vistos = new Set();
  const prontas = linhas.filter((l) => {
    const alvo = normaliza(l.nome);
    if (jaCadastrado(l.nome) || vistos.has(alvo)) return false;
    vistos.add(alvo);
    return true;
  }).length;
  const puladas = linhas.length - prontas;

  resumo.innerHTML = `<b>${prontas}</b> pronto(s) para importar` +
    (puladas ? ` · <span class="flag-critical">${puladas} já cadastrado(s) ou repetido(s)</span>` : '');
  btn.disabled = prontas === 0;
  btn.textContent = `Importar ${prontas} condomínio(s)`;

  const vistosRender = new Set();
  tbody.innerHTML = linhas.map((l) => {
    const alvo = normaliza(l.nome);
    const repetida = vistosRender.has(alvo);
    vistosRender.add(alvo);
    const existe = jaCadastrado(l.nome);
    const situacao = existe
      ? '<span class="pill pill-low">Já cadastrado</span>'
      : repetida
        ? '<span class="pill pill-low">Repetido no lote</span>'
        : '<span class="pill pill-ok">Pronto</span>';
    return `<tr>
      <td class="num">${l.numero}</td>
      <td>${escapeHtml(l.nome)}</td>
      <td>${situacao}</td></tr>`;
  }).join('');
}

async function executar() {
  const vistos = new Set();
  const validas = linhas.filter((l) => {
    const alvo = normaliza(l.nome);
    if (jaCadastrado(l.nome) || vistos.has(alvo)) return false;
    vistos.add(alvo);
    return true;
  });
  if (!validas.length) return;

  const btn = document.getElementById('btnImportarCondominiosConfirmar');
  btn.disabled = true;
  let ok = 0;
  for (const l of validas) {
    btn.textContent = `Importando ${ok + 1}/${validas.length}...`;
    try {
      await api.createCondominio({
        id: uid('cond_'), nome: l.nome, nomeFantasia: '', cnpj: '', codigo: '', cep: '',
        endereco: '', complemento: '', bairro: '', cidade: '', estado: '', localizacao: '',
        sindico: '', telefone: '', observacoes: '', createdAt: nowIso(),
      });
      ok++;
      linhas = linhas.filter((x) => x.numero !== l.numero);  // sai da prévia: já foi
    } catch (e) {
      l.erroSalvar = errorText(e);
    }
  }
  btn.disabled = false;

  const falharam = validas.length - ok;
  if (!falharam) {
    toast(`${ok} condomínio(s) importado(s).`, 'success');
    closeModal('modalImportarCondominios');
  } else {
    toast(`${ok} importado(s), ${falharam} falharam ao salvar — revise a prévia.`, 'error');
    renderPreview();
  }
  if (ok > 0 && aoImportar) await aoImportar();
}
