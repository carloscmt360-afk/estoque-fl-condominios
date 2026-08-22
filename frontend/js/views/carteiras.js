import { api, errorText } from '../api.js';
import { escapeHtml, paraBusca } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';

// Gestão SOS > Carteiras — clicar num gerente abre a lista de TODOS os
// condomínios cadastrados, marcados por clique (mesmo padrão de chip da
// Setorização/Cadastro de empresas). Os marcados formam a carteira dele.
//
// A escrita reaproveita updateGerente: a carteira é sempre regravada por
// inteiro (não há "marcar 1 condomínio" isolado na ponte), então o payload
// leva o gerente inteiro junto com a nova lista de condominioIds.

let gerentes = [];
let condominios = [];
let gerenteAtualId = '';
let selecionados = new Set();
let buscaCarteira = '';
let wired = false;

export async function initCarteiras() {
  await reload();
}

export async function reload() {
  const [g, c] = await Promise.all([api.listGerentes(), api.listCondominios()]);
  gerentes = g;
  condominios = c;
  render();
  if (!wired) {
    wired = true;
    document.getElementById('btnSalvarCarteira').addEventListener('click', saveCarteira);
    document.getElementById('carteiraBusca').addEventListener('input', (e) => {
      buscaCarteira = e.target.value;
      renderChecklist();
    });
  }
}

function render() {
  const host = document.getElementById('carteirasLista');
  const sorted = [...gerentes].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'));

  if (!sorted.length) {
    host.innerHTML = `<div class="panel modulo-vazio">
      <div class="modulo-vazio-ico">💼</div>
      <h2>Nenhum gerente cadastrado ainda</h2>
      <p>Cadastre gerentes em <b>Gestão SOS › Gerentes</b> para depois montar a carteira de cada um aqui.</p>
    </div>`;
    return;
  }

  host.innerHTML = `<div class="parceiros-grid">${sorted.map((g) => {
    const n = (g.condominios || []).length;
    return `<div class="parceiro-card carteira-card" data-abrir="${g.id}" role="button" tabindex="0">
      <div class="parceiro-nome">${escapeHtml(g.nome)}</div>
      ${g.telefone ? `<div class="parceiro-tel">📞 ${escapeHtml(g.telefone)}</div>` : ''}
      <div class="seg-count">${n} condomínio${n === 1 ? '' : 's'} na carteira</div>
      ${n ? `<div class="chip-row">${(g.condominios || []).slice(0, 6).map((c) =>
          `<span class="chip-esp">${escapeHtml(c.nome)}</span>`).join('')}${n > 6 ? `<span class="chip-esp muted">+${n - 6}</span>` : ''}</div>`
        : '<div class="muted" style="font-size:11px;">nenhum condomínio marcado</div>'}
    </div>`;
  }).join('')}</div>`;

  host.querySelectorAll('[data-abrir]').forEach((el) => {
    el.addEventListener('click', () => abrirCarteira(el.dataset.abrir));
    el.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); abrirCarteira(el.dataset.abrir); }
    });
  });
}

function abrirCarteira(gerenteId) {
  const g = gerentes.find((x) => x.id === gerenteId);
  if (!g) return;
  if (!condominios.length) { toast('Cadastre um condomínio primeiro (Cadastro de Condomínios).', 'error'); return; }
  gerenteAtualId = gerenteId;
  selecionados = new Set((g.condominios || []).map((c) => c.id));
  buscaCarteira = '';
  document.getElementById('carteiraBusca').value = '';
  document.getElementById('modalCarteiraTitulo').textContent = `Carteira de ${g.nome}`;
  renderChecklist();
  openModal('modalCarteira');
}

function renderChecklist() {
  const host = document.getElementById('carteiraCondominios');
  const termo = paraBusca(buscaCarteira).trim();
  const sorted = [...condominios]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .filter((c) => !termo || paraBusca(c.nome).includes(termo));

  if (!sorted.length) {
    host.innerHTML = '<div class="carteira-lista-vazio">Nenhum condomínio encontrado para esta busca.</div>';
    atualizaContagemCarteira();
    return;
  }

  host.innerHTML = sorted.map((c) => {
    const marcado = selecionados.has(c.id);
    // Ex-cliente (ativo=false) pode continuar na carteira por histórico —
    // mas não conta como número de carteira nem entra na meta do gerente
    // (ver a mesma regra em montarDashboard, commissions_engine.cpp).
    return `<label class="carteira-item ${marcado ? 'on' : ''}">
      <input type="checkbox" data-cond="${c.id}" ${marcado ? 'checked' : ''}>
      ${escapeHtml(c.nome)}${!c.ativo ? ' <span class="muted">(ex-cliente — não conta na meta)</span>' : ''}
    </label>`;
  }).join('');

  host.querySelectorAll('[data-cond]').forEach((chk) => chk.addEventListener('change', () => {
    const id = chk.dataset.cond;
    if (chk.checked) selecionados.add(id);
    else selecionados.delete(id);
    chk.closest('.carteira-item').classList.toggle('on', chk.checked);
    atualizaContagemCarteira();
  }));
  atualizaContagemCarteira();
}

function atualizaContagemCarteira() {
  const n = selecionados.size;
  document.getElementById('carteiraContagem').textContent =
    n === 0 ? 'nenhum condomínio marcado' : `${n} condomínio${n > 1 ? 's' : ''} marcado${n > 1 ? 's' : ''}`;
}

async function saveCarteira() {
  const g = gerentes.find((x) => x.id === gerenteAtualId);
  if (!g) return;
  try {
    await api.updateGerente({
      id: g.id, nome: g.nome, telefone: g.telefone, email: g.email, observacoes: g.observacoes,
      condominioIds: [...selecionados], createdAt: '',
    });
    await reload();
    closeModal('modalCarteira');
    toast('Carteira salva.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
