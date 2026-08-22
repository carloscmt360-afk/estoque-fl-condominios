import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtBRL } from '../format.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';

// Gestão SOS > Fechamento — fecha um mês de referência: trava (contra edição
// e exclusão) todos os serviços em aberto daquele mês e grava os totais.
// Só mostra meses que TÊM serviço em aberto — não dá para fechar um mês vazio
// nem um mês que já foi fechado (ver commissions_engine.cpp).

let servicos = [];
let mesEscolhido = '';
let wired = false;

export async function initFechamento() {
  if (!wired) {
    wired = true;
    document.getElementById('fecMesSelect').addEventListener('change', (e) => {
      mesEscolhido = e.target.value;
      renderPreview();
    });
    document.getElementById('btnFecharMes').addEventListener('click', fecharMes);
  }
  await reload();
}

export async function reload() {
  servicos = await api.listServicos();
  document.getElementById('btnFecharMes').style.display = can('gestao_sos_servicos', 'update') ? '' : 'none';
  renderSelect();
}

function mesAnoLabel(yyyymm) {
  const [ano, mes] = yyyymm.split('-');
  return `${mes}/${ano}`;
}

// Só entra na lista quem tem serviço aberto E PAGO — um mês só com
// pendências de pagamento não tem o que fechar de verdade (ver fecharMes em
// commissions_engine.cpp: quem não pagou nunca é travado, fica esperando
// ser marcado como pago para entrar num fechamento futuro).
function mesesEmAberto() {
  const set = new Set(servicos.filter((s) => !s.fechado && s.pago).map((s) => s.dataReferencia));
  return [...set].sort().reverse();
}

function renderSelect() {
  const sel = document.getElementById('fecMesSelect');
  const meses = mesesEmAberto();
  if (!meses.includes(mesEscolhido)) mesEscolhido = meses[0] || '';
  sel.innerHTML = meses.length
    ? meses.map((m) => `<option value="${m}">${mesAnoLabel(m)}</option>`).join('')
    : '<option value="">Nenhum mês em aberto</option>';
  sel.value = mesEscolhido;
  renderPreview();
}

function renderPreview() {
  const host = document.getElementById('fecPreview');
  const btn = document.getElementById('btnFecharMes');
  if (!mesEscolhido) {
    host.innerHTML = `<div class="panel modulo-vazio">
      <div class="modulo-vazio-ico">✅</div>
      <h2>Nada para fechar</h2>
      <p>Todos os serviços lançados já estão dentro de algum fechamento. Lance um novo serviço em
        <b>Gestão SOS › Serviços</b> para ter o que fechar aqui.</p>
    </div>`;
    btn.disabled = true;
    return;
  }
  btn.disabled = false;
  // Só quem está PAGO entra no fechamento — quem ainda não pagou fica de
  // fora (visível aqui só para não sumir da conta do mês), continua em
  // aberto e entra num fechamento futuro assim que for marcado como pago.
  const doMes = servicos.filter((s) => !s.fechado && s.dataReferencia === mesEscolhido);
  const pagos = doMes.filter((s) => s.pago);
  const pendentes = doMes.filter((s) => !s.pago);
  const totalVenda = pagos.reduce((a, s) => a + s.venda, 0);
  const totalComissao = pagos.reduce((a, s) => a + s.comissao, 0);

  const linhaServico = (s) => `<tr>
          <td class="num">${s.numero}</td>
          <td>${escapeHtml(s.condominioNome)}</td>
          <td>${s.gerenteNome ? escapeHtml(s.gerenteNome) : '<span class="muted">—</span>'}</td>
          <td>${s.parceiroNome ? escapeHtml(s.parceiroNome) : '<span class="muted">—</span>'}</td>
          <td class="num">${fmtBRL(s.venda)}</td>
          <td class="num">${fmtBRL(s.comissao)}</td></tr>`;

  host.innerHTML = `
    <div class="stat-grid">
      <div class="stat-tile"><div class="label">Serviços pagos em ${mesAnoLabel(mesEscolhido)}</div><div class="value">${pagos.length}</div></div>
      <div class="stat-tile"><div class="label">Total de vendas</div><div class="value">${fmtBRL(totalVenda)}</div></div>
      <div class="stat-tile is-good"><div class="label">Total de comissão</div><div class="value">${fmtBRL(totalComissao)}</div></div>
      ${pendentes.length ? `<div class="stat-tile is-warn"><div class="label">Ainda não pagos (ficam de fora)</div><div class="value">${pendentes.length}</div></div>` : ''}
    </div>
    <div class="panel"><div style="overflow-x:auto;">
      <table>
        <thead><tr><th>ID</th><th>Condomínio</th><th>Gerente</th><th>Parceiro</th><th class="num">Venda</th><th class="num">Comissão</th></tr></thead>
        <tbody>${pagos.length ? pagos.map(linhaServico).join('') :
          '<tr class="empty-row"><td colspan="6">Nenhum serviço pago neste mês ainda.</td></tr>'}</tbody>
      </table>
    </div></div>
    ${pendentes.length ? `
    <div class="info-box" style="margin-top:var(--sp-3);">
      <b>${pendentes.length} serviço(s) ainda sem pagamento confirmado</b> — continuam em aberto neste mês e
      só entram num fechamento depois de marcados como pago em <b>Serviços</b>.
    </div>` : ''}
    <div class="field" style="margin-top:var(--sp-3);"><label>Observações do fechamento</label>
      <textarea id="fecObservacoes" rows="2"></textarea></div>`;
}

async function fecharMes() {
  if (!mesEscolhido) return;
  const doMes = servicos.filter((s) => !s.fechado && s.pago && s.dataReferencia === mesEscolhido);
  if (!confirm(`Fechar ${mesAnoLabel(mesEscolhido)}? ${doMes.length} serviço(s) pago(s) ficarão travados contra edição e exclusão.`)) return;
  const observacoes = document.getElementById('fecObservacoes')?.value.trim() || '';
  try {
    await api.fecharMes({
      id: uid('fec_'), mesReferencia: mesEscolhido, observacoes,
      fechadoEm: nowIso(), createdAt: nowIso(),
    });
    await reload();
    toast(`${mesAnoLabel(mesEscolhido)} fechado.`, 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
