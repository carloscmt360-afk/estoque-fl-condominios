import { api, errorText } from '../api.js';
import { fmtBRL, fmtNum, fmtPct, escapeHtml } from '../format.js';
import { drawBarrasH } from '../charts/deptHBars.js';
import { emptyChart } from '../charts/palette.js';
import { toast } from '../components/toast.js';
import { printDocument, buildSosPainelDoc, TIPO_PAGAMENTO_LABEL } from '../print.js';

// Gestão SOS > Painel — visão de apresentação (números e gráficos) em cima
// dos mesmos Serviços da planilha, sem gravar nada de novo. Existe porque
// quem administra o módulo precisa levar esses números aos diretores, e até
// aqui só havia tabelas cruas (Serviços, Fechamento, Histórico).
const MESES = ['Janeiro','Fevereiro','Março','Abril','Maio','Junho','Julho','Agosto','Setembro','Outubro','Novembro','Dezembro'];
const MESES_ABR = ['Jan','Fev','Mar','Abr','Mai','Jun','Jul','Ago','Set','Out','Nov','Dez'];

let servicos = [];
let gerentes = [];
let parceiros = [];
let pagamentos = [];
let ano = new Date().getFullYear();
let mes = ''; // '' = ano inteiro; senão "01".."12"
let retroGerenteId = '';
let retroParceiroId = '';
let retroEspecialidadeId = '';
let wired = false;

export async function initSosPainel() {
  if (!wired) {
    wired = true;
    document.getElementById('painelSosAno').addEventListener('change', (e) => {
      ano = Number(e.target.value);
      render();
    });
    document.getElementById('painelSosMes').addEventListener('change', (e) => {
      mes = e.target.value;
      render();
    });
    document.getElementById('btnImprimirPainelSos').addEventListener('click', imprimir);
    document.getElementById('painelSosRetroGerente').addEventListener('change', (e) => {
      retroGerenteId = e.target.value;
      renderRetrospectos();
    });
    document.getElementById('painelSosRetroParceiro').addEventListener('change', (e) => {
      retroParceiroId = e.target.value;
      renderRetrospectos();
    });
    document.getElementById('painelSosRetroEspecialidade').addEventListener('change', (e) => {
      retroEspecialidadeId = e.target.value;
      renderRetrospectos();
    });
    document.getElementById('painelSosRetroPessoaOcultarZerados')
      .addEventListener('change', renderRetrospectoPessoa);
  }
  await reload();
}

export async function reload() {
  try {
    [servicos, gerentes, parceiros, pagamentos] = await Promise.all([
      api.listServicos(), api.listGerentes(), api.listParceiros(), api.listPagamentosSos(),
    ]);
  } catch (e) {
    toast('Erro ao carregar os serviços: ' + errorText(e), 'error');
    servicos = []; gerentes = []; parceiros = []; pagamentos = [];
  }
  populaFiltros();
  render();
}

// Uma especialidade pode vir de mais de um parceiro cadastrado — a lista do
// seletor é deduplicada por id (mesmo critério de "por categoria" descrito
// na documentação do Painel: um parceiro com mais de uma especialidade soma
// o mesmo serviço em cada uma delas).
function especialidadesDisponiveis() {
  const porId = new Map();
  for (const p of parceiros) {
    for (const e of p.especialidades || []) if (!porId.has(e.id)) porId.set(e.id, e.nome);
  }
  return [...porId.entries()].map(([id, nome]) => ({ id, nome }))
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'));
}

function popularSelectRetro(id, opcoes, valorAtual) {
  const sel = document.getElementById(id);
  sel.innerHTML = '<option value="">Selecione…</option>' +
    opcoes.map((o) => `<option value="${o.id}">${escapeHtml(o.nome)}</option>`).join('');
  if (opcoes.some((o) => o.id === valorAtual)) sel.value = valorAtual;
  return sel.value;
}

function populaFiltros() {
  const selAno = document.getElementById('painelSosAno');
  const anos = new Set([new Date().getFullYear()]);
  for (const s of servicos) {
    const y = Number(String(s.dataReferencia).slice(0, 4));
    if (y) anos.add(y);
  }
  const anoAtual = selAno.value ? Number(selAno.value) : ano;
  selAno.innerHTML = [...anos].sort((a, b) => b - a).map((a) => `<option value="${a}">${a}</option>`).join('');
  if ([...anos].includes(anoAtual)) selAno.value = anoAtual;
  ano = Number(selAno.value);

  const selMes = document.getElementById('painelSosMes');
  if (!selMes.options.length) {
    selMes.innerHTML = '<option value="">Ano inteiro</option>' +
      MESES.map((n, i) => `<option value="${String(i + 1).padStart(2, '0')}">${n}</option>`).join('');
  }

  retroGerenteId = popularSelectRetro('painelSosRetroGerente',
    [...gerentes].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR')), retroGerenteId);
  retroParceiroId = popularSelectRetro('painelSosRetroParceiro',
    [...parceiros].sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR')), retroParceiroId);
  retroEspecialidadeId = popularSelectRetro('painelSosRetroEspecialidade',
    especialidadesDisponiveis(), retroEspecialidadeId);
}

/* Serviços do ano/mês selecionados. Um serviço fechado continua contando —
   fechar só trava a edição, não tira o lançamento do histórico de vendas. */
function servicosNoFiltro() {
  const prefixo = mes ? `${ano}-${mes}` : `${ano}-`;
  return servicos.filter((s) => String(s.dataReferencia).startsWith(prefixo));
}

function ranking(lista, chaveNome, chaveValor) {
  const porNome = new Map();
  for (const s of lista) {
    const nome = s[chaveNome] || '(não informado)';
    porNome.set(nome, (porNome.get(nome) || 0) + s[chaveValor]);
  }
  return [...porNome.entries()]
    .map(([label, value]) => ({ label, value }))
    .sort((a, b) => b.value - a.value)
    .slice(0, 8);
}

function renderStats(lista) {
  const totalVenda = lista.reduce((s, x) => s + x.venda, 0);
  const totalComissao = lista.reduce((s, x) => s + x.comissao, 0);
  const pctMedia = totalVenda > 0 ? totalComissao / totalVenda : 0;
  const tiles = [
    { label: 'Total de vendas', value: fmtBRL(totalVenda) },
    { label: 'Total de comissão', value: fmtBRL(totalComissao) },
    { label: 'Serviços lançados', value: fmtNum(lista.length) },
    { label: '% média de comissão', value: lista.length ? fmtPct(pctMedia, 1) : '—' },
  ];
  document.getElementById('painelSosStatGrid').innerHTML = tiles.map((t) => `
    <div class="stat-tile"><div class="label">${escapeHtml(t.label)}</div>
      <div class="value">${t.value}</div></div>`).join('');
}

function renderEvolucao() {
  const porMes = new Array(12).fill(0);
  for (const s of servicos) {
    const [y, m] = String(s.dataReferencia).split('-');
    if (Number(y) === ano && m) porMes[Number(m) - 1] += s.comissao;
  }
  // mesAtualIdx marca o mês corrente SÓ quando o ano escolhido é o ano
  // corrente — não faz sentido "destacar dezembro" num ano já encerrado.
  const hoje = new Date();
  const mesAtualIdx = hoje.getFullYear() === ano ? hoje.getMonth() : -1;
  const rows = MESES_ABR.map((label, i) => ({ label, value: porMes[i], atual: i === mesAtualIdx }));
  document.getElementById('painelSosEvolucaoSub').textContent =
    `Comissão por mês de referência em ${ano} — soma ${fmtBRL(porMes.reduce((a, b) => a + b, 0))}.`;
  // O mês atual sempre aparece (mesmo com valor 0) para continuar em
  // evidência; os outros meses vazios continuam escondidos, como antes.
  drawBarrasH(document.getElementById('chartPainelSosEvolucao'), rows.filter((r) => r.value > 0 || r.atual),
    { tipLabel: 'Comissão', mode: 'timeline', aria: 'Evolução mensal de comissão', empty: `Nenhum serviço lançado em ${ano}.` });
}

function renderRankings(lista) {
  drawBarrasH(document.getElementById('chartPainelSosGerente'), ranking(lista, 'gerenteNome', 'comissao'),
    { tipLabel: 'Comissão', aria: 'Ranking por gerente', empty: 'Nenhum serviço com gerente no período.' });
  drawBarrasH(document.getElementById('chartPainelSosParceiro'), ranking(lista, 'parceiroNome', 'comissao'),
    { tipLabel: 'Comissão', aria: 'Ranking por parceiro', empty: 'Nenhum serviço com parceiro no período.' });
  drawBarrasH(document.getElementById('chartPainelSosCondominio'), ranking(lista, 'condominioNome', 'venda'),
    { tipLabel: 'Venda', aria: 'Ranking por condomínio', empty: 'Nenhum serviço no período.' });
}

// ---- retrospecto anual (mês a mês, um ano inteiro) por gerente/parceiro/
// especialidade — diferente das rankings acima (top 8 do período filtrado),
// aqui é UMA entidade escolhida contra os 12 meses do ano do topo da tela,
// pra enxergar sazonalidade e comparar contra o mês corrente.
function comissaoMensal(filtroFn) {
  const porMes = new Array(12).fill(0);
  for (const s of servicos) {
    const [y, m] = String(s.dataReferencia).split('-');
    if (Number(y) === ano && m && filtroFn(s)) porMes[Number(m) - 1] += s.comissao;
  }
  return porMes;
}

function linhasRetrospecto(porMes, mesAtualIdx) {
  return MESES_ABR.map((label, i) => ({ label, value: porMes[i], atual: i === mesAtualIdx }));
}

function renderRetrospectos() {
  const hoje = new Date();
  const mesAtualIdx = hoje.getFullYear() === ano ? hoje.getMonth() : -1;

  const hostGerente = document.getElementById('chartPainelSosRetroGerente');
  if (!retroGerenteId) {
    emptyChart(hostGerente, 'Selecione um gerente.');
  } else {
    const porMes = comissaoMensal((s) => s.gerenteId === retroGerenteId);
    drawBarrasH(hostGerente, linhasRetrospecto(porMes, mesAtualIdx),
      { tipLabel: 'Comissão', mode: 'timeline', aria: 'Retrospecto anual por gerente' });
  }

  const hostParceiro = document.getElementById('chartPainelSosRetroParceiro');
  if (!retroParceiroId) {
    emptyChart(hostParceiro, 'Selecione um parceiro.');
  } else {
    const porMes = comissaoMensal((s) => s.parceiroId === retroParceiroId);
    drawBarrasH(hostParceiro, linhasRetrospecto(porMes, mesAtualIdx),
      { tipLabel: 'Comissão', mode: 'timeline', aria: 'Retrospecto anual por parceiro' });
  }

  const hostEspecialidade = document.getElementById('chartPainelSosRetroEspecialidade');
  if (!retroEspecialidadeId) {
    emptyChart(hostEspecialidade, 'Selecione uma especialidade.');
  } else {
    // Mesmo critério do "Por categoria" do Painel: um parceiro com mais de
    // uma especialidade soma o mesmo serviço em cada uma delas.
    const porMes = comissaoMensal((s) => {
      const p = parceiros.find((x) => x.id === s.parceiroId);
      return !!(p && (p.especialidades || []).some((e) => e.id === retroEspecialidadeId));
    });
    drawBarrasH(hostEspecialidade, linhasRetrospecto(porMes, mesAtualIdx),
      { tipLabel: 'Comissão', mode: 'timeline', aria: 'Retrospecto anual por especialidade' });
  }
}

// ---- retrospecto do ano POR PESSOA (formato da planilha da gerência): uma
// linha por quem recebe, doze colunas de mês, uma de total.

// Sem "R$" na célula: são 12 colunas de dinheiro lado a lado, e repetir o
// símbolo em todas só atrapalha a leitura (mesmo critério da matriz do
// Retrospecto de departamentos). O cabeçalho da seção já diz que é dinheiro.
function moeda(v) {
  return (v || 0).toLocaleString('pt-BR', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
}

// A origem aqui NÃO são os serviços (como no resto do Painel), e sim a lista
// de pagamentos fechada de cada mês em Programar pagamento. É de propósito:
// a planilha inclui Suprimentos e Delta ao lado dos gerentes, e esses dois só
// existem no fechamento de pagamento — não dá pra derivar dos serviços. Como
// efeito, a tabela mostra o que de fato foi AUTORIZADO a pagar, que é a
// pergunta que a planilha responde ("quanto cada um levou no ano").
//
// Mês sem lista fechada fica marcado como ausente (célula "—"), nunca como
// zero: "ninguém recebeu" e "ainda não fechamos o mês" são coisas diferentes.
function matrizRetrospectoPessoa() {
  const doAno = pagamentos
    .filter((p) => String(p.mesReferencia || '').slice(0, 4) === String(ano))
    .sort((a, b) => String(a.mesReferencia).localeCompare(String(b.mesReferencia)));

  const mesFechado = new Array(12).fill(false);
  const porPessoa = new Map();

  for (const p of doAno) {
    const idx = Number(String(p.mesReferencia).slice(5, 7)) - 1;
    if (!(idx >= 0 && idx <= 11)) continue;
    mesFechado[idx] = true;
    for (const l of (p.dados && p.dados.linhas) || []) {
      // A pessoa entra na tabela por aparecer na lista do mês, mesmo sem ter
      // sido autorizada — some da matriz seria pior que mostrar 0,00. Só o
      // VALOR depende da autorização.
      const chave = `${l.tipo}|${l.nome}`;
      if (!porPessoa.has(chave)) {
        porPessoa.set(chave, { nome: l.nome, tipo: l.tipo, meses: new Array(12).fill(0) });
      }
      if (l.autorizado) porPessoa.get(chave).meses[idx] += l.valor;
    }
  }

  const linhas = [...porPessoa.values()].map((p) => ({
    ...p, total: p.meses.reduce((s, v) => s + v, 0),
  }));
  // Mesma ordem da lista impressa do mês: gerentes, depois Suprimentos, depois
  // Delta; dentro de cada tipo, por nome.
  const ordemTipo = { gerente: 0, suprimento: 1, delta: 2 };
  linhas.sort((a, b) => (ordemTipo[a.tipo] ?? 9) - (ordemTipo[b.tipo] ?? 9)
    || a.nome.localeCompare(b.nome, 'pt-BR'));

  const totaisMes = new Array(12).fill(0);
  linhas.forEach((l) => l.meses.forEach((v, i) => { totaisMes[i] += v; }));

  return { linhas, mesFechado, totaisMes, total: totaisMes.reduce((s, v) => s + v, 0) };
}

function renderRetrospectoPessoa() {
  const m = matrizRetrospectoPessoa();
  const ocultar = document.getElementById('painelSosRetroPessoaOcultarZerados').checked;
  const linhas = ocultar ? m.linhas.filter((l) => Math.round(l.total * 100) !== 0) : m.linhas;

  document.getElementById('painelSosRetroPessoaTitulo').textContent = `Ano ${ano}`;
  const tabela = document.getElementById('painelSosRetroPessoaTabela');

  if (!linhas.length) {
    tabela.innerHTML = `<thead><tr><th class="col-dept">Nome</th></tr></thead>
      <tbody><tr class="empty-row"><td>Nenhuma lista de pagamento fechada em ${ano}${
        m.linhas.length ? ' com valor autorizado' : ''}.</td></tr></tbody>`;
    return;
  }

  const celula = (v, i) => {
    if (!m.mesFechado[i]) return '<td class="num vazio" title="mês ainda sem lista de pagamento fechada">—</td>';
    if (Math.round(v * 100) === 0) return '<td class="num zero" title="mês fechado, sem valor autorizado">0,00</td>';
    return `<td class="num">${moeda(v)}</td>`;
  };

  tabela.innerHTML = `<thead><tr><th class="col-dept">Nome</th><th>Tipo</th>
      ${MESES_ABR.map((mm) => `<th class="num">${mm}</th>`).join('')}
      <th class="num col-total">Total</th></tr></thead>
    <tbody>${linhas.map((l) => `<tr>
      <td class="col-dept">${escapeHtml(l.nome)}</td>
      <td>${TIPO_PAGAMENTO_LABEL[l.tipo] || escapeHtml(l.tipo)}</td>
      ${l.meses.map(celula).join('')}
      <td class="num col-total">${moeda(l.total)}</td></tr>`).join('')}</tbody>
    <tfoot><tr><td class="col-dept">TOTAL</td><td></td>
      ${m.mesFechado.map((fechado, i) => fechado
        ? `<td class="num">${moeda(m.totaisMes[i])}</td>`
        : '<td class="num vazio">—</td>').join('')}
      <td class="num col-total">${moeda(m.total)}</td></tr></tfoot>`;
}

function render() {
  const lista = servicosNoFiltro();
  renderStats(lista);
  renderEvolucao();
  renderRankings(lista);
  renderRetrospectos();
  renderRetrospectoPessoa();
}

function filtroLabel() {
  return mes ? `${MESES[Number(mes) - 1]}/${ano}` : `Ano ${ano} (todos os meses)`;
}

function imprimir() {
  const lista = servicosNoFiltro();
  const porMes = new Array(12).fill(0);
  for (const s of servicos) {
    const [y, m] = String(s.dataReferencia).split('-');
    if (Number(y) === ano && m) porMes[Number(m) - 1] += s.comissao;
  }
  printDocument(buildSosPainelDoc({
    filtroTxt: filtroLabel(),
    lista,
    evolucaoAno: ano,
    evolucaoMeses: MESES.map((label, i) => ({ label, value: porMes[i] })),
    rankGerente: ranking(lista, 'gerenteNome', 'comissao'),
    rankParceiro: ranking(lista, 'parceiroNome', 'comissao'),
    rankCondominio: ranking(lista, 'condominioNome', 'venda'),
    retroPessoa: retroPessoaParaImpressao(),
  }));
}

// O impresso respeita a mesma caixa "Ocultar quem não recebeu nada no ano" da
// tela: o papel sai igual ao que a pessoa está vendo na hora de imprimir.
function retroPessoaParaImpressao() {
  const m = matrizRetrospectoPessoa();
  const ocultar = document.getElementById('painelSosRetroPessoaOcultarZerados').checked;
  const linhas = (ocultar ? m.linhas.filter((l) => Math.round(l.total * 100) !== 0) : m.linhas)
    .map((l) => ({ ...l, tipoLabel: TIPO_PAGAMENTO_LABEL[l.tipo] || l.tipo }));
  return { ano, linhas, mesFechado: m.mesFechado, totaisMes: m.totaisMes, total: m.total };
}
