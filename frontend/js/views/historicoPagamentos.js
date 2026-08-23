import { api, errorText } from '../api.js';
import { escapeHtml, fmtBRL } from '../format.js';
import { toast } from '../components/toast.js';
import { printDocument, buildPagamentosDoc } from '../print.js';
import { abrirMes as abrirMesEmProgramarPagamento } from './programarPagamento.js';

// Gestão SOS > Histórico de pagamentos — os meses com pagamento FECHADO
// (ver Programar pagamento). Diferente dos outros históricos do sistema
// (Fechamentos, Dashboard de Fechamento), este é editável de propósito: uma
// Chave PIX errada ou um valor autorizado por engano precisa dar pra
// corrigir depois, sem virar um segundo registro pro mesmo mês (ver
// PagamentoSalvo em commissions_engine.hpp). "Editar" reabre o mês em
// Programar pagamento, que é a mesma tela usada pra fechar.

const MESES = [
  '', 'Janeiro', 'Fevereiro', 'Março', 'Abril', 'Maio', 'Junho',
  'Julho', 'Agosto', 'Setembro', 'Outubro', 'Novembro', 'Dezembro',
];

let pagamentos = [];
let busca = '';
let wired = false;

export async function initHistoricoPagamentos() {
  if (!wired) {
    wired = true;
    document.getElementById('hpBusca').addEventListener('input', (e) => {
      busca = e.target.value.toLowerCase();
      render();
    });
    document.getElementById('historicoPagamentosTbody').addEventListener('click', onClickAcao);
  }
  await reload();
}

export async function reload() {
  try {
    pagamentos = await api.listPagamentosSos();
  } catch (e) {
    toast('Erro ao carregar Histórico de pagamentos: ' + errorText(e), 'error');
    pagamentos = [];
  }
  render();
}

function mesAnoLabel(yyyymm) {
  if (!yyyymm || yyyymm.length !== 7) return '—';
  const [ano, mes] = yyyymm.split('-');
  return `${MESES[Number(mes)]}/${ano}`;
}

function listaFiltrada() {
  if (!busca) return pagamentos;
  return pagamentos.filter((p) =>
    [mesAnoLabel(p.mesReferencia), p.observacoes].filter(Boolean).join(' ').toLowerCase().includes(busca));
}

function render() {
  const lista = listaFiltrada();
  document.getElementById('histPagamentosTotal').textContent = pagamentos.length;
  document.getElementById('histPagamentosFiltrados').textContent =
    busca ? ` · ${lista.length} no filtro` : '';

  const tbody = document.getElementById('historicoPagamentosTbody');
  if (!lista.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="7">${
      pagamentos.length ? 'Nenhum pagamento encontrado para esta busca.'
        : 'Nenhum pagamento fechado ainda — feche um mês em Programar pagamento.'}</td></tr>`;
    return;
  }

  tbody.innerHTML = lista.map((p) => `<tr>
      <td><b>${mesAnoLabel(p.mesReferencia)}</b></td>
      <td class="num">${fmtBRL(p.dados.totalGerentes)}</td>
      <td class="num">${fmtBRL(p.dados.totalSuprimentos)}</td>
      <td class="num">${fmtBRL(p.dados.totalDelta)}</td>
      <td>${p.fechadoEm ? new Date(p.fechadoEm).toLocaleString('pt-BR') : '—'}</td>
      <td>${p.observacoes ? escapeHtml(p.observacoes) : '<span class="muted">—</span>'}</td>
      <td>
        <button type="button" class="btn-sm btn-outline" data-editar="${p.id}">Editar</button>
        <button type="button" class="btn-sm btn-outline" data-imprimir="${p.id}">🖨 Imprimir</button>
      </td>
    </tr>`).join('');
}

async function onClickAcao(e) {
  const btnEditar = e.target.closest('[data-editar]');
  if (btnEditar) {
    const p = pagamentos.find((x) => x.id === btnEditar.dataset.editar);
    if (!p) return;
    document.querySelector('[data-view="programarPagamento"]')?.click();
    await abrirMesEmProgramarPagamento(p.mesReferencia);
    return;
  }
  const btnImprimir = e.target.closest('[data-imprimir]');
  if (btnImprimir) {
    const p = pagamentos.find((x) => x.id === btnImprimir.dataset.imprimir);
    if (!p) return;
    // Mesmo critério de Programar pagamento: só as linhas autorizadas, com
    // a quantia exata autorizada — nunca as não autorizadas.
    const autorizados = p.dados.linhas.filter((l) => l.autorizado);
    if (!autorizados.length) { toast('Nenhum pagamento autorizado neste registro.', 'error'); return; }
    printDocument(buildPagamentosDoc(autorizados, mesAnoLabel(p.mesReferencia)));
  }
}
