import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtBRL, fmtDateTimeBR } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can, isSuperadmin } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';

// Compras > Orçamentos — fluxo de cotação: uma Ordem (ligada a um
// condomínio) recebe N Propostas (uma por empresa solicitada). Ver
// core-cpp/include/estoque/purchases_engine.hpp para o modelo completo
// (status/e-mail/declínio automático em 25 dias) — este módulo só espelha
// o que o backend já decide; nenhuma regra de status mora aqui.

const STATUS_LABEL = {
  pendente: 'Pendente', solicitado: 'Solicitado', enviado_cliente: 'Enviado ao cliente',
  aprovado: 'Aprovado', declinado: 'Declinado',
};
const STATUS_PILL = {
  pendente: 'pill-ord-pendente', solicitado: 'pill-ord-solicitado', enviado_cliente: 'pill-ord-enviado_cliente',
  aprovado: 'pill-ord-aprovado', declinado: 'pill-ord-declinado',
};
const TIPOS_ANEXO_ACEITOS = ['application/pdf', 'image/jpeg', 'image/jpg', 'image/png', 'image/webp'];

let ordens = [];
let condominios = [];
let empresas = [];
let statusFiltro = '';
let empresasSolicSelecionadas = new Set();
let wired = false;

// Id da ordem cuja célula "Recomendada" está em modo de troca (mostrando o
// seletor em vez do selo fixo) — null = nenhuma. Só uma por vez, e o modal de
// detalhe só mostra uma ordem, então isto é suficiente.
let trocandoRecomendadaOrdemId = null;

function statusPill(statusEfetivo) {
  return `<span class="pill ${STATUS_PILL[statusEfetivo] || ''}">${escapeHtml(STATUS_LABEL[statusEfetivo] || statusEfetivo)}</span>`;
}

// Mesmo critério de carregarPropostas (purchases_engine.cpp): sem resposta
// por último; entre as respondidas, do mais caro pro mais barato.
function ordenarPropostas(lista) {
  return [...lista].sort((a, b) => {
    const aSem = a.valor < 0, bSem = b.valor < 0;
    if (aSem !== bSem) return aSem ? 1 : -1;
    if (!aSem && !bSem) return b.valor - a.valor;
    return new Date(a.createdAt) - new Date(b.createdAt);
  });
}

export async function initOrcamentos() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovaOrdemOrcamento').addEventListener('click', () => openNovaOrdemModal());
    document.getElementById('btnSalvarOrdemOrcamento').addEventListener('click', criarOrdem);
    document.querySelectorAll('#ordStatusFiltro [data-status]').forEach((b) => b.addEventListener('click', () => {
      statusFiltro = b.dataset.status;
      document.querySelectorAll('#ordStatusFiltro [data-status]').forEach((x) =>
        x.classList.toggle('active', x.dataset.status === statusFiltro));
      render();
    }));
    document.getElementById('btnConfigEmail').addEventListener('click', openConfigEmailModal);
    document.getElementById('btnSalvarConfigEmail').addEventListener('click', salvarConfigEmail);
    document.getElementById('btnTestarConfigEmail').addEventListener('click', testarConfigEmail);
    document.getElementById('btnAbrirOutlookSolicitar').addEventListener('click', abrirOutlookSolicitar);
    document.getElementById('btnAbrirOutlookEnviarCliente').addEventListener('click', abrirOutlookEnviarCliente);
    enableRowSelection(document.getElementById('ordensOrcamentoTbody'));

    document.getElementById('btnSalvarInfoOrdem').addEventListener('click', salvarInfoOrdem);
    document.getElementById('btnReativarOrdem').addEventListener('click', () =>
      reativarOrdem(document.getElementById('detOrdemId').value));
    document.getElementById('btnSolicitarEmpresas').addEventListener('click', openSolicitarEmpresasModal);
    document.getElementById('btnConfirmarSolicitar').addEventListener('click', confirmarSolicitar);
    document.getElementById('solicEmpresaBusca').addEventListener('input', renderSolicEmpresasLista);
    document.getElementById('btnEnviarCliente').addEventListener('click', openEnviarClienteModal);
    document.getElementById('btnConfirmarEnviarCliente').addEventListener('click', confirmarEnviarCliente);
  }
  await reload();
}

export async function reload() {
  try {
    const [ord, cond, emp] = await Promise.all([api.listOrdensOrcamento(), api.listCondominios(), api.listEmpresas()]);
    ordens = ord; condominios = cond; empresas = emp;
  } catch (e) {
    toast('Erro ao carregar os orçamentos: ' + errorText(e), 'error');
    ordens = []; condominios = []; empresas = [];
  }
  document.getElementById('btnNovaOrdemOrcamento').style.display = can('orcamentos', 'create') ? '' : 'none';
  document.getElementById('btnConfigEmail').style.display = isSuperadmin() ? '' : 'none';
  render();
}

// Substitui a ordem local por uma versão atualizada (a maioria das ações
// devolve a ordem inteira já com as propostas ordenadas) e, se o modal de
// detalhe estiver mostrando ela, re-renderiza tudo — sem precisar recarregar
// do banco a cada clique.
function mergeOrdemLocal(ordemAtualizada) {
  const i = ordens.findIndex((o) => o.id === ordemAtualizada.id);
  if (i >= 0) ordens[i] = ordemAtualizada; else ordens.unshift(ordemAtualizada);
  render();
  if (document.getElementById('modalDetalheOrdem').classList.contains('open') &&
      document.getElementById('detOrdemId').value === ordemAtualizada.id) {
    renderDetalheOrdem(ordemAtualizada);
  }
}

// Ações que só devolvem a proposta (não a ordem inteira): acha a ordem dona
// dela, substitui e reordena localmente do mesmo jeito que o backend ordena.
function mergePropostaLocal(propostaAtualizada) {
  const ordem = ordens.find((o) => o.propostas.some((p) => p.id === propostaAtualizada.id));
  if (!ordem) return;
  ordem.propostas = ordenarPropostas(
    ordem.propostas.map((p) => (p.id === propostaAtualizada.id ? propostaAtualizada : p)));
  render();
  if (document.getElementById('modalDetalheOrdem').classList.contains('open') &&
      document.getElementById('detOrdemId').value === ordem.id) {
    renderDetalheOrdem(ordem);
  }
}

function avisarEmailErros(resultado) {
  if (resultado.emailErros && resultado.emailErros.length) {
    toast('Ação salva, mas houve falha ao enviar e-mail: ' + resultado.emailErros.join('; '), 'error');
  }
}

function render() {
  const tbody = document.getElementById('ordensOrcamentoTbody');
  const lista = statusFiltro ? ordens.filter((o) => o.statusEfetivo === statusFiltro) : ordens;
  document.getElementById('ordensOrcamentoTotal').textContent = ordens.length;

  if (!lista.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="6">${
      ordens.length ? 'Nenhuma ordem neste status.' : 'Nenhuma ordem de orçamento lançada ainda.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = [...lista].sort((a, b) => b.numero - a.numero).map((o) => {
    const acoes = [
      `<button class="btn-sm btn-ghost" data-abrir="${o.id}">Abrir</button>`,
      o.statusEfetivo === 'declinado' ? `<button class="btn-sm btn-outline" data-reativar="${o.id}">Reativar</button>` : '',
      can('orcamentos', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${o.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    // "3" sozinho não dizia se as 3 já tinham respondido ou se era só quem
    // foi chamado — agora mostra progresso (quantas de fato mandaram valor).
    const respondidas = o.propostas.filter((p) => p.temResposta).length;
    const propostasTxt = o.propostas.length ? `${respondidas}/${o.propostas.length} respondida(s)` : '—';
    return `<tr>
      <td class="num">#${o.numero}</td>
      <td>${escapeHtml(o.condominioNome)}</td>
      <td>${escapeHtml(o.descricao)}</td>
      <td class="num">${propostasTxt}</td>
      <td>${statusPill(o.statusEfetivo)}</td>
      <td><div class="row-actions">${acoes}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-abrir]').forEach((b) => b.addEventListener('click', () => abrirDetalheOrdem(b.dataset.abrir)));
  tbody.querySelectorAll('[data-reativar]').forEach((b) => b.addEventListener('click', () => reativarOrdem(b.dataset.reativar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) => b.addEventListener('click', () => excluirOrdem(b.dataset.excluir)));
}

// ------------------------------------------------------------- nova ordem

function openNovaOrdemModal() {
  if (!condominios.length) { toast('Cadastre um condomínio primeiro (Cadastro de Condomínios).', 'error'); return; }
  const sel = document.getElementById('ordCondominioId');
  sel.innerHTML = '<option value="">Selecione...</option>' + [...condominios]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((c) => `<option value="${c.id}">${escapeHtml(c.nome)}</option>`).join('');
  document.getElementById('ordDescricao').value = '';
  document.getElementById('ordObservacoes').value = '';
  openModal('modalOrdemOrcamento');
}

async function criarOrdem() {
  const condominioId = document.getElementById('ordCondominioId').value;
  const descricao = document.getElementById('ordDescricao').value.trim();
  if (!condominioId) { toast('Selecione o condomínio.', 'error'); return; }
  if (!descricao) { toast('Informe a descrição do pedido.', 'error'); return; }
  const payload = {
    id: uid('ord_'), condominioId, descricao,
    observacoes: document.getElementById('ordObservacoes').value.trim(),
    createdAt: nowIso(),
  };
  try {
    const criada = await api.createOrdemOrcamento(payload);
    ordens.unshift(criada);
    render();
    closeModal('modalOrdemOrcamento');
    toast('Ordem de orçamento criada.', 'success');
    abrirDetalheOrdem(criada.id);
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function excluirOrdem(id) {
  const o = ordens.find((x) => x.id === id);
  if (!o) return;
  if (!confirm(`Excluir a ordem #${o.numero} (${o.descricao})? Todas as propostas ligadas a ela também serão excluídas.`)) return;
  try {
    await api.deleteOrdemOrcamento(id);
    ordens = ordens.filter((x) => x.id !== id);
    render();
    toast('Ordem excluída.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function reativarOrdem(id) {
  try {
    const atualizada = await api.reativarOrdemOrcamento(id);
    mergeOrdemLocal(atualizada);
    toast('Ordem reativada — a janela de 25 dias recomeça a contar de hoje.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

// ------------------------------------------------------------- detalhe

function abrirDetalheOrdem(id) {
  const o = ordens.find((x) => x.id === id);
  if (!o) return;
  renderDetalheOrdem(o);
  openModal('modalDetalheOrdem');
}

function renderDetalheOrdem(o) {
  document.getElementById('detOrdemId').value = o.id;
  document.getElementById('detOrdemTitulo').textContent = `Ordem #${o.numero}`;
  document.getElementById('detOrdemCondominio').textContent = o.condominioNome;
  document.getElementById('detOrdemStatusPill').innerHTML = statusPill(o.statusEfetivo);
  document.getElementById('detOrdemDescricao').value = o.descricao;
  document.getElementById('detOrdemObservacoes').value = o.observacoes;

  document.getElementById('detOrdemDeclinadoAviso').style.display = o.statusEfetivo === 'declinado' ? '' : 'none';

  // Chamar de novo é sempre um reenvio de verdade (regrava dataEnvioCliente e
  // manda o e-mail outra vez) — o rótulo avisa isso em vez de parecer a
  // primeira vez toda hora.
  document.getElementById('btnEnviarCliente').textContent =
    o.dataEnvioCliente ? '🔁 Reenviar para o cliente' : '✉ Enviar para o cliente';

  document.getElementById('detOrdemAndamento').innerHTML = andamentoOrdemHtml(o);
  renderPropostasTable(o);
}

// Data/hora de cada movimentação da ordem — todas já vêm do backend
// (createdAt/dataSolicitacao/dataEnvioCliente/dataAprovacao/reabertoEm em
// OrdemOrcamento), esta função só ordena cronologicamente e monta a linha
// do tempo. reabertoEm pode cair depois de qualquer uma das outras datas
// (reabrir uma ordem declinada não tem posição fixa no fluxo), por isso a
// ordenação é sempre pelo valor real, nunca pela ordem "esperada" do fluxo.
function andamentoOrdemHtml(o) {
  const eventos = [{ marca: '●', texto: 'Ordem criada', ts: o.createdAt }];
  if (o.dataSolicitacao) eventos.push({ marca: '●', texto: 'Solicitação enviada às empresas', ts: o.dataSolicitacao });
  if (o.dataEnvioCliente) eventos.push({ marca: '●', texto: 'Propostas enviadas ao cliente', ts: o.dataEnvioCliente });
  if (o.reabertoEm) eventos.push({ marca: '↺', texto: 'Reaberta — janela de resposta reiniciada', ts: o.reabertoEm });
  if (o.dataAprovacao) eventos.push({ marca: '✓', texto: 'Proposta aprovada', ts: o.dataAprovacao });
  eventos.sort((a, b) => new Date(a.ts) - new Date(b.ts));

  return `<div class="req-track">${eventos.map((e) => `<div class="step">
    <span class="marca">${e.marca}</span><span>${escapeHtml(e.texto)} · ${escapeHtml(fmtDateTimeBR(e.ts))}</span>
  </div>`).join('')}</div>`;
}

async function salvarInfoOrdem() {
  const id = document.getElementById('detOrdemId').value;
  const descricao = document.getElementById('detOrdemDescricao').value.trim();
  if (!descricao) { toast('Informe a descrição do pedido.', 'error'); return; }
  const payload = { id, descricao, observacoes: document.getElementById('detOrdemObservacoes').value.trim() };
  try {
    const atualizada = await api.updateOrdemOrcamentoInfo(payload);
    mergeOrdemLocal(atualizada);
    toast('Informações salvas.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

function fileToBase64(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result).split(',')[1] || '');
    reader.onerror = () => reject(new Error('não foi possível ler o arquivo'));
    reader.readAsDataURL(file);
  });
}

// A recomendação nunca fica "presa": quem decide pode trocar para outra
// empresa ou voltar a "nenhuma" a qualquer momento — daí o seletor em vez de
// um selo mudo. Só entra em modo de troca a proposta que já está marcada;
// nas demais o botão "Marcar" direto já resolve (não precisa de dois passos).
function recomendadaCelHtml(o, p) {
  if (p.recomendada && trocandoRecomendadaOrdemId === o.id) {
    const opcoes = ['<option value="">— nenhuma (desmarcar) —</option>']
      .concat(o.propostas.filter((x) => x.temResposta).map((x) =>
        `<option value="${x.id}" ${x.id === p.id ? 'selected' : ''}>${escapeHtml(x.empresaNome)}</option>`))
      .join('');
    return `<select data-trocar-recomendada-select="${o.id}" style="max-width:170px;">${opcoes}</select>
      <button class="btn-sm btn-ghost" data-trocar-recomendada-cancelar="${o.id}" title="Cancelar">✕</button>`;
  }
  if (p.recomendada) {
    return `<span class="pill pill-ok">★ Recomendada</span>
      <button class="btn-sm btn-ghost" data-trocar-recomendada="${o.id}" title="Mudar ou desmarcar">Trocar</button>`;
  }
  return p.temResposta ? `<button class="btn-sm btn-outline" data-recomendar="${p.id}">Marcar</button>` : '<span class="muted">—</span>';
}

function renderPropostasTable(o) {
  const tbody = document.getElementById('detPropostasTbody');
  if (!o.propostas.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="5">Nenhuma empresa solicitada ainda.</td></tr>';
    return;
  }
  // Um erro aqui dentro (dado inesperado numa proposta) NÃO pode deixar a
  // tabela em branco silenciosamente — isso parece "a ordem não tem
  // proposta nenhuma" quando na verdade tem, só que travou no meio do
  // render. Melhor mostrar o erro e logar no console pra dar pra investigar.
  try {
    renderPropostasTableImpl(o, tbody);
  } catch (e) {
    console.error('Falha ao renderizar propostas da ordem', o.id, e);
    tbody.innerHTML = `<tr class="empty-row"><td colspan="5">Erro ao exibir as propostas (${
      escapeHtml(String(e && e.message || e))}). Feche e abra a ordem de novo; se persistir, avise o suporte.</td></tr>`;
  }
}

function renderPropostasTableImpl(o, tbody) {
  tbody.innerHTML = o.propostas.map((p) => {
    const anexoCel = p.anexoPath
      ? `<div class="anexo-presente">
          <button class="btn-sm btn-outline" data-ver-anexo="${p.id}" data-tipo="${p.anexoTipo}"
            title="Abrir em nova aba">${p.anexoTipo === 'pdf' ? '📄 Ver PDF' : '🖼 Ver imagem'}</button>
          <button class="btn-sm btn-ghost" data-baixar-anexo="${p.id}" title="Baixar para o computador">⬇</button>
          <button class="btn-sm btn-ghost" data-remover-anexo="${p.id}" title="Remover anexo">✕</button>
        </div>`
      : `<div class="anexo-dropzone" data-dropzone="${p.id}">Arraste o PDF aqui<br/>ou clique
          <input type="file" data-file-input="${p.id}" accept="application/pdf,image/*" style="display:none;" />
        </div>`;
    const recomendadaCel = recomendadaCelHtml(o, p);
    const aprovarCel = o.propostaAprovadaId === p.id
      ? '<span class="pill pill-ok">✓ Aprovada</span>'
      : (p.temResposta && !o.propostaAprovadaId
        ? `<button class="btn-sm btn-primary" data-aprovar="${p.id}">Aprovar</button>`
        : '<span class="muted">—</span>');
    return `<tr>
      <td><b>${escapeHtml(p.empresaNome)}</b>${p.emailEnviadoEm ? `<div class="muted" style="font-size:10.5px;">
          solicitado em ${escapeHtml(fmtDateTimeBR(p.emailEnviadoEm))}
          <button class="btn-sm btn-ghost" data-reenviar-solicitacao="${p.id}" title="Reenviar e-mail de solicitação para esta empresa">🔁 Reenviar</button>
        </div>` : ''}</td>
      <td class="num"><input type="number" step="0.01" min="0" style="width:120px;"
        data-valor-proposta="${p.id}" value="${p.valor >= 0 ? p.valor : ''}" placeholder="sem resposta" /></td>
      <td>${anexoCel}</td>
      <td>${recomendadaCel}</td>
      <td>${aprovarCel}</td></tr>`;
  }).join('');

  tbody.querySelectorAll('[data-valor-proposta]').forEach((inp) => {
    inp.addEventListener('change', () => onValorPropostaChange(inp.dataset.valorProposta, inp));
  });
  tbody.querySelectorAll('[data-ver-anexo]').forEach((a) => {
    a.addEventListener('click', () => verAnexoProposta(a.dataset.verAnexo, a.dataset.tipo, o));
  });
  tbody.querySelectorAll('[data-baixar-anexo]').forEach((b) => {
    b.addEventListener('click', () => baixarAnexoProposta(b.dataset.baixarAnexo, o));
  });
  tbody.querySelectorAll('[data-remover-anexo]').forEach((b) => {
    b.addEventListener('click', () => removerAnexoProposta(b.dataset.removerAnexo));
  });
  tbody.querySelectorAll('[data-reenviar-solicitacao]').forEach((b) => {
    b.addEventListener('click', () => reenviarSolicitacao(b.dataset.reenviarSolicitacao));
  });
  tbody.querySelectorAll('[data-recomendar]').forEach((b) => {
    b.addEventListener('click', () => marcarRecomendada(o.id, b.dataset.recomendar));
  });
  tbody.querySelectorAll('[data-trocar-recomendada]').forEach((b) => {
    b.addEventListener('click', () => {
      trocandoRecomendadaOrdemId = b.dataset.trocarRecomendada;
      renderPropostasTable(o);
    });
  });
  tbody.querySelectorAll('[data-trocar-recomendada-cancelar]').forEach((b) => {
    b.addEventListener('click', () => {
      trocandoRecomendadaOrdemId = null;
      renderPropostasTable(o);
    });
  });
  tbody.querySelectorAll('[data-trocar-recomendada-select]').forEach((sel) => {
    sel.addEventListener('change', () => {
      trocandoRecomendadaOrdemId = null;
      if (sel.value) marcarRecomendada(o.id, sel.value);
      else desmarcarRecomendada(o.id);
    });
  });
  tbody.querySelectorAll('[data-aprovar]').forEach((b) => {
    b.addEventListener('click', () => aprovarProposta(o.id, b.dataset.aprovar));
  });
  wireDropzones(tbody, o.id);
}

function wireDropzones(tbody, ordemId) {
  tbody.querySelectorAll('[data-dropzone]').forEach((zone) => {
    const propostaId = zone.dataset.dropzone;
    const input = zone.querySelector('[data-file-input]');
    zone.addEventListener('click', () => input.click());
    input.addEventListener('change', () => {
      const file = input.files && input.files[0];
      if (file) enviarAnexoProposta(ordemId, propostaId, file);
    });
    zone.addEventListener('dragover', (e) => { e.preventDefault(); zone.classList.add('is-dragover'); });
    zone.addEventListener('dragleave', () => zone.classList.remove('is-dragover'));
    zone.addEventListener('drop', (e) => {
      e.preventDefault();
      zone.classList.remove('is-dragover');
      const file = e.dataTransfer.files && e.dataTransfer.files[0];
      if (file) enviarAnexoProposta(ordemId, propostaId, file);
    });
  });
}

async function enviarAnexoProposta(ordemId, propostaId, file) {
  if (!TIPOS_ANEXO_ACEITOS.includes(file.type)) {
    toast('Envie um PDF ou uma imagem (JPG, PNG, WebP).', 'error');
    return;
  }
  try {
    const fileBase64 = await fileToBase64(file);
    const atualizada = await api.uploadPropostaAttachment({ ordemId, propostaId, fileBase64 });
    mergePropostaLocal(atualizada);
    toast('Proposta anexada.', 'success');
  } catch (e) { toast('Erro ao anexar: ' + errorText(e), 'error'); }
}

async function removerAnexoProposta(propostaId) {
  const ordem = ordens.find((o) => o.propostas.some((p) => p.id === propostaId));
  if (!ordem) return;
  try {
    const atualizada = await api.deletePropostaAttachment(ordem.id, propostaId);
    mergePropostaLocal(atualizada);
    toast('Anexo removido.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

function verAnexoProposta(propostaId, tipo, ordem) {
  const proposta = ordem.propostas.find((p) => p.id === propostaId);
  if (!proposta || !proposta.anexoPath) return;
  api.readPropostaAttachment(proposta.anexoPath)
    .then((dataUrl) => window.open(dataUrl, '_blank'))
    .catch(() => toast('Não foi possível abrir o anexo.', 'error'));
}

// Mesmo mecanismo de baixarAnexo em aquisicoes.js: a extensão vem do mime
// real do data: URL, não de anexoTipo — mais confiável que adivinhar.
function baixarAnexoProposta(propostaId, ordem) {
  const proposta = ordem.propostas.find((p) => p.id === propostaId);
  if (!proposta || !proposta.anexoPath) return;
  api.readPropostaAttachment(proposta.anexoPath).then((dataUrl) => {
    const mime = (dataUrl.match(/^data:([^;]+);/) || [])[1] || '';
    const ext = mime.includes('pdf') ? 'pdf' : (mime.split('/')[1] || 'webp');
    const nomeBase = proposta.empresaNome.replace(/[^\w-]+/g, '_');
    const link = document.createElement('a');
    link.href = dataUrl;
    link.download = `proposta-${nomeBase}.${ext}`;
    document.body.appendChild(link);
    link.click();
    link.remove();
  }).catch(() => toast('Não foi possível baixar o anexo.', 'error'));
}

async function onValorPropostaChange(propostaId, input) {
  const valor = parseFloat(input.value);
  if (input.value !== '' && (isNaN(valor) || valor < 0)) {
    toast('Informe um valor válido (ou deixe em branco para "sem resposta").', 'error');
    return;
  }
  try {
    const atualizada = await api.setPropostaValor(propostaId, input.value === '' ? -1 : valor);
    mergePropostaLocal(atualizada);
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function reenviarSolicitacao(propostaId) {
  try {
    const atualizada = await api.reenviarSolicitacaoProposta(propostaId);
    mergeOrdemLocal(atualizada);
    avisarEmailErros(atualizada);
    toast('Solicitação reenviada.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function marcarRecomendada(ordemId, propostaId) {
  try {
    const atualizada = await api.marcarPropostaRecomendada(ordemId, propostaId);
    mergeOrdemLocal(atualizada);
    toast('Proposta marcada como recomendada pela FL.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function desmarcarRecomendada(ordemId) {
  try {
    const atualizada = await api.desmarcarPropostaRecomendada(ordemId);
    mergeOrdemLocal(atualizada);
    toast('Recomendação removida.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function aprovarProposta(ordemId, propostaId) {
  const ordem = ordens.find((o) => o.id === ordemId);
  const proposta = ordem && ordem.propostas.find((p) => p.id === propostaId);
  if (!proposta) return;
  if (!confirm(`Aprovar a proposta de ${proposta.empresaNome} (${fmtBRL(proposta.valor)})? Um e-mail de aprovação será enviado à empresa.`)) return;
  try {
    const atualizada = await api.aprovarPropostaOrcamento(ordemId, propostaId);
    mergeOrdemLocal(atualizada);
    avisarEmailErros(atualizada);
    toast('Proposta aprovada.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

// ------------------------------------------------- solicitar empresas

function especialidadesDaEmpresa(emp) {
  return emp.especialidades && emp.especialidades.length
    ? emp.especialidades.map((e) => e.nome).join(', ')
    : 'Sem categoria';
}

function openSolicitarEmpresasModal() {
  empresasSolicSelecionadas = new Set();
  document.getElementById('solicEmpresaBusca').value = '';
  renderSolicEmpresasLista();
  openModal('modalSolicitarEmpresas');
}

function renderSolicEmpresasLista() {
  const ordemId = document.getElementById('detOrdemId').value;
  const ordem = ordens.find((o) => o.id === ordemId);
  const jaSolicitadas = new Set(ordem ? ordem.propostas.map((p) => p.empresaId) : []);
  const busca = document.getElementById('solicEmpresaBusca').value.trim().toLowerCase();

  // Agrupa por categoria (nome da(s) especialidade(s)) — quem tem mais de
  // uma especialidade aparece em todos os grupos correspondentes, é a mesma
  // lógica do catálogo de Parceiros.
  const grupos = new Map();
  for (const emp of empresas) {
    const cats = emp.especialidades && emp.especialidades.length ? emp.especialidades.map((e) => e.nome) : ['Sem categoria'];
    const bate = !busca || emp.nome.toLowerCase().includes(busca) || cats.some((c) => c.toLowerCase().includes(busca));
    if (!bate) continue;
    for (const cat of cats) {
      if (!grupos.has(cat)) grupos.set(cat, []);
      grupos.get(cat).push(emp);
    }
  }
  const categoriasOrdenadas = [...grupos.keys()].sort((a, b) => a.localeCompare(b, 'pt-BR'));

  const lista = document.getElementById('solicEmpresasLista');
  if (!categoriasOrdenadas.length) {
    lista.innerHTML = '<p class="muted">Nenhuma empresa encontrada.</p>';
  } else {
    lista.innerHTML = categoriasOrdenadas.map((cat) => `
      <div class="section-title">${escapeHtml(cat)}</div>
      ${grupos.get(cat).sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR')).map((emp) => {
        const jaTem = jaSolicitadas.has(emp.id);
        const marcado = jaTem || empresasSolicSelecionadas.has(emp.id);
        return `<label style="display:flex;align-items:center;gap:8px;padding:5px 2px;">
          <input type="checkbox" style="width:auto;" data-empresa-check="${emp.id}"
            ${marcado ? 'checked' : ''} ${jaTem ? 'disabled' : ''} />
          <span>${escapeHtml(emp.nome)}</span>
          ${emp.parceira ? '<span class="pill pill-accent">Parceira</span>' : ''}
          ${jaTem ? '<span class="muted">(já solicitado)</span>' : ''}
        </label>`;
      }).join('')}
    `).join('');
  }
  lista.querySelectorAll('[data-empresa-check]').forEach((cb) => {
    cb.addEventListener('change', () => {
      if (cb.checked) empresasSolicSelecionadas.add(cb.dataset.empresaCheck);
      else empresasSolicSelecionadas.delete(cb.dataset.empresaCheck);
      atualizarResumoSolicitar();
    });
  });
  atualizarResumoSolicitar();
}

function atualizarResumoSolicitar() {
  document.getElementById('solicResumo').textContent = `${empresasSolicSelecionadas.size} selecionada(s)`;
}

// Mesma redação de composeSolicitacaoBody (core-cpp/src/api.cpp), só que em
// texto puro — mailto: não interpreta HTML, então a versão com <p>/<b> viraria
// tag literal no corpo do e-mail.
function abrirOutlookSolicitar() {
  const ordemId = document.getElementById('detOrdemId').value;
  const ordem = ordens.find((o) => o.id === ordemId);
  if (!ordem) return;
  if (!empresasSolicSelecionadas.size) { toast('Selecione ao menos uma empresa.', 'error'); return; }
  const emailsSelecionados = [...empresasSolicSelecionadas]
    .map((id) => empresas.find((e) => e.id === id))
    .filter((e) => e && e.emails)
    .map((e) => e.emails);
  if (!emailsSelecionados.length) {
    toast('Nenhuma das empresas selecionadas tem e-mail cadastrado.', 'error');
    return;
  }
  const linhas = [
    'Olá,', '',
    `A FL Condomínios solicita um orçamento${ordem.condominioNome ? ` para o condomínio ${ordem.condominioNome}` : ''}:`,
    '', `Descrição: ${ordem.descricao}`,
  ];
  if (ordem.observacoes) linhas.push(`Observações: ${ordem.observacoes}`);
  linhas.push('', 'Por favor, envie sua proposta em resposta a este e-mail.', '', 'Atenciosamente,', 'FL Condomínios');
  const subject = `Solicitação de orçamento${ordem.condominioNome ? ` — ${ordem.condominioNome}` : ''}`;
  api.abrirEmailOutlook(emailsSelecionados.join(','), subject, linhas.join('\n'))
    .then(() => toast('Outlook aberto — confira a janela de composição.', 'success'))
    .catch((e) => toast('Erro ao abrir o Outlook: ' + errorText(e), 'error'));
}

async function confirmarSolicitar() {
  const ordemId = document.getElementById('detOrdemId').value;
  if (!empresasSolicSelecionadas.size) { toast('Selecione ao menos uma empresa.', 'error'); return; }
  const lista = [...empresasSolicSelecionadas].map((id) => {
    const emp = empresas.find((e) => e.id === id);
    return { empresaId: id, empresaNome: emp ? emp.nome : '' };
  });
  try {
    const atualizada = await api.solicitarOrcamentoParaEmpresas(ordemId, lista);
    mergeOrdemLocal(atualizada);
    avisarEmailErros(atualizada);
    closeModal('modalSolicitarEmpresas');
    toast('Solicitação enviada.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

// ------------------------------------------------- enviar ao cliente

function openEnviarClienteModal() {
  const ordemId = document.getElementById('detOrdemId').value;
  const ordem = ordens.find((o) => o.id === ordemId);
  if (!ordem) return;
  if (!ordem.propostas.some((p) => p.temResposta)) {
    toast('Nenhuma proposta respondida ainda — aguarde ao menos um valor antes de enviar ao cliente.', 'error');
    return;
  }
  document.getElementById('envClienteEmail').value = ordem.condominioEmail || '';
  document.getElementById('envClienteMensagem').value = '';
  openModal('modalEnviarCliente');
}

async function confirmarEnviarCliente() {
  const ordemId = document.getElementById('detOrdemId').value;
  const destinatarioEmail = document.getElementById('envClienteEmail').value.trim();
  if (!destinatarioEmail) { toast('Informe o e-mail do cliente.', 'error'); return; }
  const mensagemExtra = document.getElementById('envClienteMensagem').value.trim();
  try {
    const atualizada = await api.enviarOrcamentoParaCliente(ordemId, destinatarioEmail, mensagemExtra);
    mergeOrdemLocal(atualizada);
    avisarEmailErros(atualizada);
    closeModal('modalEnviarCliente');
    toast('Propostas enviadas ao cliente.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

// Mesma redação de composeEnvioClienteBody, em texto puro — a tabela HTML
// vira uma listagem simples "Empresa — valor".
function abrirOutlookEnviarCliente() {
  const ordemId = document.getElementById('detOrdemId').value;
  const ordem = ordens.find((o) => o.id === ordemId);
  if (!ordem) return;
  const destinatario = document.getElementById('envClienteEmail').value.trim();
  if (!destinatario) { toast('Informe o e-mail do cliente.', 'error'); return; }
  const mensagemExtra = document.getElementById('envClienteMensagem').value.trim();
  const respondidas = ordem.propostas.filter((p) => p.temResposta);
  if (!respondidas.length) { toast('Nenhuma proposta respondida ainda.', 'error'); return; }

  const linhas = ['Olá,',
    '', `Seguem as propostas recebidas para o pedido ${ordem.descricao}${ordem.condominioNome ? ` (${ordem.condominioNome})` : ''}:`];
  if (mensagemExtra) linhas.push('', mensagemExtra);
  linhas.push('');
  respondidas.forEach((p) => {
    linhas.push(`- ${p.empresaNome}: ${fmtBRL(p.valor)}${p.recomendada ? ' (recomendada pela FL)' : ''}`);
  });
  linhas.push('', 'Anexe aqui o(s) PDF(s) das propostas antes de enviar — o Outlook não recebe anexo pronto.',
    '', 'Atenciosamente,', 'FL Condomínios');
  const subject = `Orçamentos — ${ordem.descricao}${ordem.condominioNome ? ` (${ordem.condominioNome})` : ''}`;
  api.abrirEmailOutlook(destinatario, subject, linhas.join('\n'))
    .then(() => toast('Outlook aberto — anexe o(s) PDF(s) antes de enviar.', 'success'))
    .catch((e) => toast('Erro ao abrir o Outlook: ' + errorText(e), 'error'));
}

// ------------------------------------------------- configuração de e-mail

async function openConfigEmailModal() {
  try {
    const cfg = await api.getEmailConfig();
    document.getElementById('cfgEmailHost').value = cfg.host || '';
    document.getElementById('cfgEmailPort').value = cfg.port || '587';
    document.getElementById('cfgEmailUsername').value = cfg.username || '';
    document.getElementById('cfgEmailPassword').value = '';
    document.getElementById('cfgEmailSenhaAtual').textContent = cfg.temSenha ? '(senha salva)' : '(nenhuma senha salva)';
    document.getElementById('cfgEmailFromEmail').value = cfg.fromEmail || '';
    document.getElementById('cfgEmailFromName').value = cfg.fromName || '';
    document.getElementById('cfgEmailUseTls').checked = !!cfg.useTls;
    document.getElementById('cfgEmailTesteDestino').value = cfg.fromEmail || cfg.username || '';
    openModal('modalConfigEmail');
  } catch (e) { toast('Erro ao carregar a configuração: ' + errorText(e), 'error'); }
}

async function testarConfigEmail() {
  const to = document.getElementById('cfgEmailTesteDestino').value.trim();
  if (!to) { toast('Informe um e-mail para receber o teste.', 'error'); return; }
  const btn = document.getElementById('btnTestarConfigEmail');
  const textoOriginal = btn.textContent;
  btn.disabled = true;
  btn.textContent = 'Enviando...';
  try {
    await api.sendTestEmail(to);
    toast(`E-mail de teste enviado — confira a caixa de entrada (e o spam) de ${to}.`, 'success');
  } catch (e) {
    toast('Falha no teste: ' + errorText(e), 'error');
  } finally {
    btn.disabled = false;
    btn.textContent = textoOriginal;
  }
}

async function salvarConfigEmail() {
  const payload = {
    host: document.getElementById('cfgEmailHost').value.trim(),
    port: document.getElementById('cfgEmailPort').value.trim() || '587',
    username: document.getElementById('cfgEmailUsername').value.trim(),
    fromEmail: document.getElementById('cfgEmailFromEmail').value.trim(),
    fromName: document.getElementById('cfgEmailFromName').value.trim(),
    useTls: document.getElementById('cfgEmailUseTls').checked,
  };
  // Senha em branco = "não mexer" (nunca manda a chave, pra não sobrescrever
  // com vazio — ver Api::setEmailConfig em api.cpp).
  const senha = document.getElementById('cfgEmailPassword').value;
  if (senha) payload.password = senha;
  try {
    await api.setEmailConfig(payload);
    closeModal('modalConfigEmail');
    toast('Configuração de e-mail salva.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
