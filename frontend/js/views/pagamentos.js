import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtBRL, fmtDateBR } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';
import { bindMoneyMask, setMoneyMaskedValue, parseMoneyMasked } from '../masks.js';

// Compras > Acompanhamento de pagamentos — uma NF ligada a uma Aquisição já
// lançada, com as parcelas dela. Cada parcela é marcada paga individualmente
// (com a data da baixa) em "Ver parcelas" — o modal de Nova/Editar NF só
// monta a COMPOSIÇÃO das parcelas (quantas, valor, vencimento), nunca marca
// pago por ali.

let pagamentos = [];
let aquisicoes = [];
let parcelasEdit = [];  // parcelas em edição no modal Nova/Editar NF
let busca = '';
let wired = false;

export async function initPagamentos() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovoPagamento').addEventListener('click', () => openPagamentoModal());
    document.getElementById('btnSalvarPagamento').addEventListener('click', savePagamento);
    document.getElementById('btnAdicionarParcela').addEventListener('click', adicionarParcela);
    document.getElementById('pagAquisicaoId').addEventListener('change', atualizaValorSugerido);
    document.getElementById('pagBusca').addEventListener('input', (e) => {
      busca = e.target.value.toLowerCase();
      render();
    });
    enableRowSelection(document.getElementById('pagamentosTbody'));
    bindMoneyMask(document.getElementById('pagValorTotal'));
  }
  await reload();
}

export async function reload() {
  try {
    const [pag, aq] = await Promise.all([api.listPagamentos(), api.listAquisicoes()]);
    pagamentos = pag; aquisicoes = aq;
  } catch (e) {
    toast('Erro ao carregar os pagamentos: ' + errorText(e), 'error');
    pagamentos = []; aquisicoes = [];
  }
  document.getElementById('btnNovoPagamento').style.display = can('pagamentos', 'create') ? '' : 'none';
  render();
}

function situacaoPagamento(p) {
  if (p.quitado) return { txt: 'Quitado', cls: 'pill-ok' };
  if (p.totalPago > 0) return { txt: 'Parcial', cls: 'pill-warn' };
  return { txt: 'Em aberto', cls: 'pill-low' };
}

function bateBusca(p) {
  if (!busca) return true;
  return [p.notaFiscal, p.aquisicaoDescricao, p.fornecedorNome].filter(Boolean).join(' ').toLowerCase().includes(busca);
}

function render() {
  const tbody = document.getElementById('pagamentosTbody');
  const filtrados = pagamentos.filter(bateBusca);
  document.getElementById('pagamentosTotal').textContent = pagamentos.length;
  document.getElementById('pagamentosFiltrados').textContent = busca ? ` · ${filtrados.length} no filtro` : '';

  if (!filtrados.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="9">${
      pagamentos.length ? 'Nenhuma NF encontrada com esse filtro.' : 'Nenhuma NF lançada ainda.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = filtrados.map((p) => {
    const sit = situacaoPagamento(p);
    const acoes = [
      `<button class="btn-sm btn-ghost" data-parcelas="${p.id}">Ver parcelas</button>`,
      can('pagamentos', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${p.id}">Editar</button>` : '',
      can('pagamentos', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${p.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    return `<tr>
      <td><b>${escapeHtml(p.notaFiscal)}</b></td>
      <td>${escapeHtml(p.aquisicaoDescricao || '(aquisição excluída)')}</td>
      <td>${escapeHtml(p.fornecedorNome)}</td>
      <td>${fmtDateBR(p.dataEmissao)}</td>
      <td class="num">${fmtBRL(p.valorTotal)}</td>
      <td class="num">${fmtBRL(p.totalPago)}</td>
      <td class="num">${fmtBRL(p.totalEmAberto)}</td>
      <td><span class="pill ${sit.cls}">${sit.txt}</span></td>
      <td><div class="row-actions">${acoes}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-parcelas]').forEach((b) =>
    b.addEventListener('click', () => openParcelasModal(b.dataset.parcelas)));
  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openPagamentoModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deletePagamento(b.dataset.excluir)));
}

// ------------------------------------------------------- modal Nova/Editar NF

function opcaoCongelada(id, txt, lista) {
  if (!id || lista.some((x) => x.id === id)) return '';
  return `<option value="${id}">${escapeHtml(txt)} (excluída)</option>`;
}

function popularAquisicaoSelect(selecionado) {
  const sel = document.getElementById('pagAquisicaoId');
  sel.innerHTML = '<option value="">Selecione...</option>' + [...aquisicoes]
    .sort((a, b) => (b.dataCompra || '').localeCompare(a.dataCompra || ''))
    .map((a) => `<option value="${a.id}">${escapeHtml(a.fornecedorNome)} — ${escapeHtml(a.descricao)} (${fmtBRL(a.valor)})</option>`)
    .join('') +
    (selecionado ? opcaoCongelada(selecionado.aquisicaoId, selecionado.aquisicaoDescricao, aquisicoes) : '');
  if (selecionado) sel.value = selecionado.aquisicaoId;
}

// Sugere o valor total da NF a partir do valor da aquisição escolhida — só
// quando o campo ainda está vazio (edição existente não é sobrescrita).
function atualizaValorSugerido() {
  const campoValor = document.getElementById('pagValorTotal');
  if (parseMoneyMasked(campoValor.value) > 0) return;
  const aq = aquisicoes.find((x) => x.id === document.getElementById('pagAquisicaoId').value);
  if (aq) setMoneyMaskedValue(campoValor, aq.valor);
}

function renderParcelasEdit() {
  const tbody = document.getElementById('pagParcelasTbody');
  document.getElementById('pagParcelasContagem').textContent =
    parcelasEdit.length ? `(${parcelasEdit.length})` : '';

  if (!parcelasEdit.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="5">Nenhuma parcela — adicione ao menos uma.</td></tr>';
    return;
  }
  tbody.innerHTML = parcelasEdit.map((p, i) => `<tr>
    <td class="num">${i + 1}</td>
    <td class="num"><input type="text" inputmode="decimal" data-parc-valor="${i}"
      style="width:110px;" ${p.pago ? 'disabled' : ''} /></td>
    <td><input type="date" data-parc-vencimento="${i}" value="${p.vencimento}" ${p.pago ? 'disabled' : ''} /></td>
    <td>${p.pago ? `<span class="pill pill-ok">Paga em ${fmtDateBR(p.dataPagamento)}</span>` : '<span class="pill pill-low">Em aberto</span>'}</td>
    <td>${p.pago ? '' : `<button type="button" class="btn-sm btn-danger" data-parc-remover="${i}">Remover</button>`}</td>
  </tr>`).join('');

  tbody.querySelectorAll('[data-parc-valor]').forEach((inp) => {
    setMoneyMaskedValue(inp, parcelasEdit[Number(inp.dataset.parcValor)].valor);
    bindMoneyMask(inp);
    inp.addEventListener('input', (e) => {
      parcelasEdit[Number(e.target.dataset.parcValor)].valor = parseMoneyMasked(e.target.value);
    });
  });
  tbody.querySelectorAll('[data-parc-vencimento]').forEach((inp) => inp.addEventListener('input', (e) => {
    parcelasEdit[Number(e.target.dataset.parcVencimento)].vencimento = e.target.value;
  }));
  tbody.querySelectorAll('[data-parc-remover]').forEach((btn) => btn.addEventListener('click', (e) => {
    parcelasEdit.splice(Number(e.target.dataset.parcRemover), 1);
    renderParcelasEdit();
  }));
}

function adicionarParcela() {
  parcelasEdit.push({ id: '', numero: 0, valor: 0, vencimento: '', pago: false, dataPagamento: '' });
  renderParcelasEdit();
}

function openPagamentoModal(id) {
  if (!aquisicoes.length) { toast('Lance uma aquisição primeiro (Aquisições FL).', 'error'); return; }
  const p = id ? pagamentos.find((x) => x.id === id) : null;
  document.getElementById('modalPagamentoTitulo').textContent = id ? 'Editar NF' : 'Nova NF';
  document.getElementById('pagId').value = id || '';
  popularAquisicaoSelect(p);
  document.getElementById('pagAquisicaoId').disabled = !!id;  // não muda a aquisição numa NF já lançada
  document.getElementById('pagNotaFiscal').value = p ? p.notaFiscal : '';
  setMoneyMaskedValue(document.getElementById('pagValorTotal'), p ? p.valorTotal : 0);
  document.getElementById('pagDataEmissao').value = p ? p.dataEmissao : new Date().toISOString().slice(0, 10);
  document.getElementById('pagObservacoes').value = p ? p.observacoes : '';
  parcelasEdit = p ? p.parcelas.map((x) => ({ ...x })) : [{ id: '', numero: 0, valor: 0, vencimento: '', pago: false, dataPagamento: '' }];
  renderParcelasEdit();
  openModal('modalPagamento');
}

async function savePagamento() {
  const id = document.getElementById('pagId').value;
  const aquisicaoId = document.getElementById('pagAquisicaoId').value;
  const notaFiscal = document.getElementById('pagNotaFiscal').value.trim();
  const dataEmissao = document.getElementById('pagDataEmissao').value;
  if (!aquisicaoId) { toast('Selecione a aquisição.', 'error'); return; }
  if (!notaFiscal) { toast('Informe o número da nota fiscal.', 'error'); return; }
  if (!dataEmissao) { toast('Informe a data de emissão.', 'error'); return; }
  if (!parcelasEdit.length) { toast('Adicione ao menos uma parcela.', 'error'); return; }
  if (parcelasEdit.some((p) => !p.vencimento)) { toast('Informe o vencimento de todas as parcelas.', 'error'); return; }
  const payload = {
    id: id || uid('pag_'),
    aquisicaoId, notaFiscal,
    valorTotal: parseMoneyMasked(document.getElementById('pagValorTotal').value),
    dataEmissao,
    observacoes: document.getElementById('pagObservacoes').value.trim(),
    createdAt: id ? '' : nowIso(),
    parcelas: parcelasEdit,
  };
  try {
    if (id) await api.updatePagamento(payload);
    else await api.createPagamento(payload);
    await reload();
    closeModal('modalPagamento');
    toast('NF salva.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deletePagamento(id) {
  const p = pagamentos.find((x) => x.id === id);
  if (!p) return;
  if (!confirm(`Excluir a NF "${p.notaFiscal}"? As parcelas dela também serão excluídas.`)) return;
  try {
    await api.deletePagamento(id);
    await reload();
    toast('NF excluída.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

// --------------------------------------------------------- modal Ver parcelas

function renderParcelasModal(p) {
  document.getElementById('modalParcelasTitulo').textContent = `Parcelas — NF ${p.notaFiscal}`;
  const host = document.getElementById('parcelasLista');
  host.innerHTML = p.parcelas.map((parc, i) => `
    <div class="panel" style="margin-bottom:var(--sp-2);padding:var(--sp-3);">
      <div style="display:flex;justify-content:space-between;align-items:center;gap:var(--sp-3);flex-wrap:wrap;">
        <div>
          <b>Parcela ${parc.numero || i + 1}</b> — ${fmtBRL(parc.valor)}
          <div class="muted" style="font-size:11.5px;">Vencimento: ${fmtDateBR(parc.vencimento)}</div>
        </div>
        ${parc.pago
          ? `<div style="display:flex;align-items:center;gap:var(--sp-2);">
               <span class="pill pill-ok">Paga em ${fmtDateBR(parc.dataPagamento)}</span>
               <button class="btn-sm btn-outline" data-reabrir="${parc.id}">Reabrir</button>
             </div>`
          : `<div style="display:flex;align-items:center;gap:var(--sp-2);">
               <input type="date" data-data-pagamento="${parc.id}" value="${new Date().toISOString().slice(0, 10)}" />
               <button class="btn-sm btn-primary" data-marcar-pago="${parc.id}">Marcar paga</button>
             </div>`}
      </div>
    </div>`).join('');

  host.querySelectorAll('[data-marcar-pago]').forEach((btn) => btn.addEventListener('click', async () => {
    const parcelaId = btn.dataset.marcarPago;
    const dataInput = host.querySelector(`[data-data-pagamento="${parcelaId}"]`);
    const dataPagamento = dataInput ? dataInput.value : '';
    if (!dataPagamento) { toast('Informe a data do pagamento.', 'error'); return; }
    try {
      const atualizado = await api.marcarParcela({ pagamentoId: p.id, parcelaId, pago: true, dataPagamento });
      await reload();
      renderParcelasModal(atualizado);
    } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
  }));
  host.querySelectorAll('[data-reabrir]').forEach((btn) => btn.addEventListener('click', async () => {
    if (!confirm('Reabrir esta parcela (voltar para "em aberto")?')) return;
    try {
      const atualizado = await api.marcarParcela({ pagamentoId: p.id, parcelaId: btn.dataset.reabrir, pago: false, dataPagamento: '' });
      await reload();
      renderParcelasModal(atualizado);
    } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
  }));
}

function openParcelasModal(id) {
  const p = pagamentos.find((x) => x.id === id);
  if (!p) return;
  renderParcelasModal(p);
  openModal('modalParcelas');
}
