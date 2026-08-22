import { api, errorText } from '../api.js';
import { escapeHtml } from '../format.js';
import { toast } from '../components/toast.js';
import { printDocument, buildCatalogoEmpresasDoc } from '../print.js';

// Catálogo de empresas — a primeira tela do módulo, e a única de LEITURA.
//
// As outras duas são de manutenção (Setorização monta o catálogo, Cadastro
// preenche as empresas). Esta existe para o momento em que já há uma demanda
// na mão: "preciso de quem faz impermeabilização" — então ela é organizada por
// SEGMENTO, não por empresa, e cada empresa já vem com telefone e e-mail à
// vista, porque o passo seguinte a achar é ligar.
//
// Nada aqui é calculado de novo: `especialidades` já vem resolvida em cada
// empresa (ver empresaToJson em api.cpp), e o agrupamento é só uma releitura
// dessa mesma lista.

let empresas = [];
let setores = [];
let setorFiltro = '';
let busca = '';
// Quais empresas o usuário marcou pra "Copiar contatos" — o botão só copia
// ESSAS, nunca o catálogo inteiro (pedido explícito: "somente os contatos
// que eu escolher"). Sobrevive a trocar de setor/busca de propósito: marcar
// uma empresa numa aba e outra em outra é o caso de uso normal (juntar um
// grupo de contatos de segmentos diferentes antes de copiar).
let selecionados = new Set();
let wired = false;

// Uma cor por setor, pra distinguir um segmento do outro numa lista longa —
// cíclica (mais de 8 setores repete a partir do 1º). O índice usa a posição
// do setor na lista COMPLETA (não a filtrada), pra a cor de um setor nunca
// mudar só porque outro sumiu do filtro.
const CORES_SETOR = ['#1b3a5c', '#6b3fa0', '#1f7a5c', '#a0522d', '#b0284a', '#2f6b6b', '#8a6d1b', '#4a4a8a'];
function corSetor(key) {
  const idx = setores.findIndex((s) => s.key === key);
  return CORES_SETOR[(idx < 0 ? 0 : idx) % CORES_SETOR.length];
}

export async function initCatalogoEmpresas() {
  if (!wired) {
    wired = true;
    document.getElementById('catBusca').addEventListener('input', (e) => {
      busca = e.target.value;
      renderResultado();
    });
    document.getElementById('btnImprimirCatalogo').addEventListener('click', imprimir);
    document.getElementById('btnImprimirParceirosCatalogo').addEventListener('click', imprimirParceiros);
    document.getElementById('btnCompartilharCatalogo').addEventListener('click', compartilhar);
  }
  await reload();
}

export async function reload() {
  try {
    const [lista, setorizacao] = await Promise.all([api.listEmpresas(), api.listSetorizacao()]);
    empresas = lista;
    setores = setorizacao.setores || [];
  } catch (e) {
    toast('Erro ao carregar o catálogo: ' + errorText(e), 'error');
    empresas = [];
    setores = [];
  }
  renderStats();
  renderFiltroSetor();
  renderResultado();
}

function normaliza(s) {
  return String(s || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
}

function renderStats() {
  const comEspecialidade = empresas.filter((e) => e.especialidades.length).length;
  const nichos = setores.reduce((s, x) => s + x.especialidades.length, 0);
  const tiles = [
    { label: 'Empresas cadastradas', value: empresas.length },
    { label: 'Especialidades no catálogo', value: nichos },
    { label: 'Empresas com especialidade', value: comEspecialidade },
    // Empresa sem nenhuma marcação não aparece em segmento nenhum: é o número
    // que diz quanto do catálogo ainda não está utilizável.
    { label: 'Sem especialidade marcada', value: empresas.length - comEspecialidade,
      cls: empresas.length - comEspecialidade > 0 ? 'is-warn' : '' },
  ];
  document.getElementById('catStatGrid').innerHTML = tiles.map((t) => `
    <div class="stat-tile ${t.cls || ''}"><div class="label">${escapeHtml(t.label)}</div>
      <div class="value">${t.value}</div></div>`).join('');
}

function renderFiltroSetor() {
  const host = document.getElementById('catAbas');
  const todos = `<button class="${setorFiltro === '' ? 'active' : ''}" data-setor="">Todos os setores</button>`;
  host.innerHTML = todos + setores.map((s) => `
    <button class="${s.key === setorFiltro ? 'active' : ''}" data-setor="${s.key}">
      ${escapeHtml(s.label)}
    </button>`).join('');
  host.querySelectorAll('[data-setor]').forEach((b) => b.addEventListener('click', () => {
    setorFiltro = b.dataset.setor;
    renderFiltroSetor();
    renderResultado();
  }));
}

/* Uma empresa entra no resultado da busca se o termo casar com o nome dela, a
   cidade, o CNPJ ou qualquer especialidade — é a mesma regra do Cadastro. */
function casaBusca(empresa, termo) {
  if (!termo) return true;
  const alvo = [empresa.nome, empresa.nomeFantasia, empresa.cnpj, empresa.cidade, empresa.estado,
                empresa.telefone, ...empresa.especialidades.map((x) => x.nome)].join(' ');
  return normaliza(alvo).includes(termo);
}

/* Agrupamento setor -> especialidade -> empresas, já filtrado pelo setor e
   pela busca ativos — a MESMA estrutura alimenta a tela, o Imprimir e o
   Copiar contatos, para o que aparece impresso/copiado ser sempre
   exatamente o que está na tela (nunca uma segunda regra de filtro
   divergindo da primeira). */
// `somenteParceiros` restringe a base ANTES de agrupar — usado só pelo
// "Imprimir só parceiros" (ver imprimirParceiros abaixo). A tela e o
// "Imprimir" normal continuam mostrando o catálogo inteiro; isso é
// deliberadamente um segundo caminho, não um filtro visível na tela, porque
// o pedido foi só "opção para imprimir o relatório somente do catálogo de
// parceiros" — a navegação do dia a dia não muda.
function catalogoAgrupado(somenteParceiros) {
  const termo = normaliza(busca);
  const setoresVisiveis = setorFiltro ? setores.filter((s) => s.key === setorFiltro) : setores;
  const base = somenteParceiros ? empresas.filter((e) => e.parceira) : empresas;

  const grupos = [];
  let achou = 0;
  for (const setor of setoresVisiveis) {
    const especialidades = [];
    for (const esp of setor.especialidades) {
      const daEspecialidade = base
        .filter((e) => e.especialidades.some((x) => x.id === esp.id))
        .filter((e) => casaBusca(e, termo))
        .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'));
      // Com busca ativa, especialidade sem resultado não vira linha vazia —
      // só atrapalharia a leitura. Sem busca, ela aparece marcada como vazia,
      // porque aí a informação útil é justamente "não tenho ninguém aqui".
      if (!daEspecialidade.length && termo) continue;
      achou += daEspecialidade.length;
      especialidades.push({ esp, empresas: daEspecialidade });
    }
    if (especialidades.length) grupos.push({ setor, especialidades });
  }
  return { grupos, achou, termo };
}

function cartaoEmpresa(e, especialidadeAtual) {
  // As OUTRAS especialidades da empresa aparecem como contexto: quem procurou
  // "dedetização" também quer saber que a mesma empresa faz limpeza de caixa.
  const outras = e.especialidades.filter((x) => x.id !== especialidadeAtual);
  const contatos = [
    e.telefone ? `<span class="cat-contato">📞 ${escapeHtml(e.telefone)}</span>` : '',
    e.emails ? `<span class="cat-contato">✉ ${escapeHtml(e.emails)}</span>` : '',
  ].filter(Boolean).join('');
  const local = [e.cidade, e.estado].filter(Boolean).join(' / ');
  // Título principal é o nome pelo qual a empresa é CONHECIDA no dia a dia —
  // o fantasia, quando tiver. A razão social vira contexto secundário (só
  // aparece se for diferente, senão repetiria a mesma coisa duas vezes).
  const tituloPrincipal = e.nomeFantasia || e.nome;
  const tituloSecundario = e.nomeFantasia && e.nomeFantasia !== e.nome ? e.nome : '';
  return `<div class="cat-empresa">
    <div class="cat-empresa-topo">
      <label class="cat-empresa-select" title="Marcar para copiar o contato">
        <input type="checkbox" data-empresa-check="${e.id}" ${selecionados.has(e.id) ? 'checked' : ''} />
      </label>
      <b>${escapeHtml(tituloPrincipal)}</b>${tituloSecundario ? ` <span class="muted">(${escapeHtml(tituloSecundario)})</span>` : ''}
      ${local ? `<span class="muted">${escapeHtml(local)}</span>` : ''}
    </div>
    ${contatos ? `<div class="cat-contatos">${contatos}</div>`
               : '<div class="cat-contatos muted">sem contato cadastrado</div>'}
    ${outras.length ? `<div class="chip-row">${outras.map((x) =>
      `<span class="chip-esp" title="${escapeHtml(x.setorLabel)}">${escapeHtml(x.nome)}</span>`).join('')}</div>` : ''}
  </div>`;
}

function renderResultado() {
  const host = document.getElementById('catResultado');
  const { grupos, achou, termo } = catalogoAgrupado();

  document.getElementById('catResumoBusca').textContent =
    termo ? `${achou} empresa(s) encontradas` : '';

  if (!grupos.length) {
    const semNicho = setores.every((s) => !s.especialidades.length);
    host.innerHTML = `<div class="panel modulo-vazio">
      <div class="modulo-vazio-ico">🔎</div>
      <h2>${semNicho ? 'Catálogo ainda vazio' : 'Nenhum resultado'}</h2>
      <p>${semNicho
        ? 'Cadastre as especialidades em <b>Setorização</b> e as empresas em <b>Cadastro</b> — elas aparecem aqui agrupadas por segmento.'
        : 'Nenhuma empresa encontrada para esta busca. Tente outro termo ou troque o setor.'}</p>
    </div>`;
    return;
  }

  host.innerHTML = grupos.map(({ setor, especialidades }) => `
    <div class="cat-setor-titulo" style="background:${corSetor(setor.key)};">${escapeHtml(setor.label)}</div>
    <div class="panel cat-setor">${especialidades.map(({ esp, empresas: daEspecialidade }) => `
      <div class="cat-esp">
        <div class="cat-esp-cab">
          <b>${escapeHtml(esp.nome)}</b>
          <span class="pill ${daEspecialidade.length ? 'pill-accent' : 'pill-warn'}">
            ${daEspecialidade.length ? `${daEspecialidade.length} empresa${daEspecialidade.length > 1 ? 's' : ''}`
                                     : 'nenhuma empresa'}</span>
        </div>
        ${daEspecialidade.length
          ? `<div class="cat-empresas">${daEspecialidade.map((e) => cartaoEmpresa(e, esp.id)).join('')}</div>`
          : '<div class="cat-vazio muted">Nenhuma empresa cadastrada neste segmento ainda.</div>'}
      </div>`).join('')}</div>`).join('');

  // A mesma empresa pode aparecer em mais de uma especialidade/setor — um
  // clique precisa marcar/desmarcar TODAS as caixinhas dela, não só a que
  // foi clicada, senão a seleção pareceria "vazar" ao trocar de aba.
  host.querySelectorAll('[data-empresa-check]').forEach((chk) => chk.addEventListener('change', () => {
    const id = chk.dataset.empresaCheck;
    if (chk.checked) selecionados.add(id); else selecionados.delete(id);
    host.querySelectorAll(`[data-empresa-check="${id}"]`).forEach((x) => { x.checked = chk.checked; });
    atualizaBotaoCopiar();
  }));
  atualizaBotaoCopiar();
}

function atualizaBotaoCopiar() {
  const btn = document.getElementById('btnCompartilharCatalogo');
  const n = selecionados.size;
  btn.textContent = n ? `📋 Copiar contatos (${n})` : '📋 Copiar contatos';
  btn.disabled = n === 0;
}

function filtroLabel() {
  const setorTxt = setorFiltro ? (setores.find((s) => s.key === setorFiltro) || {}).label : 'Todos os setores';
  return busca.trim() ? `${setorTxt} · busca "${busca.trim()}"` : setorTxt;
}

function imprimir() {
  const { grupos } = catalogoAgrupado();
  if (!grupos.length) { toast('Nada para imprimir com este filtro.', 'error'); return; }
  printDocument(buildCatalogoEmpresasDoc(grupos, filtroLabel()));
}

function imprimirParceiros() {
  const { grupos } = catalogoAgrupado(true);
  if (!grupos.length) { toast('Nenhuma empresa parceira encontrada com este filtro.', 'error'); return; }
  printDocument(buildCatalogoEmpresasDoc(grupos, `${filtroLabel()} · somente parceiros`));
}

/* "Compartilhar" num app desktop não tem folha de compartilhamento do
   sistema operacional — o que serve de verdade é colocar um texto pronto na
   área de transferência, para colar direto no WhatsApp/e-mail/onde for.
   Copia SÓ as empresas marcadas na caixinha de cada cartão — nunca o
   catálogo inteiro (pedido explícito do usuário). Cada empresa entra uma
   única vez, mesmo se estiver marcada em mais de uma especialidade. */
async function compartilhar() {
  if (!selecionados.size) { toast('Marque ao menos uma empresa para copiar.', 'error'); return; }

  const jaCopiadas = new Set();
  const linhas = [`Contatos selecionados — ${filtroLabel()}`, ''];
  for (const e of empresas) {
    if (!selecionados.has(e.id) || jaCopiadas.has(e.id)) continue;
    jaCopiadas.add(e.id);
    const nomeCompleto = e.nomeFantasia ? `${e.nome} (${e.nomeFantasia})` : e.nome;
    const local = [e.cidade, e.estado].filter(Boolean).join('/');
    linhas.push(`- ${nomeCompleto}${local ? ` — ${local}` : ''}`);
    if (e.telefone) linhas.push(`    Tel.: ${e.telefone}`);
    if (e.emails) linhas.push(`    E-mail: ${e.emails}`);
  }

  const texto = linhas.join('\n').trim();
  try {
    await navigator.clipboard.writeText(texto);
    toast(`${jaCopiadas.size} contato(s) copiado(s) — cole onde quiser compartilhar.`, 'success');
  } catch (e) {
    toast('Não foi possível copiar automaticamente: ' + errorText(e), 'error');
  }
}
