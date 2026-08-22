import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, paraBusca } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';
import { initImportarCondominios } from './condominiosImportar.js';

// Cadastro de Condomínios — os clientes administrados pela FL. É a lista que
// alimenta o seletor da Gestão de Prazos: um serviço só é vinculado a um
// condomínio que exista aqui.

let condominios = [];
let busca = '';
let wired = false;

export async function initCondominios() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovoCondominio').addEventListener('click', () => openCondominioModal());
    document.getElementById('btnSalvarCondominio').addEventListener('click', saveCondominio);
    document.getElementById('condBusca').addEventListener('input', (e) => {
      busca = paraBusca(e.target.value.trim());
      render();
    });
    enableRowSelection(document.getElementById('condominiosTbody'));
    initImportarCondominios(() => condominios, reload);
  }
  await reload();
}

export async function reload() {
  condominios = await api.listCondominios();
  document.getElementById('btnNovoCondominio').style.display = can('condominios', 'create') ? '' : 'none';
  document.getElementById('btnImportarCondominios').style.display = can('condominios', 'create') ? '' : 'none';
  render();
}

function bateBusca(c) {
  if (!busca) return true;
  return paraBusca([c.nome, c.nomeFantasia, c.codigo, c.sindico, c.endereco, c.numero, c.bairro, c.cidade].join(' ')).includes(busca);
}

function render() {
  const tbody = document.getElementById('condominiosTbody');
  const sorted = [...condominios].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'));
  const filtrados = sorted.filter(bateBusca);

  document.getElementById('condominiosTotal').textContent = sorted.length;
  document.getElementById('condominiosFiltrados').textContent =
    busca ? ` · ${filtrados.length} no filtro` : '';

  if (!filtrados.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="7">${
      sorted.length ? 'Nenhum condomínio encontrado com esse filtro.' : 'Nenhum condomínio cadastrado ainda.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = filtrados.map((c) => {
    const acoes = [
      can('condominios', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${c.id}">Editar</button>` : '',
      can('condominios', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${c.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    const cidadeUf = [c.cidade, c.estado].filter(Boolean).join(' / ');
    return `<tr>
      <td><b>${escapeHtml(c.nome)}</b>${c.nomeFantasia ? `<br/><span class="muted">${escapeHtml(c.nomeFantasia)}</span>` : ''}</td>
      <td>${c.codigo ? escapeHtml(c.codigo) : '<span class="muted">—</span>'}</td>
      <td>${cidadeUf ? escapeHtml(cidadeUf) : '<span class="muted">—</span>'}</td>
      <td>${c.sindico ? escapeHtml(c.sindico) : '<span class="muted">—</span>'}</td>
      <td>${c.telefone ? escapeHtml(c.telefone) : '<span class="muted">—</span>'}</td>
      <td><span class="pill ${c.ativo ? 'pill-ok' : 'pill-low'}">${c.ativo ? 'Sim' : 'Não'}</span></td>
      <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openCondominioModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteCondominio(b.dataset.excluir)));
}

function openCondominioModal(id) {
  document.getElementById('modalCondominioTitulo').textContent = id ? 'Editar Condomínio' : 'Novo Condomínio';
  document.getElementById('condId').value = id || '';
  const c = id ? condominios.find((x) => x.id === id) : null;
  document.getElementById('condNome').value = c ? c.nome : '';
  document.getElementById('condNomeFantasia').value = c ? c.nomeFantasia : '';
  document.getElementById('condCnpj').value = c ? c.cnpj : '';
  document.getElementById('condCodigo').value = c ? c.codigo : '';
  document.getElementById('condCep').value = c ? c.cep : '';
  document.getElementById('condEndereco').value = c ? c.endereco : '';
  document.getElementById('condNumero').value = c ? c.numero : '';
  document.getElementById('condComplemento').value = c ? c.complemento : '';
  document.getElementById('condBairro').value = c ? c.bairro : '';
  document.getElementById('condCidade').value = c ? c.cidade : '';
  document.getElementById('condEstado').value = c ? c.estado : '';
  document.getElementById('condLocalizacao').value = c ? c.localizacao : '';
  document.getElementById('condAtivoSim').checked = !c || c.ativo;
  document.getElementById('condAtivoNao').checked = !!c && !c.ativo;
  document.getElementById('condSindico').value = c ? c.sindico : '';
  document.getElementById('condTelefone').value = c ? c.telefone : '';
  document.getElementById('condEmail').value = c ? c.email : '';
  document.getElementById('condObservacoes').value = c ? c.observacoes : '';
  openModal('modalCondominio');
}

async function saveCondominio() {
  const id = document.getElementById('condId').value;
  const nome = document.getElementById('condNome').value.trim();
  if (!nome) { toast('Informe o nome do condomínio.', 'error'); return; }
  // deltaSindica não tem campo neste modal (a marcação vive na tela Delta
  // Síndicos) — preserva o valor atual do cadastro para não resetar em toda
  // edição comum.
  const atual = id ? condominios.find((x) => x.id === id) : null;
  const payload = {
    id: id || uid('cond_'),
    nome,
    deltaSindica: atual ? !!atual.deltaSindica : false,
    nomeFantasia: document.getElementById('condNomeFantasia').value.trim(),
    cnpj: document.getElementById('condCnpj').value.trim(),
    codigo: document.getElementById('condCodigo').value.trim(),
    cep: document.getElementById('condCep').value.trim(),
    endereco: document.getElementById('condEndereco').value.trim(),
    numero: document.getElementById('condNumero').value.trim(),
    complemento: document.getElementById('condComplemento').value.trim(),
    bairro: document.getElementById('condBairro').value.trim(),
    cidade: document.getElementById('condCidade').value.trim(),
    estado: document.getElementById('condEstado').value.trim().toUpperCase(),
    localizacao: document.getElementById('condLocalizacao').value,
    ativo: document.getElementById('condAtivoSim').checked,
    sindico: document.getElementById('condSindico').value.trim(),
    telefone: document.getElementById('condTelefone').value.trim(),
    email: document.getElementById('condEmail').value.trim(),
    observacoes: document.getElementById('condObservacoes').value.trim(),
    createdAt: id ? '' : nowIso(),
  };
  try {
    if (id) await api.updateCondominio(payload);
    else await api.createCondominio(payload);
    await reload();
    closeModal('modalCondominio');
    toast('Condomínio salvo.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteCondominio(id) {
  const c = condominios.find((x) => x.id === id);
  if (!c) return;
  if (!confirm(`Excluir "${c.nome}"? Os serviços vinculados a ele na Gestão de Prazos e o histórico de renovações também serão excluídos.`)) return;
  try {
    await api.deleteCondominio(id);
    await reload();
    toast('Condomínio excluído.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
