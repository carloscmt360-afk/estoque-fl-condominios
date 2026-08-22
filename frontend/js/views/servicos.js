import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtBRL } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { initImportarServicos } from './servicosImportar.js';
import { printDocument, buildServicosDoc } from '../print.js';
import { enableRowSelection } from '../components/tableTools.js';

// Gestão SOS > Serviços — a planilha de vendas/comissões. Cada linha liga um
// condomínio, opcionalmente um gerente e um parceiro, a uma venda com
// porcentagem de comissão, num mês de referência.
//
// "ID" é o número sequencial que o C++ atribui (AUTOINCREMENT — nunca se
// repete, mesmo depois de excluir uma linha); a comissão vem PRONTA do
// backend (venda × porcentagem ÷ 100), nunca calculada aqui, mesmo critério
// do vencimento em Gestão de Prazos. Um serviço dentro de um mês já fechado
// não pode ser editado nem excluído — só reabrir o fechamento libera.

let servicos = [];
let condominios = [];
let gerentes = [];
let parceiros = [];
let porcentagemPadrao = 0;
let filtroMesDe = '';
let filtroMesAte = '';
let filtroCondominio = '';
let filtroGerente = '';
let filtroParceiro = '';
let wired = false;

// Seleção para "Alterar % em massa" — marcar várias linhas (como marcar
// várias células no Excel) e aplicar uma só porcentagem a todas de uma vez.
// Serve principalmente para estudar cenários/probabilidades sem precisar
// editar cada lançamento um a um.
let selecionados = new Set();

// ids alvo do modal de pagamento em massa (pode ser um único id, quando vem
// do botão "Marcar pago" de uma linha, ou toda a seleção do bulk).
let pagoLoteAlvoIds = [];

// "YYYY-MM-DD" → "DD/MM/YYYY" por fatiamento de string, nunca via Date() —
// um <input type="date"> gravado como dia puro (sem hora) vira meia-noite
// UTC, e reformatar isso pelo fuso local pode voltar um dia (mesmo cuidado
// de fmtMesAnoBR em format.js).
function fmtDataPagamento(yyyyMmDd) {
  if (!yyyyMmDd || yyyyMmDd.length !== 10) return '—';
  const [ano, mes, dia] = yyyyMmDd.split('-');
  return `${dia}/${mes}/${ano}`;
}

export async function initServicos() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovoServico').addEventListener('click', () => openServicoModal());
    document.getElementById('btnSalvarServico').addEventListener('click', saveServico);
    document.getElementById('srvFiltroMesDe').addEventListener('input', (e) => {
      filtroMesDe = e.target.value;
      render();
    });
    document.getElementById('srvFiltroMesAte').addEventListener('input', (e) => {
      filtroMesAte = e.target.value;
      render();
    });
    document.getElementById('srvFiltroCondominio').addEventListener('input', (e) => {
      filtroCondominio = e.target.value;
      render();
    });
    document.getElementById('srvFiltroGerente').addEventListener('change', (e) => {
      filtroGerente = e.target.value;
      render();
    });
    document.getElementById('srvFiltroParceiro').addEventListener('change', (e) => {
      filtroParceiro = e.target.value;
      render();
    });
    document.getElementById('btnLimparFiltrosServicos').addEventListener('click', limparFiltros);
    ['srvVenda', 'srvPorcentagem'].forEach((id) =>
      document.getElementById(id).addEventListener('input', atualizaComissaoPrevista));
    document.getElementById('btnImprimirServicos').addEventListener('click', abrirFiltroRelatorio);
    document.getElementById('btnGerarRelatorioServicos').addEventListener('click', gerarRelatorio);
    document.getElementById('srvSelecionarTodos').addEventListener('change', onSelecionarTodos);
    document.getElementById('btnPctLote').addEventListener('click', abrirPctLote);
    document.getElementById('btnConfirmarPctLote').addEventListener('click', confirmarPctLote);
    document.getElementById('btnPagoLote').addEventListener('click', () => abrirPagoLote([...selecionados]));
    document.getElementById('btnConfirmarPagoLote').addEventListener('click', confirmarPagoLote);
    document.querySelectorAll('input[name="srvPago"]').forEach((r) =>
      r.addEventListener('change', atualizaDataPagamentoObrigatoria));
    enableRowSelection(document.getElementById('servicosTbody'));
    initImportarServicos(
      () => ({ condominios, gerentes, parceiros, porcentagemPadrao }),
      reload,
    );
  }
  await reload();
}

export async function reload() {
  const [srv, cond, ger, par, config] = await Promise.all([
    api.listServicos(), api.listCondominios(), api.listGerentes(), api.listParceiros(), api.getSosConfig(),
  ]);
  servicos = srv; condominios = cond; gerentes = ger; parceiros = par;
  porcentagemPadrao = parseFloat(config.porcentagemPadrao) || 0;
  document.getElementById('btnNovoServico').style.display = can('gestao_sos_servicos', 'create') ? '' : 'none';
  document.getElementById('btnImportarServicos').style.display = can('gestao_sos_servicos', 'create') ? '' : 'none';
  popularFiltros();
  render();
}

// Selects de filtro (Gerente/Parceiro) — mantém a seleção atual ao recarregar
// (depois de editar um serviço, por exemplo), sem resetar o filtro em uso.
function popularFiltros() {
  const selGer = document.getElementById('srvFiltroGerente');
  const selPar = document.getElementById('srvFiltroParceiro');
  selGer.innerHTML = '<option value="">Todos os gerentes</option>' + [...gerentes]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((g) => `<option value="${g.id}">${escapeHtml(g.nome)}</option>`).join('');
  selPar.innerHTML = '<option value="">Todos os parceiros</option>' + [...parceiros]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((p) => `<option value="${p.id}">${escapeHtml(p.nome)}</option>`).join('');
  selGer.value = filtroGerente;
  selPar.value = filtroParceiro;
}

function limparFiltros() {
  filtroMesDe = ''; filtroMesAte = ''; filtroCondominio = ''; filtroGerente = ''; filtroParceiro = '';
  document.getElementById('srvFiltroMesDe').value = '';
  document.getElementById('srvFiltroMesAte').value = '';
  document.getElementById('srvFiltroCondominio').value = '';
  document.getElementById('srvFiltroGerente').value = '';
  document.getElementById('srvFiltroParceiro').value = '';
  render();
}

function mesAnoLabel(yyyymm) {
  if (!yyyymm || yyyymm.length !== 7) return '—';
  const [ano, mes] = yyyymm.split('-');
  return `${mes}/${ano}`;
}

// "YYYY-MM" compara certo como string — não precisa converter pra data (mesmo
// critério de gerarRelatorio, mais abaixo).
function listaFiltrada() {
  const buscaCondominio = filtroCondominio.trim().toLowerCase();
  return servicos.filter((s) => {
    if (filtroMesDe && s.dataReferencia < filtroMesDe) return false;
    if (filtroMesAte && s.dataReferencia > filtroMesAte) return false;
    if (buscaCondominio && !s.condominioNome.toLowerCase().includes(buscaCondominio)) return false;
    if (filtroGerente && s.gerenteId !== filtroGerente) return false;
    if (filtroParceiro && s.parceiroId !== filtroParceiro) return false;
    return true;
  });
}

function temFiltroAtivo() {
  return !!(filtroMesDe || filtroMesAte || filtroCondominio.trim() || filtroGerente || filtroParceiro);
}

function render() {
  const tbody = document.getElementById('servicosTbody');
  const lista = listaFiltrada();

  document.getElementById('servicosTotal').textContent = servicos.length;
  document.getElementById('servicosFiltrados').textContent =
    temFiltroAtivo() ? ` · ${lista.length} no filtro` : '';

  // Uma linha some do filtro (ou é excluída) sem que a seleção seja limpa —
  // sem isto o contador do botão "Alterar % em massa" ficaria contando
  // linhas que não existem mais.
  const idsVisiveis = new Set(lista.map((s) => s.id));
  for (const id of selecionados) if (!idsVisiveis.has(id)) selecionados.delete(id);

  if (!lista.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="13">${
      servicos.length ? 'Nenhum serviço neste mês de referência.' : 'Nenhum serviço lançado ainda.'}</td></tr>`;
    atualizaBotaoPctLote();
    atualizaBotaoPagoLote();
    return;
  }
  tbody.innerHTML = lista.map((s) => {
    const podeEditar = !s.fechado && can('gestao_sos_servicos', 'update');
    const podeExcluir = !s.fechado && can('gestao_sos_servicos', 'delete');
    // Um serviço fechado não tem Editar/Excluir direto (ver commissions_engine.cpp) —
    // "Reabrir mês" é o caminho de correção, e reabrir NÃO apaga o fechamento
    // do histórico, só destrava os serviços dele (ver historicoFechamentos.js).
    const podeReabrir = s.fechado && can('gestao_sos_servicos', 'update');
    const acoes = [
      podeEditar ? `<button class="btn-sm btn-ghost" data-editar="${s.id}">Editar</button>` : '',
      podeExcluir ? `<button class="btn-sm btn-danger" data-excluir="${s.id}">Excluir</button>` : '',
      podeReabrir ? `<button class="btn-sm btn-outline" data-reabrir="${s.fechamentoId}">Reabrir mês para editar</button>` : '',
    ].filter(Boolean).join('') || '<span class="muted">—</span>';
    // Venda, % e Data de referência ficam editáveis direto na linha (mesmo
    // padrão .df-campo do Dashboard de Fechamento) — corrigir um lançamento
    // já feito não deveria exigir abrir o modal inteiro. Fechado ou sem
    // permissão, mostra só o valor.
    const campoVenda = podeEditar
      ? `<input type="number" class="df-campo" style="width:110px;text-align:right;" min="0" step="0.01"
           data-campo-servico="${s.id}:venda" value="${s.venda}" />`
      : fmtBRL(s.venda);
    const campoPct = podeEditar
      ? `<input type="number" class="df-campo" style="width:64px;text-align:right;" min="0" max="100" step="0.01"
           data-campo-servico="${s.id}:porcentagem" value="${s.porcentagem}" />`
      : `${s.porcentagem.toLocaleString('pt-BR')}%`;
    const campoData = podeEditar
      ? `<input type="month" class="df-campo" style="width:120px;"
           data-campo-servico="${s.id}:dataReferencia" value="${s.dataReferencia}" />`
      : mesAnoLabel(s.dataReferencia);
    const checkbox = podeEditar
      ? `<input type="checkbox" class="srv-check" data-id="${s.id}" ${selecionados.has(s.id) ? 'checked' : ''} />`
      : '';
    // Status: quem pode editar troca direto na linha (marcar/desmarcar), sem
    // precisar abrir o modal — mesmo espírito dos outros campos inline
    // acima. Fechado nunca desmarca por aqui (mês já trancado).
    const statusCel = s.pago
      ? `<span class="pill pill-ok">✓ Pago</span>${
          podeEditar ? ` <button class="btn-sm btn-ghost" data-desmarcar-pago="${s.id}">Desmarcar</button>` : ''}`
      : (podeEditar
          ? `<button class="btn-sm btn-outline" data-marcar-pago="${s.id}">Marcar pago</button>`
          : '<span class="pill pill-pendente">Pendente</span>');
    return `<tr>
      <td>${checkbox}</td>
      <td class="num">${s.numero}</td>
      <td>${s.codigo ? escapeHtml(s.codigo) : '<span class="muted">—</span>'}</td>
      <td>${escapeHtml(s.condominioNome)}</td>
      <td>${s.gerenteNome ? escapeHtml(s.gerenteNome) : '<span class="muted">—</span>'}</td>
      <td>${s.parceiroNome ? escapeHtml(s.parceiroNome) : '<span class="muted">—</span>'}</td>
      <td class="num">${campoVenda}</td>
      <td class="num">${campoPct}</td>
      <td class="num"><b>${fmtBRL(s.comissao)}</b></td>
      <td>${campoData}${s.fechado ? ' <span class="pill pill-ok">Fechado</span>' : ''}</td>
      <td>${statusCel}</td>
      <td>${s.pago ? fmtDataPagamento(s.dataPagamento) : '<span class="muted">—</span>'}</td>
      <td><div class="row-actions">${acoes}</div></td></tr>`;
  }).join('');

  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openServicoModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteServico(b.dataset.excluir)));
  tbody.querySelectorAll('[data-marcar-pago]').forEach((b) =>
    b.addEventListener('click', () => abrirPagoLote([b.dataset.marcarPago])));
  tbody.querySelectorAll('[data-desmarcar-pago]').forEach((b) =>
    b.addEventListener('click', () => desmarcarPago(b.dataset.desmarcarPago)));
  tbody.querySelectorAll('[data-reabrir]').forEach((b) =>
    b.addEventListener('click', () => reabrirParaEditar(b.dataset.reabrir)));
  tbody.querySelectorAll('[data-campo-servico]').forEach((el) =>
    el.addEventListener('change', () => {
      const [id, campo] = el.dataset.campoServico.split(':');
      atualizaCampoServico(id, campo, el.value);
    }));
  tbody.querySelectorAll('.srv-check').forEach((chk) =>
    chk.addEventListener('change', () => {
      if (chk.checked) selecionados.add(chk.dataset.id); else selecionados.delete(chk.dataset.id);
      sincronizaSelecionarTodos(lista);
      atualizaBotaoPctLote();
      atualizaBotaoPagoLote();
    }));
  sincronizaSelecionarTodos(lista);
  atualizaBotaoPctLote();
  atualizaBotaoPagoLote();
}

function editaveisDe(lista) {
  return lista.filter((s) => !s.fechado && can('gestao_sos_servicos', 'update'));
}

// O checkbox do cabeçalho reflete o estado da seleção nas linhas visíveis:
// todo marcado = marcado, nenhum = desmarcado, só parte = indeterminado
// (mesmo comportamento do "selecionar tudo" de qualquer planilha).
function sincronizaSelecionarTodos(lista) {
  const chk = document.getElementById('srvSelecionarTodos');
  const editaveis = editaveisDe(lista);
  if (!editaveis.length) { chk.checked = false; chk.indeterminate = false; chk.disabled = true; return; }
  chk.disabled = false;
  const marcados = editaveis.filter((s) => selecionados.has(s.id)).length;
  chk.checked = marcados === editaveis.length;
  chk.indeterminate = marcados > 0 && marcados < editaveis.length;
}

function onSelecionarTodos(e) {
  const editaveis = editaveisDe(listaFiltrada());
  if (e.target.checked) editaveis.forEach((s) => selecionados.add(s.id));
  else editaveis.forEach((s) => selecionados.delete(s.id));
  render();
}

function atualizaBotaoPctLote() {
  const btn = document.getElementById('btnPctLote');
  const n = selecionados.size;
  btn.disabled = n === 0;
  btn.textContent = n ? `✎ Alterar % em massa (${n})` : '✎ Alterar % em massa';
}

function abrirPctLote() {
  document.getElementById('pctLoteResumo').textContent =
    `${selecionados.size} serviço(s) selecionado(s) vão receber a mesma porcentagem — útil para testar cenários sem editar um a um.`;
  document.getElementById('pctLoteValor').value = '';
  openModal('modalPctLote');
}

async function confirmarPctLote() {
  const valor = document.getElementById('pctLoteValor').value.trim();
  const nova = parseFloat(valor);
  if (!valor || Number.isNaN(nova) || nova < 0 || nova > 100) {
    toast('Informe a nova porcentagem (entre 0 e 100).', 'error');
    return;
  }
  const alvo = servicos.filter((s) => selecionados.has(s.id) && !s.fechado);
  if (!alvo.length) { toast('Nenhum serviço selecionado.', 'error'); return; }

  const btn = document.getElementById('btnConfirmarPctLote');
  btn.disabled = true;
  let ok = 0;
  const falhas = [];
  for (const s of alvo) {
    btn.textContent = `Aplicando ${ok + falhas.length + 1}/${alvo.length}...`;
    try {
      await api.updateServico({
        id: s.id, codigo: s.codigo, condominioId: s.condominioId, gerenteId: s.gerenteId,
        parceiroId: s.parceiroId, venda: s.venda, porcentagem: nova,
        dataReferencia: s.dataReferencia, observacoes: s.observacoes, createdAt: '',
        pago: s.pago, dataPagamento: s.dataPagamento,
      });
      ok++;
    } catch (e) {
      falhas.push(`#${s.numero}: ${errorText(e)}`);
    }
  }
  btn.disabled = false;
  btn.textContent = 'Aplicar';
  selecionados.clear();
  await reload();
  closeModal('modalPctLote');
  if (!falhas.length) toast(`Porcentagem aplicada a ${ok} serviço(s).`, 'success');
  else toast(`${ok} aplicado(s), ${falhas.length} falharam.`, 'error');
}

function atualizaBotaoPagoLote() {
  const btn = document.getElementById('btnPagoLote');
  const n = selecionados.size;
  btn.disabled = n === 0;
  btn.textContent = n ? `💰 Marcar pagamento em massa (${n})` : '💰 Marcar pagamento em massa';
}

// `ids` vem do botão em massa (toda a seleção) ou do "Marcar pago" de uma
// única linha — mesmo modal, mesmo fluxo, só muda quantos ids ele aplica.
function abrirPagoLote(ids) {
  pagoLoteAlvoIds = ids.filter(Boolean);
  if (!pagoLoteAlvoIds.length) { toast('Nenhum serviço selecionado.', 'error'); return; }
  document.getElementById('pagoLoteResumo').textContent = pagoLoteAlvoIds.length > 1
    ? `${pagoLoteAlvoIds.length} serviço(s) selecionado(s) vão ser marcados como pagos, todos com a mesma data.`
    : 'Este serviço vai ser marcado como pago.';
  document.getElementById('pagoLoteData').value = new Date().toISOString().slice(0, 10);
  openModal('modalPagoLote');
}

async function confirmarPagoLote() {
  const data = document.getElementById('pagoLoteData').value;
  if (!data) { toast('Informe a data de pagamento.', 'error'); return; }
  const alvo = servicos.filter((s) => pagoLoteAlvoIds.includes(s.id) && !s.fechado);
  if (!alvo.length) { toast('Nenhum serviço selecionado.', 'error'); return; }

  const btn = document.getElementById('btnConfirmarPagoLote');
  btn.disabled = true;
  let ok = 0;
  const falhas = [];
  for (const s of alvo) {
    btn.textContent = `Aplicando ${ok + falhas.length + 1}/${alvo.length}...`;
    try {
      await api.updateServico({
        id: s.id, codigo: s.codigo, condominioId: s.condominioId, gerenteId: s.gerenteId,
        parceiroId: s.parceiroId, venda: s.venda, porcentagem: s.porcentagem,
        dataReferencia: s.dataReferencia, observacoes: s.observacoes, createdAt: '',
        pago: true, dataPagamento: data,
      });
      ok++;
    } catch (e) {
      falhas.push(`#${s.numero}: ${errorText(e)}`);
    }
  }
  btn.disabled = false;
  btn.textContent = 'Marcar como pago';
  selecionados.clear();
  pagoLoteAlvoIds = [];
  await reload();
  closeModal('modalPagoLote');
  if (!falhas.length) toast(`${ok} serviço(s) marcado(s) como pago.`, 'success');
  else toast(`${ok} aplicado(s), ${falhas.length} falharam.`, 'error');
}

async function desmarcarPago(id) {
  const s = servicos.find((x) => x.id === id);
  if (!s || s.fechado) return;
  if (!confirm('Desmarcar o pagamento deste serviço? Ele volta a ficar de fora da distribuição de comissão até ser pago de novo.')) return;
  try {
    await api.updateServico({
      id: s.id, codigo: s.codigo, condominioId: s.condominioId, gerenteId: s.gerenteId,
      parceiroId: s.parceiroId, venda: s.venda, porcentagem: s.porcentagem,
      dataReferencia: s.dataReferencia, observacoes: s.observacoes, createdAt: '',
      pago: false, dataPagamento: '',
    });
    await reload();
    toast('Pagamento desmarcado.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function atualizaCampoServico(id, campo, valorBruto) {
  const s = servicos.find((x) => x.id === id);
  if (!s) return;
  let valor = valorBruto;
  if (campo === 'venda' || campo === 'porcentagem') {
    valor = parseFloat(valorBruto);
    if (Number.isNaN(valor) || valor < 0 || (campo === 'porcentagem' && valor > 100)) {
      toast(campo === 'porcentagem' ? 'A porcentagem precisa estar entre 0 e 100.' : 'Informe uma venda válida.', 'error');
      render();
      return;
    }
  } else if (campo === 'dataReferencia' && !valor) {
    toast('Informe o mês de referência.', 'error');
    render();
    return;
  }
  try {
    await api.updateServico({
      id: s.id, codigo: s.codigo, condominioId: s.condominioId, gerenteId: s.gerenteId,
      parceiroId: s.parceiroId, venda: s.venda, porcentagem: s.porcentagem,
      dataReferencia: s.dataReferencia, observacoes: s.observacoes, createdAt: '',
      pago: s.pago, dataPagamento: s.dataPagamento,
      [campo]: valor,
    });
    await reload();
    toast('Serviço atualizado.', 'success');
  } catch (e) {
    toast('Erro: ' + errorText(e), 'error');
    render();
  }
}

async function reabrirParaEditar(fechamentoId) {
  if (!confirm('Reabrir o mês inteiro deste fechamento para poder editar este serviço? Os demais serviços que foram fechados junto também voltam a ficar em aberto — o fechamento continua no histórico, só marcado como reaberto.')) return;
  try {
    await api.reabrirFechamento(fechamentoId);
    await reload();
    toast('Mês reaberto — os serviços já podem ser editados.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

// Um serviço em aberto pode apontar para um condomínio/gerente/parceiro que
// foi excluído depois (condomínio saiu da administradora, por exemplo) — o
// nome fica congelado no próprio serviço (ver resolverNomes em
// commissions_engine.cpp). Aqui, se o id gravado não está mais na lista viva,
// injeta uma opção extra "(excluído)" com o nome congelado, pré-selecionada:
// assim editar OUTRO campo do serviço (venda, porcentagem...) não troca essa
// referência sem querer, e o <select> nunca fica "vazio" silenciosamente.
function opcaoCongelada(id, nome, lista) {
  if (!id || lista.some((x) => x.id === id)) return '';
  return `<option value="${id}">${escapeHtml(nome)} (excluído)</option>`;
}

function popularSelects(selecionado) {
  const selCond = document.getElementById('srvCondominioId');
  const selGer = document.getElementById('srvGerenteId');
  const selPar = document.getElementById('srvParceiroId');
  selCond.innerHTML = '<option value="">Selecione...</option>' + [...condominios]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((c) => `<option value="${c.id}">${escapeHtml(c.nome)}</option>`).join('') +
    (selecionado ? opcaoCongelada(selecionado.condominioId, selecionado.condominioNome, condominios) : '');
  selGer.innerHTML = '<option value="">— nenhum —</option>' + [...gerentes]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((g) => `<option value="${g.id}">${escapeHtml(g.nome)}</option>`).join('') +
    (selecionado ? opcaoCongelada(selecionado.gerenteId, selecionado.gerenteNome, gerentes) : '');
  selPar.innerHTML = '<option value="">— nenhum —</option>' + [...parceiros]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((p) => `<option value="${p.id}">${escapeHtml(p.nome)}</option>`).join('') +
    (selecionado ? opcaoCongelada(selecionado.parceiroId, selecionado.parceiroNome, parceiros) : '');
  if (selecionado) {
    selCond.value = selecionado.condominioId;
    selGer.value = selecionado.gerenteId || '';
    selPar.value = selecionado.parceiroId || '';
  }
}

function atualizaComissaoPrevista() {
  const venda = parseFloat(document.getElementById('srvVenda').value) || 0;
  const pct = parseFloat(document.getElementById('srvPorcentagem').value) || 0;
  document.getElementById('srvComissaoPrevista').textContent = fmtBRL(venda * pct / 100);
}

// O campo de data de pagamento só é OBRIGATÓRIO (e só faz sentido) com
// "Pago? Sim" marcado — com "Não", o campo fica desabilitado e limpo (mesmo
// critério do backend: dataPagamento é sempre ignorada quando pago=false).
function atualizaDataPagamentoObrigatoria() {
  const pago = document.getElementById('srvPagoSim').checked;
  document.getElementById('srvDataPagamento').disabled = !pago;
  document.getElementById('srvDataPagamentoObrig').style.display = pago ? '' : 'none';
  if (!pago) document.getElementById('srvDataPagamento').value = '';
}

function openServicoModal(id) {
  if (!condominios.length) { toast('Cadastre um condomínio primeiro (Cadastro de Condomínios).', 'error'); return; }
  const s = id ? servicos.find((x) => x.id === id) : null;
  document.getElementById('modalServicoTitulo').textContent = id ? 'Editar Serviço' : 'Novo Serviço';
  document.getElementById('srvId').value = id || '';
  document.getElementById('srvCodigo').value = s ? s.codigo : '';
  popularSelects(s);
  document.getElementById('srvVenda').value = s ? s.venda : '';
  document.getElementById('srvPorcentagem').value = s ? s.porcentagem : (porcentagemPadrao || '');
  document.getElementById('srvDataReferencia').value = s ? s.dataReferencia : new Date().toISOString().slice(0, 7);
  document.getElementById('srvObservacoes').value = s ? s.observacoes : '';
  document.getElementById('srvPagoSim').checked = !!(s && s.pago);
  document.getElementById('srvPagoNao').checked = !(s && s.pago);
  document.getElementById('srvDataPagamento').value = s ? s.dataPagamento : '';
  atualizaDataPagamentoObrigatoria();
  atualizaComissaoPrevista();
  openModal('modalServico');
}

async function saveServico() {
  const id = document.getElementById('srvId').value;
  const condominioId = document.getElementById('srvCondominioId').value;
  const dataReferencia = document.getElementById('srvDataReferencia').value;
  if (!condominioId) { toast('Selecione o condomínio.', 'error'); return; }
  if (!dataReferencia) { toast('Informe o mês de referência.', 'error'); return; }
  const pago = document.getElementById('srvPagoSim').checked;
  const dataPagamento = document.getElementById('srvDataPagamento').value;
  if (pago && !dataPagamento) { toast('Informe a data de pagamento.', 'error'); return; }
  const payload = {
    id: id || uid('srv_'),
    codigo: document.getElementById('srvCodigo').value.trim(),
    condominioId,
    gerenteId: document.getElementById('srvGerenteId').value,
    parceiroId: document.getElementById('srvParceiroId').value,
    venda: parseFloat(document.getElementById('srvVenda').value) || 0,
    porcentagem: parseFloat(document.getElementById('srvPorcentagem').value) || 0,
    dataReferencia,
    observacoes: document.getElementById('srvObservacoes').value.trim(),
    createdAt: id ? '' : nowIso(),
    pago, dataPagamento: pago ? dataPagamento : '',
  };
  try {
    if (id) await api.updateServico(payload);
    else await api.createServico(payload);
    await reload();
    closeModal('modalServico');
    toast('Serviço salvo.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteServico(id) {
  const s = servicos.find((x) => x.id === id);
  if (!s) return;
  if (!confirm(`Excluir o serviço #${s.numero} (${s.condominioNome})?`)) return;
  try {
    await api.deleteServico(id);
    await reload();
    toast('Serviço excluído.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

// Relatório impresso: filtro PRÓPRIO, separado do filtro de mês único da
// tela — o pedido foi imprimir "os valores de um parceiro específico e de
// alguns meses" (um intervalo, não um mês só).
function abrirFiltroRelatorio() {
  const sel = document.getElementById('impSrvRelParceiro');
  sel.innerHTML = '<option value="">Todos</option>' + [...parceiros]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((p) => `<option value="${p.id}">${escapeHtml(p.nome)}</option>`).join('');
  document.getElementById('impSrvRelDe').value = '';
  document.getElementById('impSrvRelAte').value = '';
  openModal('modalImprimirServicos');
}

function gerarRelatorio() {
  const parceiroId = document.getElementById('impSrvRelParceiro').value;
  const de = document.getElementById('impSrvRelDe').value;
  const ate = document.getElementById('impSrvRelAte').value;
  if (de && ate && de > ate) { toast('O mês "de" não pode ser depois do "até".', 'error'); return; }

  // "YYYY-MM" compara certo como string — não precisa converter pra data.
  const lista = servicos.filter((s) => {
    if (parceiroId && s.parceiroId !== parceiroId) return false;
    if (de && s.dataReferencia < de) return false;
    if (ate && s.dataReferencia > ate) return false;
    return true;
  });
  if (!lista.length) { toast('Nenhum serviço encontrado com este filtro.', 'error'); return; }

  const parceiroNome = parceiroId ? (parceiros.find((p) => p.id === parceiroId) || {}).nome : 'Todos os parceiros';
  const periodo = de || ate ? `${de ? mesAnoLabel(de) : 'início'} a ${ate ? mesAnoLabel(ate) : 'hoje'}` : 'todos os meses';
  printDocument(buildServicosDoc(lista, `${parceiroNome} · ${periodo}`));
  closeModal('modalImprimirServicos');
}
