import { api, errorText } from '../api.js';
import { escapeHtml, fmtBRL, uid, nowIso } from '../format.js';
import { toast } from '../components/toast.js';
import { printDocument, buildPagamentosDoc } from '../print.js';

// Gestão SOS > Programar pagamento — monta quem recebe quanto no mês
// (Gerentes/Suprimentos/Delta) a partir do Dashboard de Fechamento JÁ SALVO
// daquele mês (api.montarPagamentoSos nunca recalcula a comissão do zero).
// Se o mês já tem um pagamento fechado, o backend devolve ELE (mesmo
// registro, reaberto pra edição) em vez de uma proposta nova — por isso
// esta mesma tela serve tanto pra fechar quanto pra corrigir depois.

const MESES = [
  '', 'Janeiro', 'Fevereiro', 'Março', 'Abril', 'Maio', 'Junho',
  'Julho', 'Agosto', 'Setembro', 'Outubro', 'Novembro', 'Dezembro',
];

let pagamentoAtual = null;  // último resultado de api.montarPagamentoSos
let wired = false;

export async function initProgramarPagamento() {
  if (!wired) {
    wired = true;
    document.getElementById('btnPpBuscar').addEventListener('click', buscar);
    document.getElementById('btnPpFechar').addEventListener('click', fecharOuSalvar);
    document.getElementById('btnPpImprimir').addEventListener('click', imprimir);
  }
  await reload();
}

export async function reload() {
  const mesInput = document.getElementById('ppMes');
  if (!mesInput.value) mesInput.value = new Date().toISOString().slice(0, 7);
}

// Usado pelo Histórico de pagamentos pra reabrir um mês específico direto
// nesta tela (ver historicoPagamentos.js).
export async function abrirMes(mes) {
  document.getElementById('ppMes').value = mes;
  await buscar();
}

function mesAnoLabel(yyyymm) {
  if (!yyyymm || yyyymm.length !== 7) return '—';
  const [ano, mes] = yyyymm.split('-');
  return `${MESES[Number(mes)]}/${ano}`;
}

async function buscar() {
  const mes = document.getElementById('ppMes').value;
  if (!mes) { toast('Escolha o mês de referência.', 'error'); return; }
  try {
    pagamentoAtual = await api.montarPagamentoSos(mes);
  } catch (e) {
    pagamentoAtual = null;
    toast('Erro: ' + errorText(e), 'error');
  }
  render();
}

const TIPO_LABEL = { gerente: 'Gerente', suprimento: 'Suprimentos', delta: 'Delta' };

function render() {
  document.getElementById('ppVazio').style.display = pagamentoAtual ? 'none' : '';
  document.getElementById('ppConteudo').style.display = pagamentoAtual ? '' : 'none';
  document.getElementById('btnPpImprimir').disabled = !pagamentoAtual;
  document.getElementById('btnPpFechar').disabled = !pagamentoAtual;
  if (!pagamentoAtual) return;

  const statusEl = document.getElementById('ppStatusFechado');
  if (pagamentoAtual.fechado) {
    statusEl.style.display = '';
    const quando = pagamentoAtual.fechadoEm ? new Date(pagamentoAtual.fechadoEm).toLocaleString('pt-BR') : '—';
    statusEl.textContent =
      `Pagamento de ${mesAnoLabel(pagamentoAtual.mesReferencia)} já foi fechado em ${quando}. ` +
      'Você pode corrigir abaixo e salvar de novo — a correção substitui o mesmo registro no histórico.';
  } else {
    statusEl.style.display = 'none';
  }
  document.getElementById('btnPpFechar').textContent =
    pagamentoAtual.fechado ? '💾 Salvar correções' : '🔒 Fechar lista de pagamentos';

  const d = pagamentoAtual.dados;
  document.getElementById('ppStatGrid').innerHTML = [
    { label: 'Arrecadado', value: fmtBRL(d.arrecadado) },
    { label: 'Total Gerentes', value: fmtBRL(d.totalGerentes) },
    { label: 'Total Suprimentos', value: fmtBRL(d.totalSuprimentos) },
    { label: 'Total Delta', value: fmtBRL(d.totalDelta) },
  ].map((t) => `<div class="stat-tile"><div class="label">${escapeHtml(t.label)}</div>
    <div class="value">${t.value}</div></div>`).join('');

  document.getElementById('ppLinhasTbody').innerHTML = d.linhas.map((l, i) => `<tr>
      <td><b>${escapeHtml(l.nome)}</b></td>
      <td>${TIPO_LABEL[l.tipo] || escapeHtml(l.tipo)}</td>
      <td>${l.chavePix ? escapeHtml(l.chavePix) : '<span class="muted">sem PIX cadastrado</span>'}</td>
      <td class="num">${fmtBRL(l.valor)}</td>
      <td>${l.autorizado ? '<span class="pill pill-ok">Autorizado</span>' : '<span class="pill pill-low">Não autorizado</span>'}</td>
      <td><button type="button" class="btn-sm ${l.autorizado ? 'btn-outline' : 'btn-primary'}" data-toggle="${i}">
        ${l.autorizado ? 'Desautorizar' : 'Autorizar'}</button></td>
    </tr>`).join('');
  document.getElementById('ppLinhasTbody').querySelectorAll('[data-toggle]').forEach((btn) => {
    btn.addEventListener('click', () => {
      const i = Number(btn.dataset.toggle);
      d.linhas[i].autorizado = !d.linhas[i].autorizado;
      render();
    });
  });

  document.getElementById('ppObservacoes').value = pagamentoAtual.observacoes || '';
}

async function fecharOuSalvar() {
  if (!pagamentoAtual) return;
  const payload = {
    id: pagamentoAtual.id || uid('pag_'),
    mesReferencia: pagamentoAtual.mesReferencia,
    dados: pagamentoAtual.dados,
    observacoes: document.getElementById('ppObservacoes').value.trim(),
    fechado: true,
    geradoEm: pagamentoAtual.geradoEm || nowIso(),
    fechadoEm: pagamentoAtual.fechadoEm || '',
    createdAt: pagamentoAtual.createdAt || nowIso(),
  };
  try {
    pagamentoAtual = await api.salvarPagamentoSos(payload);
    render();
    toast('Lista de pagamentos salva no histórico.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

function imprimir() {
  if (!pagamentoAtual) return;
  // "Só demonstra as pessoas e a quantidade exata de dinheiro autorizada" —
  // nunca as linhas não autorizadas, mesmo que já tenham um valor sugerido.
  const autorizados = pagamentoAtual.dados.linhas.filter((l) => l.autorizado);
  if (!autorizados.length) { toast('Nenhum pagamento autorizado para imprimir.', 'error'); return; }
  printDocument(buildPagamentosDoc(autorizados, mesAnoLabel(pagamentoAtual.mesReferencia)));
}
