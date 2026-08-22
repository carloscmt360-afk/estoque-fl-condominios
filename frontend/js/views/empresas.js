import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';

// Cadastro de empresas (fornecedores e prestadores de serviços).
//
// A especialidade é escolhida CLICANDO num chip, e não num <select multiple>:
// o usuário marca quantas quiser, de setores diferentes, e vê o que já marcou
// sem abrir nada. Os chips saem da Setorização — esta tela nunca inventa uma
// especialidade, só oferece as que existem lá.

let empresas = [];
let setores = [];
let selecionadas = new Set();
let filtroTexto = '';
let wired = false;

export async function initEmpresas() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovaEmpresa').addEventListener('click', () => openEmpresaModal());
    document.getElementById('btnSalvarEmpresa').addEventListener('click', saveEmpresa);
    document.getElementById('empFiltro').addEventListener('input', (e) => {
      filtroTexto = e.target.value;
      render();
    });
    enableRowSelection(document.getElementById('empresasTbody'));
  }
  await reload();
}

export async function reload() {
  const [lista, setorizacao] = await Promise.all([api.listEmpresas(), api.listSetorizacao()]);
  empresas = lista;
  setores = setorizacao.setores || [];
  document.getElementById('btnNovaEmpresa').style.display = can('empresas', 'create') ? '' : 'none';
  render();
}

/* Busca sem acento e sem caixa: quem digita "impermeabilizacao" tem que achar
   "Impermeabilização". ̀-ͯ é a faixa dos diacríticos que o NFD
   separa da letra base. */
function normaliza(s) {
  return String(s || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
}

function render() {
  const tbody = document.getElementById('empresasTbody');
  const termo = normaliza(filtroTexto);
  const lista = empresas.filter((e) => {
    if (!termo) return true;
    // Busca por nome, CNPJ, cidade e pelas especialidades — quem procura
    // "piscina" quer a empresa daquele nicho, não só quem tem "piscina" no nome.
    const alvo = [e.nome, e.nomeFantasia, e.cnpj, e.cidade, e.estado,
                  ...e.especialidades.map((x) => x.nome)].join(' ');
    return normaliza(alvo).includes(termo);
  });

  document.getElementById('empresasTotal').textContent = empresas.length;
  document.getElementById('empresasFiltradas').textContent =
    termo ? ` · ${lista.length} no filtro` : '';

  if (!lista.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="5">${
      empresas.length ? 'Nenhuma empresa encontrada para esta busca.'
                      : 'Nenhuma empresa cadastrada ainda.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = lista.map((e) => {
    const acoes = [
      can('empresas', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${e.id}">Editar</button>` : '',
      can('empresas', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${e.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    const esp = e.especialidades.length
      ? `<div class="chip-row">${e.especialidades.map((x) =>
          `<span class="chip-esp" title="${escapeHtml(x.setorLabel)}">${escapeHtml(x.nome)}</span>`).join('')}</div>`
      : '<span class="muted">nenhuma</span>';
    const contato = [
      e.telefone ? escapeHtml(e.telefone) : '',
      e.emails ? `<span class="muted">${escapeHtml(e.emails)}</span>` : '',
    ].filter(Boolean).join('<br/>') || '<span class="muted">—</span>';
    const local = [e.cidade, e.estado].filter(Boolean).join(' / ');
    return `<tr>
      <td><b>${escapeHtml(e.nome)}</b>${e.parceira ? ' <span class="pill pill-ok">Parceira</span>' : ''}${
        e.nomeFantasia ? `<br/><span class="muted">${escapeHtml(e.nomeFantasia)}</span>` : ''}${
        e.cnpj ? `<br/><span class="muted">${escapeHtml(e.cnpj)}</span>` : ''}</td>
      <td>${esp}</td>
      <td>${contato}</td>
      <td>${local ? escapeHtml(local) : '<span class="muted">—</span>'}</td>
      <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openEmpresaModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteEmpresa(b.dataset.excluir)));
}

/* Os chips de especialidade, agrupados por setor. Clicar alterna a marcação —
   sem limite de quantas. */
function renderChips() {
  const host = document.getElementById('empEspecialidades');
  const temAlguma = setores.some((s) => s.especialidades.length);
  if (!temAlguma) {
    host.innerHTML = `<div class="info-box">Nenhuma especialidade cadastrada ainda. Cadastre-as em
      <b>Setorização</b> e elas aparecerão aqui para marcar.</div>`;
    return;
  }
  host.innerHTML = setores.filter((s) => s.especialidades.length).map((s) => `
    <div class="esp-grupo">
      <div class="esp-grupo-titulo">${escapeHtml(s.label)}</div>
      <div class="chip-row">
        ${s.especialidades.map((e) => `
          <button type="button" class="chip-toggle ${selecionadas.has(e.id) ? 'on' : ''}"
                  data-esp="${e.id}" aria-pressed="${selecionadas.has(e.id)}">
            ${escapeHtml(e.nome)}
          </button>`).join('')}
      </div>
    </div>`).join('');

  host.querySelectorAll('[data-esp]').forEach((b) => b.addEventListener('click', () => {
    const id = b.dataset.esp;
    if (selecionadas.has(id)) selecionadas.delete(id);
    else selecionadas.add(id);
    renderChips();
    atualizaContagem();
  }));
}

function atualizaContagem() {
  const n = selecionadas.size;
  document.getElementById('empEspContagem').textContent =
    n === 0 ? 'nenhuma marcada' : `${n} marcada${n > 1 ? 's' : ''}`;
}

function openEmpresaModal(id) {
  const e = id ? empresas.find((x) => x.id === id) : null;
  document.getElementById('modalEmpresaTitulo').textContent = id ? 'Editar Empresa' : 'Nova Empresa';
  document.getElementById('empId').value = id || '';
  document.getElementById('empNome').value = e ? e.nome : '';
  document.getElementById('empNomeFantasia').value = e ? e.nomeFantasia : '';
  document.getElementById('empCnpj').value = e ? e.cnpj : '';
  document.getElementById('empEndereco').value = e ? e.endereco : '';
  document.getElementById('empNumero').value = e ? e.numero : '';
  document.getElementById('empComplemento').value = e ? e.complemento : '';
  document.getElementById('empBairro').value = e ? e.bairro : '';
  document.getElementById('empCep').value = e ? e.cep : '';
  document.getElementById('empCidade').value = e ? e.cidade : '';
  document.getElementById('empEstado').value = e ? e.estado : '';
  document.getElementById('empTelefone').value = e ? e.telefone : '';
  document.getElementById('empEmails').value = e ? e.emails : '';
  document.getElementById('empObservacoes').value = e ? e.observacoes : '';
  // Padrão "Não" no cadastro novo: parceira é uma resposta afirmativa, não algo
  // que a empresa vira por esquecimento.
  document.getElementById('empParceiraSim').checked = !!(e && e.parceira);
  document.getElementById('empParceiraNao').checked = !(e && e.parceira);

  selecionadas = new Set(e ? e.especialidades.map((x) => x.id) : []);
  renderChips();
  atualizaContagem();
  openModal('modalEmpresa');
}

async function saveEmpresa() {
  const id = document.getElementById('empId').value;
  const nome = document.getElementById('empNome').value.trim();
  if (!nome) { toast('Informe o nome da empresa.', 'error'); return; }

  const payload = {
    id: id || uid('emp_'),
    nome,
    nomeFantasia: document.getElementById('empNomeFantasia').value.trim(),
    cnpj: document.getElementById('empCnpj').value.trim(),
    endereco: document.getElementById('empEndereco').value.trim(),
    numero: document.getElementById('empNumero').value.trim(),
    complemento: document.getElementById('empComplemento').value.trim(),
    bairro: document.getElementById('empBairro').value.trim(),
    cep: document.getElementById('empCep').value.trim(),
    cidade: document.getElementById('empCidade').value.trim(),
    estado: document.getElementById('empEstado').value.trim(),
    telefone: document.getElementById('empTelefone').value.trim(),
    emails: document.getElementById('empEmails').value.trim(),
    observacoes: document.getElementById('empObservacoes').value.trim(),
    parceira: document.getElementById('empParceiraSim').checked,
    especialidadeIds: [...selecionadas],
    createdAt: id ? '' : nowIso(),
  };
  try {
    if (id) await api.updateEmpresa(payload);
    else await api.createEmpresa(payload);
    await reload();
    closeModal('modalEmpresa');
    toast('Empresa salva.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteEmpresa(id) {
  const e = empresas.find((x) => x.id === id);
  if (!e) return;
  if (!confirm(`Excluir "${e.nome}"? As especialidades marcadas nela serão desvinculadas, mas o catálogo da Setorização continua intacto.`)) return;
  try {
    await api.deleteEmpresa(id);
    await reload();
    toast('Empresa excluída.', 'success');
  } catch (err) { toast('Erro: ' + errorText(err), 'error'); }
}
