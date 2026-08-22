import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtBRL, paraBusca } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';

// Importar Serviços em lote — cola direto do Excel (as colunas vêm separadas
// por TAB automaticamente ao copiar um intervalo de células). Pensado para o
// caso real: dezenas/centenas de linhas do MESMO mês, então a data fica fixa
// para o lote inteiro; cada linha colada traz o resto.
//
// Nem toda planilha do usuário tem as mesmas colunas nem na mesma ordem — por
// isso a colagem não assume mais uma ordem fixa: o usuário marca, na ordem em
// que clica, quais colunas vai colar (renderColunasConfig/toggleColuna
// abaixo), e essa ordem é que decide qual pedaço de cada linha colada
// (separada por TAB) vai para qual campo. A escolha fica salva no
// localStorage — a planilha de cada usuário não muda de formato todo mês.
//
// Condomínio, Gerente e Empresa são casados pelo NOME com o que já está
// cadastrado (normalizado — sem acento, minúsculo). Nome exigido de forma
// inflexível derrubaria o lote inteiro por causa de uma abreviação; por isso,
// quando não há correspondência exata (ou há mais de uma parcial), a própria
// linha da prévia vira um <select> para o usuário resolver na hora, sem sair
// do modal nem editar o texto colado.

const COLUNAS_INFO = {
  codigo: 'Código', condominio: 'Condomínio', gerente: 'Gerente',
  empresa: 'Empresa', venda: 'Venda', porcentagem: 'Porcentagem',
};
const COLUNAS_CHAVES = ['codigo', 'condominio', 'gerente', 'empresa', 'venda', 'porcentagem'];
const COLUNAS_OBRIGATORIAS = ['condominio', 'venda'];
const COLUNAS_PADRAO = ['codigo', 'condominio', 'gerente', 'venda', 'porcentagem'];
const COLUNAS_NUM = ['venda', 'porcentagem'];
const CHAVE_STORAGE_COLUNAS = 'impServicosColunas';

let getContexto = null;   // () => { condominios, gerentes, parceiros, porcentagemPadrao }
let aoImportar = null;    // chamado (async) depois de importar com sucesso ao menos 1 linha
let linhas = [];
let colunasAtivas = carregarColunas();
let soPendencias = false;  // filtro "Mostrar só as linhas com pendência"
let wired = false;

function carregarColunas() {
  try {
    const salvo = JSON.parse(localStorage.getItem(CHAVE_STORAGE_COLUNAS) || 'null');
    if (Array.isArray(salvo) && salvo.every((k) => COLUNAS_CHAVES.includes(k)) &&
        COLUNAS_OBRIGATORIAS.every((k) => salvo.includes(k))) {
      return salvo;
    }
  } catch { /* fixture antiga/corrompida: cai no padrão */ }
  return [...COLUNAS_PADRAO];
}

function salvarColunas() {
  localStorage.setItem(CHAVE_STORAGE_COLUNAS, JSON.stringify(colunasAtivas));
}

// Clicar num chip INATIVO o ativa no fim da ordem. Clicar num chip já ATIVO
// não desliga — REORDENA, mandando ele para o fim (é assim que se resolve o
// caso "Venda ficou obrigatoriamente em 3º lugar mas preciso dela em 4º":
// clica em Empresa, depois clica de novo em Venda). Só o × (removerColuna)
// desliga uma coluna, e só as não-obrigatórias têm esse ×.
function ativarOuReordenar(chave) {
  colunasAtivas = [...colunasAtivas.filter((k) => k !== chave), chave];
  salvarColunas();
  renderColunasConfig();
  reprocessar();
}

function removerColuna(chave) {
  if (COLUNAS_OBRIGATORIAS.includes(chave)) {
    toast('Condomínio e Venda são obrigatórios na colagem.', 'error');
    return;
  }
  colunasAtivas = colunasAtivas.filter((k) => k !== chave);
  salvarColunas();
  renderColunasConfig();
  reprocessar();
}

function renderColunasConfig() {
  const host = document.getElementById('impServicosColunasConfig');
  host.innerHTML = COLUNAS_CHAVES.map((chave) => {
    const pos = colunasAtivas.indexOf(chave);
    const ativa = pos >= 0;
    const removivel = ativa && !COLUNAS_OBRIGATORIAS.includes(chave);
    return `<button type="button" class="chip-toggle ${ativa ? 'on' : ''}" data-coluna="${chave}"
              title="${ativa ? 'Clique para mandar para o fim da ordem' : 'Clique para incluir'}">
      ${ativa ? `${pos + 1}. ` : ''}${escapeHtml(COLUNAS_INFO[chave])}
      ${removivel ? `<span class="chip-toggle-x" data-remover="${chave}" title="Remover">✕</span>` : ''}
    </button>`;
  }).join('');
  host.querySelectorAll('[data-coluna]').forEach((b) =>
    b.addEventListener('click', () => ativarOuReordenar(b.dataset.coluna)));
  host.querySelectorAll('[data-remover]').forEach((x) =>
    x.addEventListener('click', (e) => { e.stopPropagation(); removerColuna(x.dataset.remover); }));

  document.getElementById('impServicosColunasHint').textContent = colunasAtivas.length
    ? `Ordem esperada da colagem: ${colunasAtivas.map((k) => COLUNAS_INFO[k]).join(', ')}.`
    : 'Marque ao menos Condomínio e Venda.';
  document.getElementById('impServicosParceiroLabel').textContent = colunasAtivas.includes('empresa')
    ? 'Parceiro padrão (linhas sem Empresa preenchida)'
    : 'Parceiro (aplicado a todas as linhas)';
}

export function initImportarServicos(contexto, callbackAoImportar) {
  getContexto = contexto;
  aoImportar = callbackAoImportar;
  if (wired) return;
  wired = true;
  document.getElementById('btnImportarServicos').addEventListener('click', abrir);
  document.getElementById('impServicosTexto').addEventListener('input', reprocessar);
  document.getElementById('btnImportarServicosConfirmar').addEventListener('click', executar);
  document.getElementById('impServicosSoPendencias').addEventListener('change', (e) => {
    soPendencias = e.target.checked;
    renderPreview();
  });
}

function abrir() {
  document.getElementById('impServicosTexto').value = '';
  document.getElementById('impServicosDataReferencia').value = new Date().toISOString().slice(0, 7);
  popularParceiroSelect();
  renderColunasConfig();
  linhas = [];
  soPendencias = false;
  document.getElementById('impServicosSoPendencias').checked = false;
  renderPreview();
  openModal('modalImportarServicos');
}

function popularParceiroSelect() {
  const sel = document.getElementById('impServicosParceiro');
  const { parceiros } = getContexto();
  sel.innerHTML = '<option value="">— nenhum —</option>' + [...parceiros]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((p) => `<option value="${p.id}">${escapeHtml(p.nome)}</option>`).join('');
}

// "125.000,00" -> 125000; "5.525,76" -> 5525.76; "10%" -> 10. Formato BR: ponto
// separa milhar, vírgula separa decimal — é assim que o Excel em pt-BR copia.
function parseNumeroBR(bruto) {
  let s = String(bruto ?? '').trim().replace(/[R$\s%]/g, '');
  if (!s) return null;
  s = s.includes(',') ? s.replace(/\./g, '').replace(',', '.') : s;
  const n = parseFloat(s);
  return Number.isFinite(n) ? n : null;
}

// Os nomes dos campos de cada linha nunca mudam (codigo, condominioNome...);
// o que muda é DE QUAL coluna colada cada um vem, conforme colunasAtivas.
const CAMPO_DA_COLUNA = {
  codigo: 'codigo', condominio: 'condominioNome', gerente: 'gerenteNome',
  empresa: 'empresaNome', venda: 'vendaTexto', porcentagem: 'porcentagemTexto',
};

function parseLinhas(texto) {
  return texto.split(/\r?\n/).map((l) => l.trim()).filter(Boolean).map((linha, i) => {
    const cols = linha.split('\t').map((c) => c.trim());
    const l0 = {
      numero: i + 1, codigo: '', condominioNome: '', gerenteNome: '', empresaNome: '',
      vendaTexto: '', porcentagemTexto: '',
      condominioManualId: '', gerenteManualId: '', empresaManualId: '',
    };
    colunasAtivas.forEach((chave, idx) => { l0[CAMPO_DA_COLUNA[chave]] = cols[idx] || ''; });
    return l0;
  });
}

// Casa pelo nome normalizado: exato primeiro; se não achar exatamente um,
// tenta parcial (contém, nos dois sentidos) — só resolve sozinho quando UMA
// única opção sobra. Ambíguo ou sem nenhuma opção vira <select> na prévia.
function casarPorNome(nomeBusca, lista) {
  const alvo = paraBusca(nomeBusca);
  if (!alvo) return { status: 'vazio' };
  const exatos = lista.filter((x) => paraBusca(x.nome) === alvo);
  if (exatos.length === 1) return { status: 'ok', item: exatos[0] };
  const parciais = lista.filter((x) => {
    const n = paraBusca(x.nome);
    return n.includes(alvo) || alvo.includes(n);
  });
  if (parciais.length === 1) return { status: 'ok', item: parciais[0] };
  return { status: parciais.length > 1 ? 'ambiguo' : 'nao_encontrado' };
}

function reprocessar() {
  const texto = document.getElementById('impServicosTexto').value;
  linhas = parseLinhas(texto);
  renderPreview();
}

function condominioResolvido(l, ctx) {
  if (l.condominioManualId) return ctx.condominios.find((c) => c.id === l.condominioManualId) || null;
  const r = casarPorNome(l.condominioNome, ctx.condominios);
  return r.status === 'ok' ? r.item : null;
}

function gerenteResolvido(l, ctx) {
  if (l.gerenteManualId) return ctx.gerentes.find((g) => g.id === l.gerenteManualId) || null;
  if (!l.gerenteNome) return null;  // gerente é opcional
  const r = casarPorNome(l.gerenteNome, ctx.gerentes);
  return r.status === 'ok' ? r.item : null;
}

// Empresa é opcional linha a linha: quando a coluna não é colada ou a célula
// vem em branco, cai no Parceiro padrão do topo do modal (ver executar()).
function empresaResolvido(l, ctx) {
  if (l.empresaManualId) return ctx.parceiros.find((p) => p.id === l.empresaManualId) || null;
  if (!l.empresaNome) return null;
  const r = casarPorNome(l.empresaNome, ctx.parceiros);
  return r.status === 'ok' ? r.item : null;
}

function statusCondominio(l, ctx) {
  if (l.condominioManualId) return 'ok';
  return casarPorNome(l.condominioNome, ctx.condominios).status;
}

function statusGerente(l, ctx) {
  if (l.gerenteManualId) return 'ok';
  if (!l.gerenteNome) return 'vazio';
  return casarPorNome(l.gerenteNome, ctx.gerentes).status;
}

function statusEmpresa(l, ctx) {
  if (l.empresaManualId) return 'ok';
  if (!l.empresaNome) return 'vazio';
  return casarPorNome(l.empresaNome, ctx.parceiros).status;
}

function errosDe(l, ctx) {
  const erros = [];
  const scond = statusCondominio(l, ctx);
  if (scond === 'vazio') erros.push('condomínio em branco');
  else if (scond !== 'ok') erros.push('condomínio não resolvido');
  const sger = statusGerente(l, ctx);
  if (sger === 'ambiguo' || sger === 'nao_encontrado') erros.push('gerente não resolvido');
  const semp = statusEmpresa(l, ctx);
  if (semp === 'ambiguo' || semp === 'nao_encontrado') erros.push('empresa não resolvida');
  const venda = parseNumeroBR(l.vendaTexto);
  if (venda === null || venda < 0) erros.push('venda inválida');
  const pctInformada = l.porcentagemTexto !== '';
  const porcentagem = pctInformada ? parseNumeroBR(l.porcentagemTexto) : ctx.porcentagemPadrao;
  if (porcentagem === null || porcentagem < 0 || porcentagem > 100) erros.push('porcentagem inválida');
  if (l.erroSalvar) erros.push(l.erroSalvar);
  return erros;
}

function celulaColuna(chave, l, ctx) {
  switch (chave) {
    case 'codigo':
      return l.codigo ? escapeHtml(l.codigo) : '<span class="muted">—</span>';
    case 'condominio': {
      if (statusCondominio(l, ctx) === 'ok') return escapeHtml(condominioResolvido(l, ctx).nome);
      return `<select data-cond-manual="${l.numero}" class="imp-select">
                <option value="">${escapeHtml(l.condominioNome) || '(vazio)'} — selecione...</option>
                ${[...ctx.condominios].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
                  .map((c) => `<option value="${c.id}">${escapeHtml(c.nome)}</option>`).join('')}
              </select>`;
    }
    case 'gerente': {
      if (statusGerente(l, ctx) === 'ok') {
        const g = gerenteResolvido(l, ctx);
        return g ? escapeHtml(g.nome) : '<span class="muted">—</span>';
      }
      return `<select data-ger-manual="${l.numero}" class="imp-select">
                <option value="">${escapeHtml(l.gerenteNome) || '(vazio)'} — selecione...</option>
                ${[...ctx.gerentes].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
                  .map((g) => `<option value="${g.id}">${escapeHtml(g.nome)}</option>`).join('')}
              </select>`;
    }
    case 'empresa': {
      const semp = statusEmpresa(l, ctx);
      if (semp === 'ok' || semp === 'vazio') {
        const p = empresaResolvido(l, ctx);
        return p ? escapeHtml(p.nome) : '<span class="muted">— usa o padrão</span>';
      }
      return `<select data-emp-manual="${l.numero}" class="imp-select">
                <option value="">${escapeHtml(l.empresaNome) || '(vazio)'} — selecione...</option>
                ${[...ctx.parceiros].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
                  .map((p) => `<option value="${p.id}">${escapeHtml(p.nome)}</option>`).join('')}
              </select>`;
    }
    case 'venda': {
      const venda = parseNumeroBR(l.vendaTexto);
      return venda === null ? '<span class="flag-critical">?</span>' : fmtBRL(venda);
    }
    case 'porcentagem': {
      const pctInformada = l.porcentagemTexto !== '';
      const porcentagem = pctInformada ? parseNumeroBR(l.porcentagemTexto) : ctx.porcentagemPadrao;
      return porcentagem === null ? '<span class="flag-critical">?</span>' : porcentagem.toLocaleString('pt-BR') + '%';
    }
    default:
      return '';
  }
}

// Agrupa as linhas com condomínio/gerente/empresa não resolvidos pelo MESMO
// texto colado (normalizado — sem acento, minúsculo, mesmo critério de
// casarPorNome): é o caso comum de "Alencar" aparecendo em várias dezenas de
// linhas porque a planilha de origem abrevia o nome do gerente. Resolver o
// grupo aplica o mesmo id a TODAS as linhas dele de uma vez, em vez de abrir
// um <select> igual repetidas vezes na prévia.
function gruposPendentes(ctx) {
  const grupos = new Map();
  const addGrupo = (campo, texto, linha) => {
    const chave = campo + '|' + paraBusca(texto);
    if (!grupos.has(chave)) grupos.set(chave, { campo, texto, linhas: [] });
    grupos.get(chave).linhas.push(linha);
  };
  for (const l of linhas) {
    if (colunasAtivas.includes('condominio')) {
      const s = statusCondominio(l, ctx);
      if (s === 'ambiguo' || s === 'nao_encontrado') addGrupo('condominio', l.condominioNome, l);
    }
    if (colunasAtivas.includes('gerente')) {
      const s = statusGerente(l, ctx);
      if (s === 'ambiguo' || s === 'nao_encontrado') addGrupo('gerente', l.gerenteNome, l);
    }
    if (colunasAtivas.includes('empresa')) {
      const s = statusEmpresa(l, ctx);
      if (s === 'ambiguo' || s === 'nao_encontrado') addGrupo('empresa', l.empresaNome, l);
    }
  }
  return [...grupos.values()].sort((a, b) => b.linhas.length - a.linhas.length);
}

const CAMPO_MANUAL = { condominio: 'condominioManualId', gerente: 'gerenteManualId', empresa: 'empresaManualId' };

function renderPendenciasLote(ctx) {
  const host = document.getElementById('impServicosPendenciasLote');
  const grupos = gruposPendentes(ctx);
  if (!grupos.length) { host.innerHTML = ''; return; }

  const listaDe = { condominio: ctx.condominios, gerente: ctx.gerentes, empresa: ctx.parceiros };
  host.innerHTML = `<div class="imp-pendencias-lote">
      <div class="imp-pendencias-lote-titulo">Corrigir pendências em massa</div>
      ${grupos.map((g, i) => `<div class="imp-pendencias-lote-item">
          <span>${escapeHtml(COLUNAS_INFO[g.campo])} "<b>${escapeHtml(g.texto) || '(vazio)'}</b>"
            em ${g.linhas.length} linha${g.linhas.length > 1 ? 's' : ''} —</span>
          <select data-grupo-lote="${i}">
            <option value="">é o mesmo que...</option>
            ${[...listaDe[g.campo]].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
              .map((x) => `<option value="${x.id}">${escapeHtml(x.nome)}</option>`).join('')}
          </select>
        </div>`).join('')}
    </div>`;

  host.querySelectorAll('[data-grupo-lote]').forEach((sel) => sel.addEventListener('change', (e) => {
    const g = grupos[Number(e.target.dataset.grupoLote)];
    if (!g || !e.target.value) return;
    const campo = CAMPO_MANUAL[g.campo];
    for (const l of g.linhas) l[campo] = e.target.value;
    renderPreview();
  }));
}

function renderThead() {
  const thead = document.getElementById('impServicosThead');
  const ths = colunasAtivas.map((k) =>
    `<th${COLUNAS_NUM.includes(k) ? ' class="num"' : ''}>${escapeHtml(k === 'porcentagem' ? '%' : COLUNAS_INFO[k])}</th>`).join('');
  thead.innerHTML = `<tr><th class="num">#</th>${ths}<th>Situação</th></tr>`;
}

function renderPreview() {
  const ctx = getContexto();
  const tbody = document.getElementById('impServicosTbody');
  const resumo = document.getElementById('impServicosResumo');
  const btn = document.getElementById('btnImportarServicosConfirmar');
  renderThead();
  renderPendenciasLote(ctx);

  const colspan = colunasAtivas.length + 2;
  if (!linhas.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="${colspan}">Cole as linhas acima para ver a prévia.</td></tr>`;
    resumo.textContent = '';
    btn.disabled = true;
    btn.textContent = 'Importar';
    return;
  }

  const linhasComErro = linhas.filter((l) => errosDe(l, ctx).length > 0);
  const comErro = linhasComErro.length;
  const prontas = linhas.length - comErro;
  resumo.innerHTML = `<b>${prontas}</b> pronta(s) para importar` +
    (comErro ? ` · <span class="flag-critical">${comErro} com pendência</span>` : '');
  btn.disabled = prontas === 0;
  btn.textContent = `Importar ${prontas} serviço(s)`;

  // "Mostrar só as linhas com pendência" só afeta o que aparece na tabela —
  // o total/prontas acima sempre reflete o lote inteiro, e importar continua
  // processando toda linha válida, visível ou não.
  const exibidas = soPendencias ? linhasComErro : linhas;
  if (!exibidas.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="${colspan}">Nenhuma pendência — todas as linhas estão prontas.</td></tr>`;
  } else {
    tbody.innerHTML = exibidas.map((l) => {
      const erros = errosDe(l, ctx);
      const situacao = erros.length
        ? `<span class="pill pill-low" title="${escapeHtml(erros.join('; '))}">${escapeHtml(erros[0])}</span>`
        : `<span class="pill pill-ok">Pronta</span>`;
      const celulas = colunasAtivas.map((k) =>
        `<td${COLUNAS_NUM.includes(k) ? ' class="num"' : ''}>${celulaColuna(k, l, ctx)}</td>`).join('');

      return `<tr${erros.length ? ' class="imp-row-pendente"' : ''}><td class="num">${l.numero}</td>${celulas}<td>${situacao}</td></tr>`;
    }).join('');
  }

  tbody.querySelectorAll('[data-cond-manual]').forEach((sel) => sel.addEventListener('change', (e) => {
    const l = linhas.find((x) => x.numero === Number(e.target.dataset.condManual));
    if (l) l.condominioManualId = e.target.value;
    renderPreview();
  }));
  tbody.querySelectorAll('[data-ger-manual]').forEach((sel) => sel.addEventListener('change', (e) => {
    const l = linhas.find((x) => x.numero === Number(e.target.dataset.gerManual));
    if (l) l.gerenteManualId = e.target.value;
    renderPreview();
  }));
  tbody.querySelectorAll('[data-emp-manual]').forEach((sel) => sel.addEventListener('change', (e) => {
    const l = linhas.find((x) => x.numero === Number(e.target.dataset.empManual));
    if (l) l.empresaManualId = e.target.value;
    renderPreview();
  }));
}

async function executar() {
  const ctx = getContexto();
  const parceiroPadrao = document.getElementById('impServicosParceiro').value;
  const dataReferencia = document.getElementById('impServicosDataReferencia').value;
  if (!dataReferencia) { toast('Informe o mês de referência.', 'error'); return; }

  const validas = linhas.filter((l) => errosDe(l, ctx).length === 0);
  if (!validas.length) return;

  const btn = document.getElementById('btnImportarServicosConfirmar');
  btn.disabled = true;
  let ok = 0;
  for (const l of validas) {
    btn.textContent = `Importando ${ok + 1}/${validas.length}...`;
    const condominio = condominioResolvido(l, ctx);
    const gerente = gerenteResolvido(l, ctx);
    const empresa = empresaResolvido(l, ctx);
    const pctInformada = l.porcentagemTexto !== '';
    try {
      await api.createServico({
        id: uid('srv_'), codigo: l.codigo, condominioId: condominio.id,
        gerenteId: gerente ? gerente.id : '', parceiroId: empresa ? empresa.id : parceiroPadrao,
        venda: parseNumeroBR(l.vendaTexto),
        porcentagem: pctInformada ? parseNumeroBR(l.porcentagemTexto) : ctx.porcentagemPadrao,
        dataReferencia, observacoes: '', createdAt: nowIso(),
      });
      ok++;
      linhas = linhas.filter((x) => x.numero !== l.numero);  // sai da prévia: já foi
    } catch (e) {
      l.erroSalvar = errorText(e);
    }
  }
  btn.disabled = false;

  const falharam = validas.length - ok;
  if (!falharam && !linhas.length) {
    toast(`${ok} serviço(s) importado(s).`, 'success');
    closeModal('modalImportarServicos');
  } else {
    toast(`${ok} importado(s)${falharam ? `, ${falharam} falharam ao salvar` : ''} — revise a prévia.`, falharam ? 'error' : 'success');
    renderPreview();
  }
  if (ok > 0 && aoImportar) await aoImportar();
}
