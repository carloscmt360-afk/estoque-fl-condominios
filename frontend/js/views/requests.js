import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtNum, fmtBRL, fmtDateTimeBR } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can, canValidateRequests, currentUser, isSuperadmin } from '../session.js';

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

let requests = [];
let availability = [];
let departments = [];
let itensNovaRequisicao = [];
let decisaoPendente = null;  // { acao, id }
let wired = false;

export async function initRequests() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovaRequisicao').addEventListener('click', openNewRequestModal);
    document.getElementById('btnAdicionarItem').addEventListener('click', addItem);
    document.getElementById('btnSalvarRequisicao').addEventListener('click', saveRequest);
    document.getElementById('btnConfirmarDecisao').addEventListener('click', confirmDecision);
    document.getElementById('reqProduto').addEventListener('change', updateDisponivelHint);
    document.getElementById('reqQtd').addEventListener('input', updateDisponivelHint);
    document.getElementById('filtroRequisicaoBusca').addEventListener('input', render);
    document.querySelectorAll('#filtroRequisicaoStatus button').forEach((b) => {
      b.addEventListener('click', () => {
        document.querySelectorAll('#filtroRequisicaoStatus button').forEach((x) => x.classList.remove('active'));
        b.classList.add('active');
        render();
      });
    });
  }
  await reload();
}

export async function reload() {
  const tarefas = [api.listRequests(), api.stockAvailability()];
  // A lista de setores só interessa a quem pode requisitar em nome de outro.
  tarefas.push(isSuperadmin() ? api.listDepartments() : Promise.resolve([]));
  [requests, availability, departments] = await Promise.all(tarefas);

  document.getElementById('btnNovaRequisicao').style.display = can('requisicoes', 'create') ? '' : 'none';
  document.getElementById('requestsSub').textContent = canValidateRequests()
    ? 'Pedidos de todos os departamentos — aprovação e entrega'
    : 'Pedidos do seu departamento';
  renderStatGrid();
  render();
}

function statusInfo(s) {
  return STATUS[s] || { label: s, cls: '' };
}

function codigo(r) {
  return '#' + String(r.id).slice(-6).toUpperCase();
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
    const resumo = itens.slice(0, 2).map((i) => `${fmtNum(i.qty)}× ${escapeHtml(i.productName)}`).join('<br/>');
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
  const info = statusInfo(r.status);
  const linhas = (r.items || []).map((i) => {
    const a = availability.find((x) => x.productId === i.productId);
    return `<tr><td>${escapeHtml(i.productName)}</td><td class="num">${fmtNum(i.qty)}</td>
      <td>${escapeHtml(i.unit || '')}</td>
      <td class="num">${a ? fmtBRL(i.qty * a.avgCost) : '<span class="muted">—</span>'}</td>
      <td>${i.movementId
        ? '<span class="pill pill-entregue">baixado</span>'
        : (r.status === 'pendente' || r.status === 'aprovado'
            ? '<span class="pill pill-aprovado">reservado</span>'
            : '<span class="muted">—</span>')}</td></tr>`;
  }).join('');

  document.getElementById('reqDetalheTitulo').textContent = `Requisição ${codigo(r)}`;
  document.getElementById('reqDetalheCorpo').innerHTML = `
    <div class="info-box"><b>${escapeHtml(r.departmentName)}</b> ·
      <span class="pill ${info.cls}">${info.label}</span>
      ${r.obs ? `<br/>Observação: ${escapeHtml(r.obs)}` : ''}</div>
    <div class="section-title" style="margin-top:0;">Materiais</div>
    <div class="panel"><table>
      <thead><tr><th>Material</th><th class="num">Qtd.</th><th>Unid.</th>
        <th class="num">Valor estimado</th><th>Situação do item</th></tr></thead>
      <tbody>${linhas || '<tr class="empty-row"><td colspan="5">Sem itens.</td></tr>'}</tbody>
    </table></div>
    <div class="muted" style="font-size:11px;">Valor estimado pelo custo médio atual; a baixa real é
      valorizada pelo custo médio da data da entrega.</div>
    <div class="section-title">Andamento</div>
    ${trackHtml(r)}`;
  openModal('modalRequisicaoDetalhe');
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

function openNewRequestModal() {
  itensNovaRequisicao = [];
  const eu = currentUser();

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
  if (!a) { hint.textContent = ''; return; }
  const livre = disponivelDe(productId);
  hint.innerHTML = `Em estoque: <b>${fmtNum(a.qty)} ${escapeHtml(a.unit)}</b> · reservado em outros pedidos: ` +
    `<b>${fmtNum(a.reserved)}</b> · pode pedir agora: <b>${fmtNum(Math.max(0, livre))}</b>`;
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
    tbody.innerHTML = '<tr class="req-itens-vazio"><td colspan="4">Nenhum material adicionado ainda.</td></tr>';
    return;
  }
  tbody.innerHTML = itensNovaRequisicao.map((i, idx) => {
    const a = availability.find((x) => x.productId === i.productId);
    return `<tr><td>${escapeHtml(i.productName)}</td>
      <td class="num">${fmtNum(i.qty)} ${escapeHtml(i.unit || '')}</td>
      <td class="num">${a ? fmtNum(a.available) : '—'}</td>
      <td><button class="btn-sm btn-ghost" data-remover="${idx}">Remover</button></td></tr>`;
  }).join('');
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

  try {
    await api.createRequest(payload);
    await reload();
    closeModal('modalRequisicao');
    toast('Requisição enviada. O material ficou reservado até a validação.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
