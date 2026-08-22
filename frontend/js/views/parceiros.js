import { api, errorText } from '../api.js';
import { escapeHtml } from '../format.js';
import { toast } from '../components/toast.js';
import { printDocument, buildParceirosDoc } from '../print.js';

// Gestão SOS > Parceiros — só as empresas que responderam SIM à pergunta
// "é empresa parceira?" no Cadastro.
//
// Tela de leitura, e o motivo dela ser dentro de SOS dita o formato: num
// chamado emergencial o que se precisa é do telefone, agora. Por isso o
// contato vem grande, e não escondido atrás de um clique.
//
// Não há cadastro próprio de parceiro: a marcação vive na própria empresa (ver
// companies_engine.hpp). Manter uma segunda lista significaria dois telefones
// da mesma empresa divergindo — exatamente o que não pode acontecer aqui.

let parceiros = [];
let setores = [];
let setorFiltro = '';
let busca = '';
let wired = false;

export async function initParceiros() {
  if (!wired) {
    wired = true;
    document.getElementById('parBusca').addEventListener('input', (e) => {
      busca = e.target.value;
      render();
    });
    document.getElementById('btnImprimirParceiros').addEventListener('click', imprimir);
    document.getElementById('btnCompartilharParceiros').addEventListener('click', compartilhar);
  }
  await reload();
}

export async function reload() {
  try {
    const [lista, setorizacao] = await Promise.all([api.listParceiros(), api.listSetorizacao()]);
    parceiros = lista;
    setores = setorizacao.setores || [];
  } catch (e) {
    toast('Erro ao carregar os parceiros: ' + errorText(e), 'error');
    parceiros = [];
    setores = [];
  }
  renderAbas();
  render();
}

function normaliza(s) {
  return String(s || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
}

function renderAbas() {
  const host = document.getElementById('parAbas');
  const todos = `<button class="${setorFiltro === '' ? 'active' : ''}" data-setor="">Todos os setores</button>`;
  host.innerHTML = todos + setores.map((s) => `
    <button class="${s.key === setorFiltro ? 'active' : ''}" data-setor="${s.key}">
      ${escapeHtml(s.label)}
    </button>`).join('');
  host.querySelectorAll('[data-setor]').forEach((b) => b.addEventListener('click', () => {
    setorFiltro = b.dataset.setor;
    renderAbas();
    render();
  }));
}

/* Lista filtrada por setor (ao menos uma especialidade daquele setor) e por
   busca — a MESMA lista alimenta a tela, o Imprimir e o Copiar contatos, para
   nunca haver uma segunda regra de filtro divergindo da primeira. */
function parceirosFiltrados() {
  const termo = normaliza(busca);
  return parceiros.filter((e) => {
    if (setorFiltro && !e.especialidades.some((x) => x.setor === setorFiltro)) return false;
    if (!termo) return true;
    const alvo = [e.nome, e.nomeFantasia, e.cidade, e.estado, e.telefone, e.cnpj,
                  ...e.especialidades.map((x) => x.nome)].join(' ');
    return normaliza(alvo).includes(termo);
  });
}

function renderStats(lista) {
  const comTelefone = lista.filter((e) => e.telefone).length;
  const cidades = new Set(lista.map((e) => e.cidade).filter(Boolean)).size;
  const especialidades = new Set(lista.flatMap((e) => e.especialidades.map((x) => x.id))).size;
  const tiles = [
    { label: 'Parceiros no filtro', value: lista.length },
    { label: 'Com telefone cadastrado', value: comTelefone,
      cls: lista.length - comTelefone > 0 ? 'is-warn' : '' },
    { label: 'Especialidades cobertas', value: especialidades },
    { label: 'Cidades atendidas', value: cidades },
  ];
  document.getElementById('parStatGrid').innerHTML = tiles.map((t) => `
    <div class="stat-tile ${t.cls || ''}"><div class="label">${escapeHtml(t.label)}</div>
      <div class="value">${t.value}</div></div>`).join('');
}

function render() {
  const host = document.getElementById('parLista');
  const lista = parceirosFiltrados();
  renderStats(lista);

  document.getElementById('parTotal').textContent = parceiros.length;
  document.getElementById('parResumo').textContent =
    (busca.trim() || setorFiltro) ? ` · ${lista.length} no filtro` : '';

  if (!lista.length) {
    host.innerHTML = `<div class="panel modulo-vazio">
      <div class="modulo-vazio-ico">🤝</div>
      <h2>${parceiros.length ? 'Nenhum parceiro neste filtro' : 'Nenhuma empresa parceira ainda'}</h2>
      <p>${parceiros.length
        ? 'Tente outro termo ou troque o setor.'
        : 'Uma empresa entra nesta lista quando o campo <b>“É empresa parceira?”</b> é marcado como <b>Sim</b> em <b>Fornecedores e Prestadores de Serviços › Cadastro</b>.'}</p>
    </div>`;
    return;
  }

  host.innerHTML = `<div class="parceiros-grid">${lista.map((e) => {
    const local = [e.cidade, e.estado].filter(Boolean).join(' / ');
    // Os e-mails vêm num campo só, separados por vírgula (ver o modelo) —
    // aqui cada um vira uma linha, que é como se lê um contato.
    const emails = String(e.emails || '').split(',').map((x) => x.trim()).filter(Boolean);
    return `<div class="parceiro-card">
      <div class="parceiro-nome">${escapeHtml(e.nome)}</div>
      ${e.nomeFantasia ? `<div class="muted parceiro-local">${escapeHtml(e.nomeFantasia)}</div>` : ''}
      ${local ? `<div class="muted parceiro-local">${escapeHtml(local)}</div>` : ''}
      ${e.telefone ? `<div class="parceiro-tel">📞 ${escapeHtml(e.telefone)}</div>`
                   : '<div class="parceiro-tel muted">sem telefone cadastrado</div>'}
      ${emails.length ? `<div class="parceiro-emails">${emails.map((m) =>
        `<div>✉ ${escapeHtml(m)}</div>`).join('')}</div>` : ''}
      ${e.especialidades.length
        ? `<div class="chip-row">${e.especialidades.map((x) =>
            `<span class="chip-esp" title="${escapeHtml(x.setorLabel)}">${escapeHtml(x.nome)}</span>`).join('')}</div>`
        : '<div class="muted" style="font-size:11px;">nenhuma especialidade marcada</div>'}
      ${e.observacoes ? `<div class="parceiro-obs">${escapeHtml(e.observacoes)}</div>` : ''}
    </div>`;
  }).join('')}</div>`;
}

function filtroLabel() {
  const setorTxt = setorFiltro ? (setores.find((s) => s.key === setorFiltro) || {}).label : 'Todos os setores';
  return busca.trim() ? `${setorTxt} · busca "${busca.trim()}"` : setorTxt;
}

function imprimir() {
  const lista = parceirosFiltrados();
  if (!lista.length) { toast('Nada para imprimir com este filtro.', 'error'); return; }
  printDocument(buildParceirosDoc(lista, filtroLabel()));
}

/* "Compartilhar" num app desktop não tem folha de compartilhamento do
   sistema operacional — o que serve de verdade é colocar um texto pronto na
   área de transferência, para colar direto no WhatsApp/e-mail/onde for. */
async function compartilhar() {
  const lista = parceirosFiltrados();
  if (!lista.length) { toast('Nada para copiar com este filtro.', 'error'); return; }

  const linhas = [`Parceiros — ${filtroLabel()}`, ''];
  for (const e of lista) {
    const nomeCompleto = e.nomeFantasia ? `${e.nome} (${e.nomeFantasia})` : e.nome;
    const local = [e.cidade, e.estado].filter(Boolean).join('/');
    linhas.push(`${nomeCompleto}${local ? ` — ${local}` : ''}`);
    if (e.especialidades.length) linhas.push(`  ${e.especialidades.map((x) => x.nome).join(', ')}`);
    if (e.telefone) linhas.push(`  Tel.: ${e.telefone}`);
    if (e.emails) linhas.push(`  E-mail: ${e.emails}`);
    linhas.push('');
  }

  const texto = linhas.join('\n').trim();
  try {
    await navigator.clipboard.writeText(texto);
    toast('Contatos copiados — cole onde quiser compartilhar.', 'success');
  } catch (e) {
    toast('Não foi possível copiar automaticamente: ' + errorText(e), 'error');
  }
}
