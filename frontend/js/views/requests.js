import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtNum, fmtBRL, fmtDateTimeBR } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can, canValidateRequests, currentUser, isSuperadmin } from '../session.js';
import { printDocument, buildSeparacaoDoc } from '../print.js';
import { enableRowSelection } from '../components/tableTools.js';
import { openFotoAmpliada } from '../components/fotoAmpliada.js';
import { instalarFiltroSelect } from '../components/filtroSelect.js';

// Requisições de material.
//
// A mesma tela serve a dois papéis diferentes, com os mesmos dados:
//  - quem SOLICITA vê o histórico do próprio departamento (autor, data/hora do
//    pedido, da aprovação e da entrega — é o que o cliente pediu);
//  - quem VALIDA (superadmin, ou quem tiver `requisicoes.editar`) vê todos os
//    setores e ganha os botões de aprovar/rejeitar/entregar.
// Quem filtra é o backend: um usuário comum simplesmente não recebe os pedidos
// dos outros setores (ver listRequestsJson em api.cpp).

const STATUS = {
  pendente: { label: 'Pendente', cls: 'pill-pendente' },
  aprovado: { label: 'Aprovada', cls: 'pill-aprovado' },
  entregue: { label: 'Entregue', cls: 'pill-entregue' },
  rejeitado: { label: 'Rejeitada', cls: 'pill-rejeitado' },
  cancelado: { label: 'Cancelada', cls: 'pill-cancelado' },
};

const SITUACAO_JANELA = {
  aberta: { label: 'Aberta', cls: 'pill-aprovado' },
  agendada: { label: 'Agendada', cls: 'pill-pendente' },
  // Prazo já passou (ou foi encerrada antes) mas ainda tem pedido pendente
  // daquele período — precisa de alguém aprovar/rejeitar antes de virar
  // "encerrada"/"concluída" de vez. Ver hasPendingInWindow em api.cpp.
  validacao: { label: 'Em validação', cls: 'pill-pendente' },
  concluida: { label: 'Encerrada no prazo', cls: 'pill-entregue' },
  encerrada: { label: 'Encerrada antes', cls: 'pill-rejeitado' },
  cancelada: { label: 'Cancelada', cls: 'pill-cancelado' },
};

let requests = [];
let availability = [];
let departments = [];
let janelaStatus = { open: false, current: null, next: null, canManage: false };
let janelas = [];
let itensNovaRequisicao = [];
let decisaoPendente = null;  // { acao, id }
let wired = false;

// Edição dos itens de uma requisição já enviada — corrige quantidade errada
// ou acrescenta algo que o solicitante esqueceu de registrar. `null` = modo
// leitura; um array = rascunho em edição (mesmo padrão do "nova requisição").
let requisicaoDetalheAtual = null;
let itensEdicaoRequisicao = null;

export async function initRequests() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovaRequisicao').addEventListener('click', () => openNewRequestModal(false));
    document.getElementById('btnNovaRequisicaoManual').addEventListener('click', () => openNewRequestModal(true));
    document.getElementById('btnAdicionarItem').addEventListener('click', addItem);
    document.getElementById('btnSalvarRequisicao').addEventListener('click', saveRequest);
    document.getElementById('btnNovaJanela').addEventListener('click', openWindowModal);
    document.getElementById('btnSalvarJanela').addEventListener('click', saveWindow);
    document.getElementById('btnImprimirSeparacao').addEventListener('click', imprimirSeparacao);
    document.getElementById('btnConfirmarDecisao').addEventListener('click', confirmDecision);
    document.getElementById('reqProduto').addEventListener('change', updateDisponivelHint);
    document.getElementById('reqQtd').addEventListener('input', updateDisponivelHint);
    document.getElementById('reqProdutoFoto').addEventListener('click', () => {
      const el = document.getElementById('reqProdutoFoto');
      if (el.dataset.thumbPath) openFotoAmpliada(el.dataset.imagePath, el.dataset.nome);
    });
    document.getElementById('filtroRequisicaoBusca').addEventListener('input', render);
    document.querySelectorAll('#filtroRequisicaoStatus button').forEach((b) => {
      b.addEventListener('click', () => {
        document.querySelectorAll('#filtroRequisicaoStatus button').forEach((x) => x.classList.remove('active'));
        b.classList.add('active');
        render();
      });
    });
    enableRowSelection(document.getElementById('requestsTbody'));
    enableRowSelection(document.getElementById('janelasTbody'));
  }
  await reload();
}

export async function reload() {
  const tarefas = [api.listRequests(), api.stockAvailability(), api.requestWindowStatus()];
  // A lista de setores só interessa a quem pode requisitar em nome de outro.
  tarefas.push(isSuperadmin() ? api.listDepartments() : Promise.resolve([]));
  // O histórico de janelas é dado de gestão: quem só requisita não recebe.
  tarefas.push(canValidateRequests() ? api.listRequestWindows() : Promise.resolve([]));
  [requests, availability, janelaStatus, departments, janelas] = await Promise.all(tarefas);

  document.getElementById('requestsSub').textContent = canValidateRequests()
    ? 'Pedidos de todos os departamentos — aprovação e entrega'
    : 'Pedidos do seu departamento';

  // A folha de separação é da área de RECEBIMENTO: só quem valida pedidos
  // enxerga todos os setores, e é essa a lista que vai para o almoxarifado.
  // Sem nada em aberto não há o que separar — o botão some em vez de imprimir
  // uma folha vazia.
  const emAberto = requests.some((r) => r.status === 'pendente' || r.status === 'aprovado');
  document.getElementById('btnImprimirSeparacao').style.display =
    canValidateRequests() && emAberto ? '' : 'none';
  renderJanela();
  renderStatGrid();
  render();
}

/* Folha de separação. Imprime SEMPRE os pedidos em aberto (pendentes e
   aprovados), de propósito ignorando o filtro de situação da tela: separar é
   uma tarefa sobre o que ainda não foi entregue, e uma folha impressa a
   partir do filtro "Entregues" mandaria alguém buscar material que já saiu. */
function imprimirSeparacao() {
  printDocument(buildSeparacaoDoc(requests, availability));
}

// ------------------------------------------------------ janela de pedidos

function renderJanela() {
  const banner = document.getElementById('janelaBanner');
  banner.style.display = '';
  if (janelaStatus.open) {
    banner.className = 'janela-banner aberta';
    banner.innerHTML = `<div class="titulo">● Requisições abertas</div>
      <div class="detalhe">O período vai até <b>${escapeHtml(fmtDateTimeBR(janelaStatus.current.closesAt))}</b>.
      Depois disso o sistema para de aceitar pedidos automaticamente.
      ${janelaStatus.current.obs ? `<br/>${escapeHtml(janelaStatus.current.obs)}` : ''}</div>`;
  } else {
    banner.className = 'janela-banner fechada';
    const quando = janelaStatus.next
      ? `A próxima janela abre em <b>${escapeHtml(fmtDateTimeBR(janelaStatus.next.opensAt))}</b>.`
      : (janelaStatus.canManage
          ? 'Abra uma janela abaixo para voltar a receber pedidos.'
          : 'Aguarde o administrador abrir um novo período de pedidos.');
    banner.innerHTML = `<div class="titulo">● Requisições fechadas</div>
      <div class="detalhe">Nenhum pedido novo pode ser feito agora. ${quando}</div>`;
  }

  // Sem janela aberta o botão some para todo mundo — o backend recusaria de
  // qualquer forma (ver requireOpenRequestWindow), isto evita oferecer o que
  // vai dar erro.
  document.getElementById('btnNovaRequisicao').style.display =
    can('requisicoes', 'create') && janelaStatus.open ? '' : 'none';

  // Exceção: quem valida requisições pode lançar um pedido MESMO com a janela
  // fechada — é o caso de alguém que esqueceu de pedir no prazo e o
  // administrador registra por ele (o backend aceita, ver createRequest em
  // api.cpp). Só aparece quando faz sentido: janela fechada e quem tem esse
  // poder — com a janela aberta o botão de cima já resolve.
  document.getElementById('btnNovaRequisicaoManual').style.display =
    can('requisicoes', 'create') && canValidateRequests() && !janelaStatus.open ? '' : 'none';

  document.getElementById('janelaPainel').style.display = janelaStatus.canManage ? '' : 'none';
  if (janelaStatus.canManage) renderJanelasTable();
}

function renderJanelasTable() {
  const tbody = document.getElementById('janelasTbody');
  if (!janelas.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="6">Nenhuma janela cadastrada — ' +
      'enquanto não houver uma aberta, o sistema não aceita requisições.</td></tr>';
    return;
  }
  tbody.innerHTML = janelas.map((j) => {
    const info = SITUACAO_JANELA[j.situacao] || { label: j.situacao, cls: '' };
    const acoes = [];
    // "Encerrar agora" só existe enquanto há prazo a interromper; "Excluir",
    // só antes de a janela começar (depois disso ela já explica pedidos).
    if (j.situacao === 'aberta') acoes.push(`<button class="btn-sm btn-danger" data-encerrar="${j.id}">Encerrar agora</button>`);
    if (j.situacao === 'agendada') {
      acoes.push(`<button class="btn-sm btn-outline" data-encerrar="${j.id}">Cancelar</button>`);
      acoes.push(`<button class="btn-sm btn-ghost" data-excluir-janela="${j.id}">Excluir</button>`);
    }
    // "Em validação" = o prazo já passou mas sobrou pedido pendente daquele
    // período — atalho direto pro filtro que mostra só eles.
    if (j.situacao === 'validacao') acoes.push('<button class="btn-sm btn-primary" data-ver-pendentes="1">Ver pendentes</button>');
    return `<tr>
      <td>${escapeHtml(fmtDateTimeBR(j.opensAt))}</td>
      <td>${escapeHtml(fmtDateTimeBR(j.closesAt))}
        ${j.closedAt ? `<div class="muted">encerrada em ${escapeHtml(fmtDateTimeBR(j.closedAt))}</div>` : ''}</td>
      <td><span class="pill ${info.cls}">${info.label}</span></td>
      <td>${j.obs ? escapeHtml(j.obs) : '<span class="muted">—</span>'}</td>
      <td>${escapeHtml(j.createdByName || '—')}</td>
      <td><div class="row-actions">${acoes.join('') || '<span class="muted">—</span>'}</div></td>
    </tr>`;
  }).join('');

  tbody.querySelectorAll('[data-encerrar]').forEach((b) =>
    b.addEventListener('click', () => closeWindowNow(b.dataset.encerrar)));
  tbody.querySelectorAll('[data-excluir-janela]').forEach((b) =>
    b.addEventListener('click', () => deleteWindow(b.dataset.excluirJanela)));
  tbody.querySelectorAll('[data-ver-pendentes]').forEach((b) =>
    b.addEventListener('click', () => {
      document.querySelector('#filtroRequisicaoStatus [data-status="pendente"]').click();
      document.getElementById('requestsTbody').scrollIntoView({ behavior: 'smooth', block: 'start' });
    }));
}

function openWindowModal() {
  // Sugestão de período: começa agora, termina no fim do dia seguinte — quem
  // for abrir mensalmente ajusta, mas ninguém precisa digitar do zero.
  const agora = new Date();
  const fim = new Date(agora.getTime() + 24 * 60 * 60 * 1000);
  fim.setHours(23, 59, 0, 0);
  document.getElementById('janelaAbertura').value = paraInputLocal(agora);
  document.getElementById('janelaFechamento').value = paraInputLocal(fim);
  document.getElementById('janelaObs').value = '';
  openModal('modalJanela');
}

// Um <input type="datetime-local"> fala no fuso do computador; o banco só
// guarda UTC. Estas duas funções são o único ponto de tradução.
function paraInputLocal(d) {
  const local = new Date(d.getTime() - d.getTimezoneOffset() * 60000);
  return local.toISOString().slice(0, 16);
}
function inputLocalParaIso(valor) {
  // `new Date('2026-08-15T18:00')` (sem fuso no texto) é interpretado como
  // horário LOCAL pela especificação — é exatamente o que o campo mostrou ao
  // usuário, então toISOString() devolve o UTC correspondente.
  return new Date(valor).toISOString();
}

async function saveWindow() {
  const abertura = document.getElementById('janelaAbertura').value;
  const fechamento = document.getElementById('janelaFechamento').value;
  if (!abertura || !fechamento) { toast('Informe a abertura e o fechamento da janela.', 'error'); return; }

  const botao = document.getElementById('btnSalvarJanela');
  botao.disabled = true;
  try {
    const criada = await api.createRequestWindow({
      id: uid('jan_'),
      opensAt: inputLocalParaIso(abertura),
      closesAt: inputLocalParaIso(fechamento),
      obs: document.getElementById('janelaObs').value.trim(),
    });
    await reload();
    closeModal('modalJanela');
    // Uma janela marcada para depois foi AGENDADA, não aberta — dizer
    // "aberta" faria o usuário achar que já dá para pedir.
    toast(criada.opensAt <= new Date().toISOString()
      ? 'Janela aberta. O sistema já está recebendo pedidos.'
      : `Janela agendada para ${fmtDateTimeBR(criada.opensAt)}.`, 'success');
  } catch (e) {
    toast('Erro: ' + errorText(e), 'error');
  } finally {
    botao.disabled = false;
  }
}

async function closeWindowNow(id) {
  const j = janelas.find((x) => x.id === id);
  const pergunta = j && j.situacao === 'agendada'
    ? 'Cancelar esta janela? Ela não chegará a abrir.'
    : 'Encerrar a janela agora? O sistema para de aceitar pedidos imediatamente.';
  if (!confirm(pergunta)) return;
  try {
    await api.closeRequestWindowNow(id);
    await reload();
    toast(j && j.situacao === 'agendada' ? 'Janela cancelada.' : 'Janela encerrada. Requisições fechadas.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteWindow(id) {
  if (!confirm('Excluir esta janela agendada?')) return;
  try {
    await api.deleteRequestWindow(id);
    await reload();
    toast('Janela excluída.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

function statusInfo(s) {
  return STATUS[s] || { label: s, cls: '' };
}

function codigo(r) {
  return '#' + String(r.id).slice(-6).toUpperCase();
}

// Miniatura do material, pela mesma foto que a tela de Produtos usa — quem
// aprova ou separa o pedido enxerga o produto, não só o nome digitado. `a` é
// o registro de disponibilidade do produto (tem sku/nome/thumbnailPath); sem
// foto cadastrada, a caixa fica vazia (mesmo padrão de Produtos: só quem tem
// data-thumb-path vira link clicável).
function thumbCellHtml(a, extraClass) {
  const cls = `thumb-cell${extraClass ? ' ' + extraClass : ''}`;
  if (!a || !a.thumbnailPath) return `<div class="${cls}"><span class="thumb-placeholder"></span></div>`;
  return `<div class="${cls}" data-thumb-path="${escapeHtml(a.thumbnailPath)}" ` +
    `data-image-path="${escapeHtml(a.imagePath || a.thumbnailPath)}" data-nome="${escapeHtml(a.name)}" ` +
    `title="Clique para ampliar"><span class="thumb-placeholder"></span></div>`;
}

// Carrega (uma vez, sem lazy loading: as listas de requisição são curtas,
// nunca o catálogo inteiro) e liga o clique-para-ampliar de toda miniatura
// dentro de `container`.
function wireThumbs(container) {
  container.querySelectorAll('.thumb-cell[data-thumb-path]').forEach((el) => {
    api.readProductImage(el.dataset.thumbPath)
      .then((dataUrl) => { el.innerHTML = `<img src="${dataUrl}" alt="" loading="lazy" />`; })
      .catch(() => {}); // mantém o placeholder — nunca mostra imagem quebrada
    el.addEventListener('click', () => openFotoAmpliada(el.dataset.imagePath, el.dataset.nome));
  });
}

function valorEstimado(r) {
  // Estimativa pelo custo médio ATUAL: o valor real da baixa só existe depois
  // da entrega (é o custo médio da data). Serve para dar ordem de grandeza a
  // quem aprova, e é rotulado como estimativa na tela.
  return (r.items || []).reduce((s, i) => {
    const a = availability.find((x) => x.productId === i.productId);
    return s + i.qty * (a ? a.avgCost : 0);
  }, 0);
}

function renderStatGrid() {
  const pendentes = requests.filter((r) => r.status === 'pendente').length;
  const aprovadas = requests.filter((r) => r.status === 'aprovado').length;
  const entregues = requests.filter((r) => r.status === 'entregue').length;
  const reservado = availability.reduce((s, a) => s + a.reserved, 0);
  const valorReservado = availability.reduce((s, a) => s + a.reserved * a.avgCost, 0);

  document.getElementById('requestsStatGrid').innerHTML = [
    { label: 'Aguardando aprovação', value: fmtNum(pendentes), cls: pendentes > 0 ? 'is-warn' : 'is-good' },
    { label: 'Aprovadas, a entregar', value: fmtNum(aprovadas) },
    { label: 'Entregues', value: fmtNum(entregues) },
    { label: 'Itens reservados', value: fmtNum(reservado), foot: fmtBRL(valorReservado) + ' estimados' },
  ].map((t) => `<div class="stat-tile ${t.cls || ''}"><div class="label">${escapeHtml(t.label)}</div>
      <div class="value">${t.value}</div>${t.foot ? `<div class="foot">${t.foot}</div>` : ''}</div>`).join('');
}

function trackHtml(r) {
  const linhas = [
    `<div class="step"><span class="marca">●</span><span>Solicitado por <b>${escapeHtml(r.requesterName)}</b>
      · ${escapeHtml(fmtDateTimeBR(r.createdAt))}</span></div>`,
  ];

  if (r.status === 'rejeitado' || r.status === 'cancelado') {
    const verbo = r.status === 'rejeitado' ? 'Rejeitado' : 'Cancelado';
    linhas.push(`<div class="step"><span class="marca">✕</span><span>${verbo} por
      <b>${escapeHtml(r.decidedByName || '—')}</b> · ${escapeHtml(fmtDateTimeBR(r.decidedAt))}</span></div>`);
  } else if (r.decidedAt) {
    linhas.push(`<div class="step"><span class="marca">●</span><span>Aprovado por
      <b>${escapeHtml(r.decidedByName || '—')}</b> · ${escapeHtml(fmtDateTimeBR(r.decidedAt))}</span></div>`);
  } else {
    linhas.push('<div class="step pendente"><span class="marca">○</span><span>Aguardando aprovação</span></div>');
  }

  if (r.deliveredAt) {
    linhas.push(`<div class="step"><span class="marca">●</span><span>Entregue por
      <b>${escapeHtml(r.deliveredByName || '—')}</b> · ${escapeHtml(fmtDateTimeBR(r.deliveredAt))}</span></div>`);
  } else if (r.status === 'aprovado') {
    linhas.push('<div class="step pendente"><span class="marca">○</span><span>Aguardando entrega ' +
      '(material reservado)</span></div>');
  }

  if (r.decisionNote) {
    linhas.push(`<div class="step"><span class="marca">✎</span><span class="muted">${escapeHtml(r.decisionNote)}</span></div>`);
  }
  if (r.editedAt) {
    linhas.push(`<div class="step"><span class="marca">✎</span><span>Itens corrigidos por
      <b>${escapeHtml(r.editedByName || '—')}</b> · ${escapeHtml(fmtDateTimeBR(r.editedAt))}</span></div>`);
  }
  return `<div class="req-track">${linhas.join('')}</div>`;
}

function acoesHtml(r) {
  const botoes = [];
  botoes.push(`<button class="btn-sm btn-ghost" data-detalhe="${r.id}">Ver</button>`);
  if (canValidateRequests() && r.status === 'pendente') {
    botoes.push(`<button class="btn-sm btn-primary" data-aprovar="${r.id}">Aprovar</button>`);
    botoes.push(`<button class="btn-sm btn-danger" data-rejeitar="${r.id}">Rejeitar</button>`);
  }
  if (canValidateRequests() && r.status === 'aprovado') {
    botoes.push(`<button class="btn-sm btn-primary" data-entregar="${r.id}">Confirmar entrega</button>`);
  }
  const emAberto = r.status === 'pendente' || r.status === 'aprovado';
  if (emAberto && (canValidateRequests() || can('requisicoes', 'delete'))) {
    botoes.push(`<button class="btn-sm btn-outline" data-cancelar="${r.id}">Cancelar</button>`);
  }
  return `<div class="row-actions">${botoes.join('')}</div>`;
}

function render() {
  const status = document.querySelector('#filtroRequisicaoStatus button.active').dataset.status;
  const busca = (document.getElementById('filtroRequisicaoBusca').value || '').toLowerCase();

  let lista = [...requests];
  if (status) lista = lista.filter((r) => r.status === status);
  if (busca) {
    lista = lista.filter((r) => {
      const alvo = [r.requesterName, r.departmentName, r.obs, codigo(r),
        ...(r.items || []).map((i) => i.productName)].join(' ').toLowerCase();
      return alvo.includes(busca);
    });
  }

  const tbody = document.getElementById('requestsTbody');
  if (!lista.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="7">${
      requests.length ? 'Nenhuma requisição com esses filtros.'
        : 'Nenhuma requisição registrada ainda.'
    }</td></tr>`;
    document.getElementById('requestsRodape').textContent = '';
    return;
  }

  tbody.innerHTML = lista.map((r) => {
    const itens = r.items || [];
    const resumo = itens.slice(0, 2).map((i) => {
      const a = availability.find((x) => x.productId === i.productId);
      return `<div class="req-item-linha">${thumbCellHtml(a, 'thumb-cell-inline')}<span>${fmtNum(i.qty)}× ${escapeHtml(i.productName)}</span></div>`;
    }).join('');
    const extra = itens.length > 2 ? `<div class="muted">+ ${itens.length - 2} outro(s)</div>` : '';
    const info = statusInfo(r.status);
    return `<tr>
      <td><b>${codigo(r)}</b><div class="muted">${escapeHtml(fmtDateTimeBR(r.createdAt))}</div></td>
      <td>${escapeHtml(r.departmentName)}</td>
      <td>${escapeHtml(r.requesterName)}</td>
      <td>${resumo}${extra}</td>
      <td><span class="pill ${info.cls}">${info.label}</span></td>
      <td>${trackHtml(r)}</td>
      <td>${acoesHtml(r)}</td>
    </tr>`;
  }).join('');

  wireThumbs(tbody);
  tbody.querySelectorAll('[data-detalhe]').forEach((b) =>
    b.addEventListener('click', () => openDetail(b.dataset.detalhe)));
  tbody.querySelectorAll('[data-aprovar]').forEach((b) =>
    b.addEventListener('click', () => openDecision('aprovar', b.dataset.aprovar)));
  tbody.querySelectorAll('[data-rejeitar]').forEach((b) =>
    b.addEventListener('click', () => openDecision('rejeitar', b.dataset.rejeitar)));
  tbody.querySelectorAll('[data-entregar]').forEach((b) =>
    b.addEventListener('click', () => openDecision('entregar', b.dataset.entregar)));
  tbody.querySelectorAll('[data-cancelar]').forEach((b) =>
    b.addEventListener('click', () => openDecision('cancelar', b.dataset.cancelar)));

  const pendentes = lista.filter((r) => r.status === 'pendente').length;
  document.getElementById('requestsRodape').textContent =
    `${lista.length} requisição(ões) exibida(s)${pendentes ? ` · ${pendentes} aguardando aprovação` : ''}.`;
}

// ---------------------------------------------------------------- detalhe

function openDetail(id) {
  const r = requests.find((x) => x.id === id);
  if (!r) return;
  requisicaoDetalheAtual = r;
  itensEdicaoRequisicao = null;
  renderDetail();
  openModal('modalRequisicaoDetalhe');
}

// Só quem valida requisições corrige em nome do solicitante, e só enquanto o
// pedido está em aberto — depois de entregue os itens já viraram saída de
// estoque de verdade, e de rejeitada/cancelada, história encerrada.
function podeEditarItens(r) {
  return canValidateRequests() && (r.status === 'pendente' || r.status === 'aprovado');
}

function renderDetail() {
  const r = requisicaoDetalheAtual;
  if (!r) return;
  const info = statusInfo(r.status);
  const editando = itensEdicaoRequisicao !== null;

  document.getElementById('reqDetalheTitulo').textContent = `Requisição ${codigo(r)}`;
  document.getElementById('reqDetalheCorpo').innerHTML = `
    <div class="info-box"><b>${escapeHtml(r.departmentName)}</b> ·
      <span class="pill ${info.cls}">${info.label}</span>
      ${r.obs ? `<br/>Observação: ${escapeHtml(r.obs)}` : ''}</div>
    <div class="section-title" style="margin-top:0;">Materiais</div>
    ${podeEditarItens(r) && !editando
      ? '<div style="display:flex;justify-content:flex-end;margin:-4px 0 8px;"><button class="btn-sm btn-outline" id="btnEditarItensReq" type="button">✎ Editar itens</button></div>'
      : ''}
    ${editando ? editorItensHtml() : materiaisLeituraHtml(r)}
    <div class="section-title">Andamento</div>
    ${trackHtml(r)}`;

  wireThumbs(document.getElementById('reqDetalheCorpo'));
  if (editando) {
    wireEditorItens();
  } else {
    const btn = document.getElementById('btnEditarItensReq');
    if (btn) btn.addEventListener('click', () => {
      itensEdicaoRequisicao = r.items.map((i) => ({ ...i }));
      renderDetail();
    });
  }
}

function materiaisLeituraHtml(r) {
  const linhas = (r.items || []).map((i) => {
    const a = availability.find((x) => x.productId === i.productId);
    return `<tr><td>${thumbCellHtml(a)}</td><td>${escapeHtml(i.productName)}</td><td class="num">${fmtNum(i.qty)}</td>
      <td>${escapeHtml(i.unit || '')}</td>
      <td class="num">${a ? fmtBRL(i.qty * a.avgCost) : '<span class="muted">—</span>'}</td>
      <td>${i.movementId
        ? '<span class="pill pill-entregue">baixado</span>'
        : (r.status === 'pendente' || r.status === 'aprovado'
            ? '<span class="pill pill-aprovado">reservado</span>'
            : '<span class="muted">—</span>')}</td></tr>`;
  }).join('');
  return `
    <div class="panel"><div class="table-scroll" style="max-height:300px;"><table>
      <thead><tr><th></th><th>Material</th><th class="num">Qtd.</th><th>Unid.</th>
        <th class="num">Valor estimado</th><th>Situação do item</th></tr></thead>
      <tbody>${linhas || '<tr class="empty-row"><td colspan="6">Sem itens.</td></tr>'}</tbody>
    </table></div></div>
    <div class="muted" style="font-size:11px;">Valor estimado pelo custo médio atual; a baixa real é
      valorizada pelo custo médio da data da entrega.</div>`;
}

// ------------------------------------------------------- edição de itens

// Quanto ainda pode ser posto num material durante a edição: desconta o que
// está reservado em OUTRAS requisições (nunca a própria, que está sendo
// regravada) e o que já está no rascunho desta edição — espelha
// reservedQtyExcluding() do C++.
function disponivelEdicao(productId) {
  const a = availability.find((x) => x.productId === productId);
  if (!a) return 0;
  const reservadoPropriaOriginal = (requisicaoDetalheAtual.items || [])
    .filter((i) => i.productId === productId)
    .reduce((s, i) => s + i.qty, 0);
  const reservadoOutros = a.reserved - reservadoPropriaOriginal;
  const jaNoRascunho = itensEdicaoRequisicao
    .filter((i) => i.productId === productId)
    .reduce((s, i) => s + i.qty, 0);
  return a.qty - reservadoOutros - jaNoRascunho;
}

function editorItensHtml() {
  const linhas = itensEdicaoRequisicao.map((i, idx) => {
    const a = availability.find((x) => x.productId === i.productId);
    return `<tr><td>${thumbCellHtml(a)}</td><td>${escapeHtml(i.productName)}</td>
      <td class="num"><input type="number" min="0.0001" step="any" style="width:90px;text-align:right;"
        data-qtd-edicao="${idx}" value="${i.qty}" /> ${escapeHtml(i.unit || '')}</td>
      <td><button class="btn-sm btn-ghost" type="button" data-remover-edicao="${idx}">Remover</button></td></tr>`;
  }).join('');
  const opcoesProduto = availability.map((a) =>
    `<option value="${a.productId}">${escapeHtml(a.name)} (${escapeHtml(a.unit)})</option>`).join('');

  return `
    <div class="panel"><div class="table-scroll" style="max-height:300px;"><table>
      <thead><tr><th></th><th>Material</th><th class="num">Qtd.</th><th></th></tr></thead>
      <tbody id="reqEditItensTbody">${linhas || '<tr class="empty-row"><td colspan="4">Nenhum material — adicione pelo menos um.</td></tr>'}</tbody>
    </table></div></div>
    <div class="field-row" style="align-items:end;margin-top:var(--sp-3);">
      <div class="field"><label>Acrescentar material</label><select id="reqEditProduto">${opcoesProduto}</select></div>
      <div class="field" style="max-width:150px;"><label>Quantidade</label>
        <input type="number" id="reqEditQtd" min="0.0001" step="any" /></div>
      <div class="field" style="max-width:130px;"><label>&nbsp;</label>
        <button class="btn-outline" id="btnEditAdicionarItem" type="button">Adicionar</button></div>
    </div>
    <div class="muted" style="font-size:11px;margin-top:var(--sp-2);">A correção fica registrada no
      andamento do pedido, com seu nome e o horário.</div>
    <div class="row-actions" style="margin-top:var(--sp-3);justify-content:flex-end;">
      <button class="btn-outline" id="btnEditCancelarItens" type="button">Cancelar edição</button>
      <button class="btn-primary" id="btnEditSalvarItens" type="button">Salvar alterações</button>
    </div>`;
}

function wireEditorItens() {
  instalarFiltroSelect('reqEditProduto', 'Digite o nome do material...');
  document.getElementById('btnEditAdicionarItem').addEventListener('click', addItemEdicao);
  document.getElementById('btnEditCancelarItens').addEventListener('click', () => {
    itensEdicaoRequisicao = null;
    renderDetail();
  });
  document.getElementById('btnEditSalvarItens').addEventListener('click', salvarEdicaoItens);
  document.querySelectorAll('#reqEditItensTbody [data-qtd-edicao]').forEach((inp) => {
    inp.addEventListener('input', () => {
      itensEdicaoRequisicao[Number(inp.dataset.qtdEdicao)].qty = parseFloat(inp.value) || 0;
    });
  });
  document.querySelectorAll('#reqEditItensTbody [data-remover-edicao]').forEach((b) => {
    b.addEventListener('click', () => {
      itensEdicaoRequisicao.splice(Number(b.dataset.removerEdicao), 1);
      renderDetail();
    });
  });
}

function addItemEdicao() {
  const productId = document.getElementById('reqEditProduto').value;
  const qty = parseFloat(document.getElementById('reqEditQtd').value);
  const a = availability.find((x) => x.productId === productId);
  if (!a) { toast('Selecione um material.', 'error'); return; }
  if (!qty || qty <= 0) { toast('Informe uma quantidade maior que zero.', 'error'); return; }
  if (qty > disponivelEdicao(productId) + 1e-9) {
    toast(`Só há ${fmtNum(Math.max(0, disponivelEdicao(productId)))} ${a.unit} disponível de ${a.name}.`, 'error');
    return;
  }
  const existente = itensEdicaoRequisicao.find((i) => i.productId === productId);
  if (existente) existente.qty += qty;
  else itensEdicaoRequisicao.push({ id: uid('ri_'), productId, productName: a.name, unit: a.unit, qty });
  document.getElementById('reqEditQtd').value = '';
  renderDetail();
}

async function salvarEdicaoItens() {
  const itens = itensEdicaoRequisicao.filter((i) => i.qty > 0);
  if (!itens.length) { toast('Adicione pelo menos um material.', 'error'); return; }
  const botao = document.getElementById('btnEditSalvarItens');
  botao.disabled = true;
  try {
    await api.updateRequestItems(requisicaoDetalheAtual.id,
      itens.map((i) => ({ id: i.id, productId: i.productId, qty: i.qty })));
    await reload();
    const atualizado = requests.find((x) => x.id === requisicaoDetalheAtual.id);
    itensEdicaoRequisicao = null;
    if (atualizado) {
      requisicaoDetalheAtual = atualizado;
      renderDetail();
    } else {
      closeModal('modalRequisicaoDetalhe');
    }
    toast('Itens da requisição atualizados.', 'success');
  } catch (e) {
    toast('Erro: ' + errorText(e), 'error');
    botao.disabled = false;
  }
}

// --------------------------------------------------------------- decisões

function openDecision(acao, id) {
  const r = requests.find((x) => x.id === id);
  if (!r) return;
  decisaoPendente = { acao, id };

  const titulos = {
    aprovar: 'Aprovar requisição',
    rejeitar: 'Rejeitar requisição',
    cancelar: 'Cancelar requisição',
    entregar: 'Confirmar entrega',
  };
  const rotulos = {
    aprovar: 'Observação da aprovação',
    rejeitar: 'Motivo da rejeição',
    cancelar: 'Motivo do cancelamento',
    entregar: 'Observação',
  };
  document.getElementById('decisaoTitulo').textContent = titulos[acao];
  document.getElementById('decisaoNotaLabel').textContent = rotulos[acao];
  document.getElementById('decisaoNota').value = '';
  document.getElementById('decisaoNota').parentElement.style.display = acao === 'entregar' ? 'none' : '';
  document.getElementById('decisaoResumo').innerHTML =
    `<b>${codigo(r)}</b> · ${escapeHtml(r.departmentName)} · ${escapeHtml(r.requesterName)}<br/>` +
    (r.items || []).map((i) => `${fmtNum(i.qty)}× ${escapeHtml(i.productName)}`).join(' · ') +
    `<br/>Valor estimado: <b>${fmtBRL(valorEstimado(r))}</b>`;

  const aviso = document.getElementById('decisaoAviso');
  if (acao === 'entregar') {
    aviso.innerHTML = 'Confirmar a entrega <b>debita o estoque agora</b>: uma saída por material é gerada ' +
      'na Linha do Tempo, em nome do solicitante e do departamento. Isso não se desfaz por aqui.';
    aviso.style.display = '';
  } else if (acao === 'aprovar') {
    aviso.innerHTML = 'Aprovar <b>não</b> baixa o estoque — o material segue reservado até a confirmação ' +
      'da entrega.';
    aviso.style.display = '';
  } else {
    aviso.innerHTML = 'O material volta a ficar disponível para outros pedidos.';
    aviso.style.display = '';
  }

  const botao = document.getElementById('btnConfirmarDecisao');
  botao.textContent = titulos[acao];
  botao.className = acao === 'rejeitar' || acao === 'cancelar' ? 'btn-danger' : 'btn-primary';
  openModal('modalDecisao');
}

async function confirmDecision() {
  if (!decisaoPendente) return;
  const { acao, id } = decisaoPendente;
  const nota = document.getElementById('decisaoNota').value.trim();
  const botao = document.getElementById('btnConfirmarDecisao');
  botao.disabled = true;
  try {
    if (acao === 'aprovar') await api.approveRequest(id, nota);
    else if (acao === 'rejeitar') await api.rejectRequest(id, nota);
    else if (acao === 'cancelar') await api.cancelRequest(id, nota);
    else await api.deliverRequest(id, uid('m_'));

    await reload();
    closeModal('modalDecisao');
    decisaoPendente = null;
    const mensagens = {
      aprovar: 'Requisição aprovada. O material continua reservado até a entrega.',
      rejeitar: 'Requisição rejeitada e reserva liberada.',
      cancelar: 'Requisição cancelada e reserva liberada.',
      entregar: 'Entrega confirmada — estoque baixado e saídas registradas.',
    };
    toast(mensagens[acao], 'success');
    // A Linha do Tempo escuta este evento e se recarrega se estiver aberta; as
    // demais telas recarregam sozinhas quando o usuário navegar até elas.
    document.dispatchEvent(new CustomEvent('estoque:dados-alterados'));
  } catch (e) {
    toast('Erro: ' + errorText(e), 'error');
  } finally {
    botao.disabled = false;
  }
}

// --------------------------------------------------------- nova requisição

function disponivelDe(productId) {
  const a = availability.find((x) => x.productId === productId);
  if (!a) return 0;
  // Desconta o que já foi posto no rascunho desta requisição — senão daria
  // para montar um pedido que o backend vai recusar só no "Enviar".
  const jaNoRascunho = itensNovaRequisicao
    .filter((i) => i.productId === productId)
    .reduce((s, i) => s + i.qty, 0);
  return a.available - jaNoRascunho;
}

function openNewRequestModal(forcado) {
  itensNovaRequisicao = [];
  const eu = currentUser();

  document.getElementById('modalRequisicaoTitulo').textContent =
    forcado ? '＋ Lançar pedido (janela fechada)' : '＋ Nova requisição';
  document.getElementById('reqAvisoJanelaFechada').style.display = forcado ? '' : 'none';

  const box = document.getElementById('reqDepartamentoBox');
  const fixo = document.getElementById('reqDepartamentoFixo');
  if (isSuperadmin()) {
    box.style.display = '';
    fixo.style.display = 'none';
    const ordenados = [...departments].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
    document.getElementById('reqDepartamento').innerHTML = ordenados.length
      ? ordenados.map((d) => `<option value="${d.id}">${escapeHtml(d.name)}</option>`).join('')
      : '<option value="">Nenhum departamento cadastrado</option>';
  } else {
    box.style.display = 'none';
    fixo.style.display = '';
    fixo.innerHTML = eu && eu.departmentName
      ? `Pedido para o departamento <b>${escapeHtml(eu.departmentName)}</b>, em nome de <b>${escapeHtml(eu.name)}</b>.`
      : 'Seu usuário não está atrelado a nenhum departamento — peça ao administrador para vincular ' +
        'antes de requisitar.';
  }

  const comSaldo = availability.filter((a) => a.available > 0);
  document.getElementById('reqProduto').innerHTML = comSaldo.length
    ? comSaldo.map((a) =>
        `<option value="${a.productId}">${escapeHtml(a.name)} — ${fmtNum(a.available)} ${escapeHtml(a.unit)} disponível</option>`).join('')
    : '<option value="">Nenhum material com saldo disponível</option>';
  instalarFiltroSelect('reqProduto', 'Digite o nome do material...');
  document.getElementById('reqQtd').value = '';
  document.getElementById('reqObs').value = '';
  renderDraftItems();
  updateDisponivelHint();
  openModal('modalRequisicao');
}

function updateDisponivelHint() {
  const productId = document.getElementById('reqProduto').value;
  const a = availability.find((x) => x.productId === productId);
  const hint = document.getElementById('reqDisponivelHint');
  atualizarFotoSelecionada(a);
  if (!a) { hint.textContent = ''; return; }
  const livre = disponivelDe(productId);
  hint.innerHTML = `Em estoque: <b>${fmtNum(a.qty)} ${escapeHtml(a.unit)}</b> · reservado em outros pedidos: ` +
    `<b>${fmtNum(a.reserved)}</b> · pode pedir agora: <b>${fmtNum(Math.max(0, livre))}</b>`;
}

// #reqProdutoFoto é um único elemento fixo no HTML (não é recriado a cada
// render, ao contrário das células de tabela) — atualiza conteúdo/atributos
// no lugar, nunca via innerHTML/outerHTML do container, senão o clique
// ligado uma vez em initRequests() se perderia.
function atualizarFotoSelecionada(a) {
  const el = document.getElementById('reqProdutoFoto');
  if (!a || !a.thumbnailPath) {
    el.innerHTML = '<span class="thumb-placeholder"></span>';
    delete el.dataset.thumbPath;
    delete el.dataset.imagePath;
    delete el.dataset.nome;
    return;
  }
  el.dataset.thumbPath = a.thumbnailPath;
  el.dataset.imagePath = a.imagePath || a.thumbnailPath;
  el.dataset.nome = a.name;
  el.innerHTML = '<span class="thumb-placeholder"></span>';
  api.readProductImage(a.thumbnailPath)
    .then((dataUrl) => { el.innerHTML = `<img src="${dataUrl}" alt="" />`; })
    .catch(() => {});
}

function addItem() {
  const productId = document.getElementById('reqProduto').value;
  const qty = parseFloat(document.getElementById('reqQtd').value);
  const a = availability.find((x) => x.productId === productId);
  if (!a) { toast('Selecione um material.', 'error'); return; }
  if (!qty || qty <= 0) { toast('Informe uma quantidade maior que zero.', 'error'); return; }
  if (qty > disponivelDe(productId) + 1e-9) {
    toast(`Só há ${fmtNum(Math.max(0, disponivelDe(productId)))} ${a.unit} disponível de ${a.name}.`, 'error');
    return;
  }

  const existente = itensNovaRequisicao.find((i) => i.productId === productId);
  if (existente) existente.qty += qty;
  else itensNovaRequisicao.push({ id: uid('ri_'), productId, productName: a.name, unit: a.unit, qty });

  document.getElementById('reqQtd').value = '';
  renderDraftItems();
  updateDisponivelHint();
}

function renderDraftItems() {
  const tbody = document.getElementById('reqItensTbody');
  if (!itensNovaRequisicao.length) {
    tbody.innerHTML = '<tr class="req-itens-vazio"><td colspan="5">Nenhum material adicionado ainda.</td></tr>';
    return;
  }
  tbody.innerHTML = itensNovaRequisicao.map((i, idx) => {
    const a = availability.find((x) => x.productId === i.productId);
    return `<tr><td>${thumbCellHtml(a)}</td><td>${escapeHtml(i.productName)}</td>
      <td class="num">${fmtNum(i.qty)} ${escapeHtml(i.unit || '')}</td>
      <td class="num">${a ? fmtNum(a.available) : '—'}</td>
      <td><button class="btn-sm btn-ghost" data-remover="${idx}">Remover</button></td></tr>`;
  }).join('');
  wireThumbs(tbody);
  tbody.querySelectorAll('[data-remover]').forEach((b) =>
    b.addEventListener('click', () => {
      itensNovaRequisicao.splice(Number(b.dataset.remover), 1);
      renderDraftItems();
      updateDisponivelHint();
    }));
}

async function saveRequest() {
  if (!itensNovaRequisicao.length) { toast('Adicione pelo menos um material.', 'error'); return; }
  const payload = {
    id: uid('req_'),
    createdAt: nowIso(),
    obs: document.getElementById('reqObs').value.trim(),
    items: itensNovaRequisicao.map((i) => ({ id: i.id, productId: i.productId, qty: i.qty })),
  };
  // Só o superadmin escolhe o setor; para os demais o backend força o próprio
  // (mandar aqui não adiantaria nada — ver createRequest em api.cpp).
  if (isSuperadmin()) payload.departmentId = document.getElementById('reqDepartamento').value;

  const botao = document.getElementById('btnSalvarRequisicao');
  botao.disabled = true;
  try {
    const criada = await api.createRequest(payload);
    // Guarda o resumo ANTES do reload: `itensNovaRequisicao` é zerado quando o
    // modal de nova requisição for reaberto, e o retorno do backend traz só os
    // ids dos itens.
    const resumo = itensNovaRequisicao
      .map((i) => `${fmtNum(i.qty)} ${escapeHtml(i.unit || '')} de ${escapeHtml(i.productName)}`)
      .join('<br/>');

    await reload();
    closeModal('modalRequisicao');
    showPedidoOk(criada, resumo);
  } catch (e) {
    toast('Erro: ' + errorText(e), 'error');
  } finally {
    botao.disabled = false;
  }
}

// Confirmação em modal, e não só um toast: o solicitante fica com o número do
// pedido na tela até fechar por conta, em vez de ter três segundos para
// decorá-lo.
function showPedidoOk(criada, resumo) {
  document.getElementById('pedidoOkCorpo').innerHTML = `
    <div class="info-box">Seu pedido foi registrado com o número <b>${codigo(criada)}</b> e já está
      na fila de aprovação.</div>
    <div class="section-title" style="margin-top:var(--sp-4);">Materiais pedidos</div>
    <div style="font-size:12.5px;">${resumo}</div>
    <div class="muted" style="font-size:11.5px;margin-top:var(--sp-3);">
      O material fica <b>reservado</b> em seu nome desde já. A baixa no estoque só acontece quando a
      entrega for confirmada — você acompanha o andamento na lista desta tela.
    </div>`;
  openModal('modalPedidoOk');
}
