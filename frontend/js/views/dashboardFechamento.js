import { api, errorText } from '../api.js';
import { escapeHtml, fmtBRL, fmtPct, uid, nowIso } from '../format.js';
import { toast } from '../components/toast.js';
import { printDocument, buildDashboardFechamentoDoc } from '../print.js';
import { distribuicaoDashboard, tileDistribuido } from '../sosRateio.js';

// Gestão SOS > Dashboard de Fechamento — replica mês após mês as fórmulas
// SOMASES da planilha real do usuário. Nunca calcula nada aqui: cada
// "Puxar"/edição chama api.montarDashboard (commissions_engine.cpp), que é
// a fonte única da verdade da fórmula — este arquivo só monta o payload,
// mostra o resultado e deixa editar Carteira/Eficácia/(%)/A+P por gerente.
//
// overrides guarda, por gerente, só os campos que o usuário JÁ tocou nesta
// sessão de edição — a primeira edição de QUALQUER campo de uma linha
// "trava" os outros dois no valor atual calculado (senão editar só a
// Eficácia, por exemplo, resetaria Carteira/(%) para os padrões da entrada
// vazia). Ver onCampoAlterado.
const MESES = [
  '', 'Janeiro', 'Fevereiro', 'Março', 'Abril', 'Maio', 'Junho',
  'Julho', 'Agosto', 'Setembro', 'Outubro', 'Novembro', 'Dezembro',
];

let mesAtual = '';
let overrides = {};       // gerenteId -> { porcentagem, eficacia, carteira }
let overridesManuais = new Set();  // "gerenteId:campo" — só pra destacar visualmente o que foi digitado à mão
let dashAtual = null;     // último resultado de api.montarDashboard
let wired = false;

export async function initDashboardFechamento() {
  if (!wired) {
    wired = true;
    document.getElementById('btnDfPuxar').addEventListener('click', puxar);
    document.getElementById('btnDfRepetirAnterior').addEventListener('click', repetirMesAnterior);
    document.getElementById('btnDfSalvar').addEventListener('click', salvar);
    document.getElementById('btnDfImprimir').addEventListener('click', imprimir);
    document.getElementById('dfGerentesTbody').addEventListener('change', onCampoAlterado);
  }
  await reload();
}

export async function reload() {
  const mesInput = document.getElementById('dfMes');
  if (!mesInput.value) mesInput.value = new Date().toISOString().slice(0, 7);
}

async function puxar() {
  const mes = document.getElementById('dfMes').value;
  if (!mes) { toast('Escolha o mês de referência.', 'error'); return; }
  mesAtual = mes;
  overrides = {};
  overridesManuais = new Set();
  await recompute();
}

// "Repetir o do mês anterior (Recomendado)": traz Carteira/Eficácia/(%)/A+P
// do ÚLTIMO retrato salvo antes deste mês — o caso comum é a carteira e a
// meta não mudarem de um mês pro outro, só a produção.
async function repetirMesAnterior() {
  const mes = document.getElementById('dfMes').value;
  if (!mes) { toast('Escolha o mês de referência.', 'error'); return; }
  mesAtual = mes;
  try {
    const salvos = await api.listDashboards();
    const anterior = salvos.filter((d) => d.mesReferencia < mes)
      .sort((a, b) => (a.mesReferencia < b.mesReferencia ? 1 : -1))[0];
    if (!anterior) {
      toast('Nenhum fechamento salvo antes deste mês — puxando do zero.', 'error');
      overrides = {};
    } else {
      overrides = {};
      for (const g of (anterior.dados.gerentes || [])) {
        overrides[g.gerenteId] = {
          porcentagem: g.porcentagem, eficacia: g.eficacia, carteira: g.carteira,
        };
      }
      toast(`Repetindo configuração de ${anterior.mesReferencia}.`, 'success');
    }
    overridesManuais = new Set();
    await recompute();
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

function entradaAtual() {
  return {
    mesReferencia: mesAtual,
    gerentes: Object.entries(overrides).map(([gerenteId, o]) => ({ gerenteId, ...o })),
  };
}

async function recompute() {
  try {
    dashAtual = await api.montarDashboard(entradaAtual());
    render();
  } catch (e) {
    toast('Erro ao montar o dashboard: ' + errorText(e), 'error');
  }
}

function onCampoAlterado(e) {
  const el = e.target;
  const gerenteId = el.dataset.gerente;
  const campo = el.dataset.campo;
  if (!gerenteId || !campo) return;

  const linhaAtual = dashAtual.gerentes.find((g) => g.gerenteId === gerenteId);
  const base = overrides[gerenteId] || {
    porcentagem: linhaAtual.porcentagem, eficacia: linhaAtual.eficacia,
    carteira: linhaAtual.carteira,
  };
  const valor = parseFloat(el.value) || 0;
  overrides[gerenteId] = { ...base, [campo]: valor };
  overridesManuais.add(`${gerenteId}:${campo}`);
  recompute();
}

function filtroLabel() {
  const [ano, mes] = mesAtual.split('-');
  return mes ? `${MESES[Number(mes)]}/${ano}` : mesAtual;
}

function render() {
  document.getElementById('dfVazio').style.display = dashAtual ? 'none' : '';
  document.getElementById('dfConteudo').style.display = dashAtual ? '' : 'none';
  document.getElementById('btnDfSalvar').disabled = !dashAtual;
  document.getElementById('btnDfImprimir').disabled = !dashAtual;
  if (!dashAtual) return;

  const d = dashAtual;
  document.getElementById('dfStatGrid').innerHTML = [
    { label: 'Arrecadado', value: fmtBRL(d.arrecadado) },
    { label: 'FL', value: fmtBRL(d.flLucro) },
    { label: 'Liberado p/ comissão (Gerentes)', value: fmtBRL(d.liberadoParaComissao) },
    { label: 'Gerência líquido', value: fmtBRL(d.gerenciaLiquido) },
    { label: 'Retido para a FL', value: fmtBRL(d.retido) },
    tileDistribuido(distribuicaoDashboard(d)),
  ].map((t) => `<div class="stat-tile ${t.classeTile || ''}"><div class="label">${escapeHtml(t.label)}</div>
    <div class="value ${t.classeValor || ''}">${t.value}</div>${
      t.foot ? `<div class="foot">${escapeHtml(t.foot)}</div>` : ''}</div>`).join('');

  // Arredonda com PONTO decimal (round2(62.833) -> 62.83) — nunca
  // toLocaleString aqui: um <input type="number"> só aceita ponto, e
  // "62,83" (vírgula do pt-BR) é um valor inválido pro atributo value, que o
  // navegador simplesmente não mostra (campo aparece em branco). É por isso
  // que só o gerente que NÃO bate 100% da meta via o campo sumir: eficácia
  // exata em 100 é redonda, qualquer fração vinha formatada com vírgula.
  const round2 = (n) => Math.round(n * 100) / 100;

  document.getElementById('dfGerentesTbody').innerHTML = d.gerentes.map((g) => {
    const campo = (nome, valor, casas) => {
      const destacado = overridesManuais.has(`${g.gerenteId}:${nome}`);
      return `<input type="number" min="0" step="${casas === 0 ? '1' : '0.01'}" value="${valor}"
        data-gerente="${g.gerenteId}" data-campo="${nome}" class="df-campo ${destacado ? 'is-override' : ''}"
        style="width:${nome === 'carteira' ? '64' : '76'}px;text-align:right;" />`;
    };
    return `<tr>
      <td><b>${escapeHtml(g.gerenteNome)}</b></td>
      <td class="num">${fmtBRL(g.recebido)}</td>
      <td class="num">${campo('carteira', g.carteira, 0)}</td>
      <td class="num">${fmtBRL(g.meta)}</td>
      <td class="num">${campo('eficacia', round2(g.eficacia))}%</td>
      <td class="num">${campo('porcentagem', round2(g.porcentagem))}%</td>
      <td class="num">${fmtBRL(g.descontos)}</td>
      <td class="num"><b>${fmtBRL(g.comissao)}</b></td>
      <td class="num">${fmtBRL(g.retido)}</td>
    </tr>`;
  }).join('');
  document.getElementById('dfGerentesTfoot').innerHTML = `<tr style="font-weight:700;">
    <td colspan="7">Total</td>
    <td class="num">${fmtBRL(d.gerenciaLiquido)}</td>
    <td class="num">${fmtBRL(d.retido)}</td></tr>`;

  document.getElementById('dfSuprimentosTbody').innerHTML = d.distribuicaoCompras.length
    ? d.distribuicaoCompras.map((l) => `<tr><td>${escapeHtml(l.rotulo)}</td>
        <td class="num">${fmtBRL(l.valor)}</td></tr>`).join('')
    : '<tr class="empty-row"><td colspan="2">Nada a distribuir neste mês.</td></tr>';

  document.getElementById('dfEmpresasTbody').innerHTML = d.empresas.length
    ? d.empresas.map((e) => `<tr><td>${escapeHtml(e.empresaNome)}</td>
        <td class="num">${fmtBRL(e.recebidos)}</td></tr>`).join('')
    : '<tr class="empty-row"><td colspan="2">Nenhuma parceira cadastrada.</td></tr>';

  document.getElementById('dfDeltaTbody').innerHTML = d.deltaSindicos.length
    ? d.deltaSindicos.map((x) => `<tr>
        <td>${escapeHtml(x.condominioNome)}</td>
        <td>${x.sindico ? escapeHtml(x.sindico) : '<span class="muted">—</span>'}</td>
        <td>${x.gerenteNome ? escapeHtml(x.gerenteNome) : '<span class="muted">—</span>'}</td>
        <td class="num">${fmtBRL(x.venda)}</td>
        <td class="num">${fmtPct(x.porcentagem / 100, 0)}</td>
        <td class="num">${fmtBRL(x.comissao)}</td></tr>`).join('')
    : '<tr class="empty-row"><td colspan="6">Nenhum lançamento de Delta Síndicos neste mês.</td></tr>';
  document.getElementById('dfDeltaTfoot').innerHTML = d.deltaSindicos.length ? `<tr style="font-weight:700;">
    <td colspan="3">Total</td>
    <td class="num">${fmtBRL(d.deltaSindicos.reduce((s, x) => s + x.venda, 0))}</td>
    <td></td>
    <td class="num">${fmtBRL(d.deltaSindicos.reduce((s, x) => s + x.comissao, 0))}</td></tr>` : '';

  document.getElementById('dfObservacoes').value = d.observacoes || '';
}

async function salvar() {
  if (!dashAtual) return;
  const snap = {
    id: uid('dash_'),
    mesReferencia: mesAtual,
    dados: { ...dashAtual, observacoes: document.getElementById('dfObservacoes').value.trim() },
    observacoes: document.getElementById('dfObservacoes').value.trim(),
    geradoEm: nowIso(),
    createdAt: nowIso(),
  };
  try {
    await api.salvarDashboard(snap);
    toast(`Fechamento de ${filtroLabel()} salvo no histórico.`, 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

function imprimir() {
  if (!dashAtual) return;
  printDocument(buildDashboardFechamentoDoc(dashAtual, filtroLabel()));
}
