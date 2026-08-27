import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtBRL, paraBusca } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { initImportarServicos } from './servicosImportar.js';
import { printDocument, buildServicosDoc } from '../print.js';
import { enableRowSelection } from '../components/tableTools.js';
import { bindMoneyMask, setMoneyMaskedValue, parseMoneyMasked } from '../masks.js';

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
    bindMoneyMask(document.getElementById('srvVenda'));
    ['srvVenda', 'srvPorcentagem'].forEach((id) =>
      document.getElementById(id).addEventListener('input', atualizaComissaoPrevista));
    document.getElementById('btnImprimirServicos').addEventListener('click', abrirFiltroRelatorio);
    document.getElementById('btnGerarRelatorioServicos').addEventListener('click', gerarRelatorio);
    document.getElementById('impSrvRelBuscaParceiro').addEventListener('input', renderChipsRelatorio);
    document.getElementById('impSrvRelMarcarTodos').addEventListener('click', () => {
      parceiros.forEach((p) => relParceirosSelecionados.add(p.id));
      renderChipsRelatorio();
    });
    document.getElementById('impSrvRelLimparTodos').addEventListener('click', () => {
      relParceirosSelecionados.clear();
      renderChipsRelatorio();
    });
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

// ---- problemas de carteira
//
// O "Recebido" de cada gerente no Dashboard de Fechamento soma a comissão dos
// serviços dos condomínios NA CARTEIRA dele — não dos serviços que trazem o
// nome dele no campo Gerente (ver commissions_engine.cpp). Como os dois lados
// são cadastrados em telas diferentes, eles divergem calados: a comissão vai
// pro Recebido de outra pessoa, ou de ninguém, e nada na tela denuncia. Só dá
// pra perceber muito depois, conferindo o fechamento à mão — foi assim que três
// condomínios ficaram fora da carteira de um gerente por meses e derrubaram o
// Recebido dele pela metade. Marcar aqui, na tela onde o serviço é lançado, é o
// único momento em que dá pra corrigir antes do estrago. Ver situacaoCarteira
// logo abaixo para os casos.

// Dono(s) da carteira de cada condomínio. A carteira chega como
// `condominios: [{id, nome}]` (ver gerenteToJson em api.cpp) — mesmo caminho
// que Carteiras e Gerentes já usam. Cuidado: o simulador de desenvolvimento
// também carrega um `condominioIds`, que o backend real NÃO manda — usar aquele
// campo deixaria todo condomínio vermelho no aplicativo instalado.
//
// É uma LISTA de donos, não um dono só, porque gerente_condominios é uma tabela
// N:N sem trava de exclusividade (ver db.cpp): nada impede o mesmo condomínio
// de estar em duas carteiras. Quando isso acontece a comissão dele é somada no
// Recebido dos DOIS gerentes (commissions_engine.cpp percorre gerente por
// gerente), inflando a distribuição — por isso esse caso também é sinalizado.
function donosDeCarteiraPorCondominio() {
  const mapa = new Map();
  for (const g of gerentes) {
    for (const c of g.condominios || []) {
      if (!mapa.has(c.id)) mapa.set(c.id, []);
      mapa.get(c.id).push({ id: g.id, nome: g.nome });
    }
  }
  return mapa;
}

function condominioExiste(condominioId) {
  return condominios.some((c) => c.id === condominioId);
}

// Três situações, do ponto de vista de quem vai receber a comissão:
//   'ok'          — nada a corrigir
//   'sem'         — não está em carteira nenhuma: a comissão não entra no
//                   Recebido de ninguém
//   'divergente'  — está em carteira, mas não na do gerente lançado no serviço
//                   (ou está em mais de uma): o dinheiro vai pro Recebido de
//                   outra pessoa, ou de duas
//
// Serviço preso a condomínio já EXCLUÍDO do cadastro nunca é sinalizado: o nome
// fica congelado no registro, não há o que corrigir em Carteiras, e apontar
// para algo insolúvel só ensina a ignorar o aviso.
//
// Serviço SEM gerente lançado não gera divergência: sem os dois lados não há
// como dizer que a carteira está "errada" — e o Recebido vai pro dono da
// carteira normalmente, que é o comportamento esperado.
function situacaoCarteira(s, donos) {
  if (!s.condominioId || !condominioExiste(s.condominioId)) return 'ok';
  const lista = donos.get(s.condominioId) || [];
  if (!lista.length) return 'sem';
  if (lista.length > 1) return 'divergente';
  if (s.gerenteId && lista[0].id !== s.gerenteId) return 'divergente';
  return 'ok';
}

function motivoDivergencia(s, donos) {
  const lista = donos.get(s.condominioId) || [];
  if (lista.length > 1) {
    return `Está na carteira de mais de um gerente (${lista.map((d) => d.nome).join(', ')}) — `
      + 'a comissão deste serviço é contada para todos eles no Dashboard de Fechamento.';
  }
  return `Está na carteira de ${lista[0].nome}, mas o serviço está lançado no nome de `
    + `${s.gerenteNome || '(sem gerente)'} — a comissão vai para o Recebido de ${lista[0].nome}.`;
}

function condominioCelHtml(s, donos) {
  const nome = escapeHtml(s.condominioNome);
  const sit = situacaoCarteira(s, donos);
  if (sit === 'ok') return nome;
  const titulo = sit === 'sem'
    ? 'Este condomínio não está na carteira de nenhum gerente — a comissão dele não entra no Recebido de ninguém no Dashboard de Fechamento.'
    : motivoDivergencia(s, donos);
  const classe = sit === 'sem' ? 'sem-carteira' : 'carteira-divergente';
  return `<span class="${classe}" title="${escapeHtml(titulo)} Corrija em Gestão SOS > Carteiras.">⚠ ${nome}</span>`;
}

// O aviso conta os condomínios DISTINTOS (não os serviços): o que precisa ser
// corrigido em Carteiras é o condomínio, e um mesmo condomínio costuma ter
// vários serviços no mês — contar serviços inflaria o número sem ajudar.
// A lista sai completa, sem "e mais N": é exatamente o que a pessoa vai
// procurar na tela de Carteiras, e cortar obrigaria a voltar aqui pra ver o
// resto. Considera só os serviços VISÍVEIS no filtro atual, pra não acusar
// problema em mês nenhum quando a pessoa está olhando um mês específico.
function renderAvisoSemCarteira(lista, donos) {
  const box = document.getElementById('servicosSemCarteiraAviso');
  const distintos = (sit) => [...new Map(lista.filter((s) => situacaoCarteira(s, donos) === sit)
    .map((s) => [s.condominioId, s])).values()]
    .sort((a, b) => String(a.condominioNome).localeCompare(String(b.condominioNome), 'pt-BR'));

  const sem = distintos('sem');
  const div = distintos('divergente');
  if (!sem.length && !div.length) {
    box.style.display = 'none';
    box.innerHTML = '';
    return;
  }

  const blocos = [];
  if (sem.length) {
    blocos.push(`<div><span class="sem-carteira">⚠ ${sem.length} condomínio(s) fora de qualquer
      carteira:</span> ${sem.map((s) => escapeHtml(s.condominioNome)).join(' · ')}.
      A comissão desses serviços entra no Arrecadado do mês, mas não no <b>Recebido</b> de nenhum gerente.</div>`);
  }
  if (div.length) {
    blocos.push(`<div style="margin-top:6px;"><span class="carteira-divergente">⚠ ${div.length}
      condomínio(s) na carteira de outro gerente:</span>
      ${div.map((s) => escapeHtml(s.condominioNome)).join(' · ')}.
      A comissão vai para o <b>Recebido</b> de quem tem o condomínio na carteira, não de quem está
      lançado no serviço.</div>`);
  }
  box.style.display = '';
  box.innerHTML = `${blocos.join('')}<div style="margin-top:6px;">Corrija em
    <b>Gestão SOS &gt; Carteiras</b>.</div>`;
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

  const donosCarteira = donosDeCarteiraPorCondominio();
  renderAvisoSemCarteira(lista, donosCarteira);

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
      <td>${condominioCelHtml(s, donosCarteira)}</td>
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
  const venda = parseMoneyMasked(document.getElementById('srvVenda').value);
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
  setMoneyMaskedValue(document.getElementById('srvVenda'), s ? s.venda : 0);
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
    venda: parseMoneyMasked(document.getElementById('srvVenda').value),
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
// tela — o pedido foi imprimir "os valores de um ou mais parceiros e de
// alguns meses" (um intervalo, não um mês só). Parceiro agora é multi-
// seleção por chip (mesmo padrão de Setorização/Cadastro de empresas) —
// nenhum marcado equivale a "todos".
let relParceirosSelecionados = new Set();
let relBuscaParceiro = '';

function abrirFiltroRelatorio() {
  relParceirosSelecionados = new Set();
  relBuscaParceiro = '';
  document.getElementById('impSrvRelBuscaParceiro').value = '';
  const selGerRel = document.getElementById('impSrvRelGerente');
  selGerRel.innerHTML = '<option value="">Todos</option>' + [...gerentes]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((g) => `<option value="${g.id}">${escapeHtml(g.nome)}</option>`).join('');
  document.getElementById('impSrvRelDe').value = '';
  document.getElementById('impSrvRelAte').value = '';
  document.getElementById('impSrvRelSoComPct').checked = false;
  renderChipsRelatorio();
  openModal('modalImprimirServicos');
}

function renderChipsRelatorio() {
  relBuscaParceiro = document.getElementById('impSrvRelBuscaParceiro').value;
  const termo = paraBusca(relBuscaParceiro).trim();
  const host = document.getElementById('impSrvRelParceiros');
  const sorted = [...parceiros]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .filter((p) => !termo || paraBusca(p.nome).includes(termo));

  if (!sorted.length) {
    host.innerHTML = '<span class="muted">Nenhum parceiro encontrado.</span>';
    return;
  }
  host.innerHTML = sorted.map((p) => `
    <button type="button" class="chip-toggle ${relParceirosSelecionados.has(p.id) ? 'on' : ''}"
            data-parceiro="${p.id}" aria-pressed="${relParceirosSelecionados.has(p.id)}">
      ${escapeHtml(p.nome)}
    </button>`).join('');
  host.querySelectorAll('[data-parceiro]').forEach((btn) => btn.addEventListener('click', () => {
    const id = btn.dataset.parceiro;
    if (relParceirosSelecionados.has(id)) relParceirosSelecionados.delete(id);
    else relParceirosSelecionados.add(id);
    btn.classList.toggle('on', relParceirosSelecionados.has(id));
    btn.setAttribute('aria-pressed', relParceirosSelecionados.has(id));
  }));
}

function gerarRelatorio() {
  const gerenteId = document.getElementById('impSrvRelGerente').value;
  const de = document.getElementById('impSrvRelDe').value;
  const ate = document.getElementById('impSrvRelAte').value;
  const soComPct = document.getElementById('impSrvRelSoComPct').checked;
  if (de && ate && de > ate) { toast('O mês "de" não pode ser depois do "até".', 'error'); return; }

  // "YYYY-MM" compara certo como string — não precisa converter pra data.
  const lista = servicos.filter((s) => {
    if (gerenteId && s.gerenteId !== gerenteId) return false;
    if (relParceirosSelecionados.size && !relParceirosSelecionados.has(s.parceiroId)) return false;
    if (de && s.dataReferencia < de) return false;
    if (ate && s.dataReferencia > ate) return false;
    if (soComPct && !(s.porcentagem > 0)) return false;
    return true;
  });
  if (!lista.length) { toast('Nenhum serviço encontrado com este filtro.', 'error'); return; }

  const gerenteNome = gerenteId ? (gerentes.find((g) => g.id === gerenteId) || {}).nome : '';
  const parceiroNome = relParceirosSelecionados.size
    ? [...relParceirosSelecionados].map((id) => (parceiros.find((p) => p.id === id) || {}).nome)
        .filter(Boolean).sort((a, b) => a.localeCompare(b, 'pt-BR')).join(', ')
    : 'Todos os parceiros';
  const periodo = de || ate ? `${de ? mesAnoLabel(de) : 'início'} a ${ate ? mesAnoLabel(ate) : 'hoje'}` : 'todos os meses';
  const pctLabel = soComPct ? ' · só com comissão' : '';
  const titulo = (gerenteNome ? `${gerenteNome} · ` : '') + `${parceiroNome} · ${periodo}${pctLabel}`;
  printDocument(buildServicosDoc(lista, titulo));
  closeModal('modalImprimirServicos');
}
