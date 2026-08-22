import { api, errorText } from '../api.js';
import { escapeHtml, fmtBRL } from '../format.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { enableRowSelection } from '../components/tableTools.js';

// Gestão SOS > Histórico de fechamentos — os meses já fechados, com os
// totais gravados no momento do fechamento (nunca recalculados: os serviços
// dentro de um fechamento são imutáveis, então o total nunca diverge).
//
// TODO fechamento permanece aqui para sempre — "Reabrir" NUNCA remove a
// linha, só solta os serviços de volta para edição e marca `reabertoEm`
// (ver commissions_engine.cpp). Um mês pode ser fechado de novo depois de
// reaberto: cada fechamento vira sua própria entrada permanente, então o
// mesmo mês pode aparecer aqui mais de uma vez ao longo do tempo.

let fechamentos = [];
let busca = '';
let wired = false;

export async function initHistoricoFechamentos() {
  if (!wired) {
    wired = true;
    document.getElementById('fecBusca').addEventListener('input', (e) => {
      busca = e.target.value.toLowerCase();
      render();
    });
    enableRowSelection(document.getElementById('historicoFechamentosTbody'));
  }
  await reload();
}

export async function reload() {
  fechamentos = await api.listFechamentos();
  render();
}

function mesAnoLabel(yyyymm) {
  const [ano, mes] = yyyymm.split('-');
  return `${mes}/${ano}`;
}

function bateBusca(f) {
  if (!busca) return true;
  return [mesAnoLabel(f.mesReferencia), f.observacoes].filter(Boolean).join(' ').toLowerCase().includes(busca);
}

function render() {
  const tbody = document.getElementById('historicoFechamentosTbody');
  const filtrados = fechamentos.filter(bateBusca);
  document.getElementById('fechamentosTotal').textContent = fechamentos.length;
  document.getElementById('fechamentosFiltrados').textContent = busca ? ` · ${filtrados.length} no filtro` : '';

  if (!filtrados.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="7">${
      fechamentos.length ? 'Nenhum fechamento encontrado com esse filtro.' : 'Nenhum fechamento realizado ainda.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = filtrados.map((f) => {
    const acoes = (!f.reabertoEm && can('gestao_sos_servicos', 'update'))
      ? `<button class="btn-sm btn-outline" data-reabrir="${f.id}">Reabrir para editar</button>`
      : '<span class="muted">—</span>';
    const situacao = f.reabertoEm
      ? `<span class="pill pill-low" title="Os serviços deste fechamento foram destravados e podem ter sido alterados desde então.">Reaberto em ${new Date(f.reabertoEm).toLocaleString('pt-BR')}</span>`
      : '';
    return `<tr>
      <td><b>${mesAnoLabel(f.mesReferencia)}</b>${situacao ? `<br/>${situacao}` : ''}</td>
      <td class="num">${f.quantidadeServicos}</td>
      <td class="num">${fmtBRL(f.totalVenda)}</td>
      <td class="num"><b>${fmtBRL(f.totalComissao)}</b></td>
      <td>${new Date(f.fechadoEm).toLocaleString('pt-BR')}</td>
      <td>${f.observacoes ? escapeHtml(f.observacoes) : '<span class="muted">—</span>'}</td>
      <td><div class="row-actions">${acoes}</div></td></tr>`;
  }).join('');

  tbody.querySelectorAll('[data-reabrir]').forEach((b) =>
    b.addEventListener('click', () => reabrir(b.dataset.reabrir)));
}

async function reabrir(id) {
  const f = fechamentos.find((x) => x.id === id);
  if (!f) return;
  if (!confirm(`Reabrir ${mesAnoLabel(f.mesReferencia)}? Os ${f.quantidadeServicos} serviço(s) voltam a poder ser editados/excluídos em Serviços. Este registro continua aqui no histórico, só marcado como reaberto — os totais mostrados ficam congelados como estavam no momento do fechamento.`)) return;
  try {
    await api.reabrirFechamento(id);
    await reload();
    toast('Fechamento reaberto — continua no histórico.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
