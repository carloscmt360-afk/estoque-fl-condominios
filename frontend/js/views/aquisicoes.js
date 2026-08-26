import { api, errorText } from '../api.js';
import { escapeHtml, uid, nowIso, fmtBRL, fmtNum, fmtDateBR } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { drawBarrasH } from '../charts/deptHBars.js';
import { enableRowSelection } from '../components/tableTools.js';
import { bindMoneyMask, setMoneyMaskedValue, parseMoneyMasked } from '../masks.js';

// Compras > Aquisições FL — compra geral ligada a um fornecedor já
// cadastrado em Fornecedores e Prestadores de Serviços. Sem item a item: só
// fornecedor, descrição, NF, valor e data — o que uma NF de compra tem.
//
// O dashboard (stat-grid + gráficos) é só uma releitura da mesma lista de
// aquisições, mesmo critério do Painel de Gestão SOS: nada de novo é
// calculado no backend, tudo aqui é soma/agrupamento em cima do que já veio.

const MESES_ABR = ['Jan','Fev','Mar','Abr','Mai','Jun','Jul','Ago','Set','Out','Nov','Dez'];

let aquisicoes = [];
let fornecedores = [];
let anoDash = new Date().getFullYear();
let busca = '';
let wired = false;

// Estado do anexo pendente no modal — só é gravado (upload ou remoção) ao
// Salvar, mesmo critério de fotoPendenteBase64/fotoRemovida em products.js.
let anexoPendenteBase64 = null;
let anexoPendenteTipo = null; // "imagem" | "pdf" — só usado pra decidir o preview em memória
let anexoRemovido = false;

export async function initAquisicoes() {
  if (!wired) {
    wired = true;
    document.getElementById('btnNovaAquisicao').addEventListener('click', () => openAquisicaoModal());
    document.getElementById('btnSalvarAquisicao').addEventListener('click', saveAquisicao);
    document.getElementById('aqDashAno').addEventListener('change', (e) => {
      anoDash = Number(e.target.value);
      renderDashboard();
    });
    document.getElementById('btnEscolherAnexo').addEventListener('click', () =>
      document.getElementById('aqAnexoInput').click());
    document.getElementById('aqAnexoInput').addEventListener('change', onAnexoSelecionado);
    document.getElementById('btnRemoverAnexo').addEventListener('click', onRemoverAnexoClick);
    document.getElementById('aqAnexoPreview').addEventListener('click', onAnexoPreviewClick);
    document.getElementById('aqBusca').addEventListener('input', (e) => {
      busca = e.target.value.toLowerCase();
      render();
    });
    enableRowSelection(document.getElementById('aquisicoesTbody'));
    bindMoneyMask(document.getElementById('aqValor'));
  }
  await reload();
}

export async function reload() {
  try {
    const [aq, emp] = await Promise.all([api.listAquisicoes(), api.listEmpresas()]);
    aquisicoes = aq; fornecedores = emp;
  } catch (e) {
    toast('Erro ao carregar as aquisições: ' + errorText(e), 'error');
    aquisicoes = []; fornecedores = [];
  }
  document.getElementById('btnNovaAquisicao').style.display = can('aquisicoes', 'create') ? '' : 'none';
  populaAnoDash();
  render();
  renderDashboard();
}

function populaAnoDash() {
  const sel = document.getElementById('aqDashAno');
  const anos = new Set([new Date().getFullYear()]);
  for (const a of aquisicoes) {
    const y = Number(String(a.dataCompra).slice(0, 4));
    if (y) anos.add(y);
  }
  const anoAtual = sel.value ? Number(sel.value) : anoDash;
  sel.innerHTML = [...anos].sort((a, b) => b - a).map((a) => `<option value="${a}">${a}</option>`).join('');
  if ([...anos].includes(anoAtual)) sel.value = anoAtual;
  anoDash = Number(sel.value);
}

function doAno() {
  return aquisicoes.filter((a) => String(a.dataCompra).startsWith(String(anoDash)));
}

function renderStats(lista) {
  const total = lista.reduce((s, a) => s + a.valor, 0);
  const tiles = [
    { label: `Total comprado em ${anoDash}`, value: fmtBRL(total) },
    { label: 'Aquisições no ano', value: fmtNum(lista.length) },
    { label: 'Ticket médio', value: lista.length ? fmtBRL(total / lista.length) : '—' },
  ];
  document.getElementById('aqStatGrid').innerHTML = tiles.map((t) => `
    <div class="stat-tile"><div class="label">${escapeHtml(t.label)}</div>
      <div class="value">${t.value}</div></div>`).join('');
}

function renderEvolucao(lista) {
  const porMes = new Array(12).fill(0);
  for (const a of lista) {
    const m = Number(String(a.dataCompra).slice(5, 7));
    if (m) porMes[m - 1] += a.valor;
  }
  const rows = MESES_ABR.map((label, i) => ({ label, value: porMes[i] })).filter((r) => r.value > 0);
  document.getElementById('aqEvolucaoSub').textContent =
    `Valor comprado por mês em ${anoDash} — soma ${fmtBRL(porMes.reduce((a, b) => a + b, 0))}.`;
  drawBarrasH(document.getElementById('chartAqEvolucao'), rows,
    { tipLabel: 'Comprado', aria: 'Compras por mês', empty: `Nenhuma aquisição lançada em ${anoDash}.` });
}

function renderRankingFornecedor(lista) {
  const porFornecedor = new Map();
  for (const a of lista) {
    const nome = a.fornecedorNome || '(não informado)';
    porFornecedor.set(nome, (porFornecedor.get(nome) || 0) + a.valor);
  }
  const rows = [...porFornecedor.entries()]
    .map(([label, value]) => ({ label, value }))
    .sort((a, b) => b.value - a.value)
    .slice(0, 8);
  drawBarrasH(document.getElementById('chartAqFornecedor'), rows,
    { tipLabel: 'Comprado', aria: 'Ranking por fornecedor', empty: `Nenhuma aquisição lançada em ${anoDash}.` });
}

function renderDashboard() {
  const lista = doAno();
  renderStats(lista);
  renderEvolucao(lista);
  renderRankingFornecedor(lista);
}

// Fornecedor excluído depois do lançamento continua visível, pré-selecionado
// e congelado — mesmo critério de opcaoCongelada em servicos.js: editar outro
// campo da aquisição não pode trocar essa referência sem querer.
function opcaoCongelada(id, nome, lista) {
  if (!id || lista.some((x) => x.id === id)) return '';
  return `<option value="${id}">${escapeHtml(nome)} (excluído)</option>`;
}

function popularFornecedorSelect(selecionado) {
  const sel = document.getElementById('aqFornecedorId');
  sel.innerHTML = '<option value="">Selecione...</option>' + [...fornecedores]
    .sort((a, b) => a.nome.localeCompare(b.nome, 'pt-BR'))
    .map((f) => `<option value="${f.id}">${escapeHtml(f.nome)}</option>`).join('') +
    (selecionado ? opcaoCongelada(selecionado.fornecedorId, selecionado.fornecedorNome, fornecedores) : '');
  if (selecionado) sel.value = selecionado.fornecedorId;
}

function bateBusca(a) {
  if (!busca) return true;
  return [a.fornecedorNome, a.descricao, a.notaFiscal].filter(Boolean).join(' ').toLowerCase().includes(busca);
}

function render() {
  const tbody = document.getElementById('aquisicoesTbody');
  const filtradas = aquisicoes.filter(bateBusca);
  document.getElementById('aquisicoesTotal').textContent = aquisicoes.length;
  document.getElementById('aquisicoesFiltradas').textContent = busca ? ` · ${filtradas.length} no filtro` : '';

  if (!filtradas.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="6">${
      aquisicoes.length ? 'Nenhuma aquisição encontrada com esse filtro.' : 'Nenhuma aquisição lançada ainda.'}</td></tr>`;
    return;
  }
  tbody.innerHTML = filtradas.map((a) => {
    const acoes = [
      a.anexoPath ? `<button class="btn-sm btn-outline" data-baixar="${a.id}" title="Baixar a NF anexada">⬇ NF</button>` : '',
      can('aquisicoes', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${a.id}">Editar</button>` : '',
      can('aquisicoes', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${a.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    return `<tr>
      <td>${fmtDateBR(a.dataCompra)}</td>
      <td><b>${escapeHtml(a.fornecedorNome)}</b></td>
      <td>${escapeHtml(a.descricao)}</td>
      <td>${a.notaFiscal ? escapeHtml(a.notaFiscal) : '<span class="muted">—</span>'}
        ${a.anexoPath ? '<span class="pill pill-accent" title="Tem NF anexada">📎 anexo</span>' : ''}</td>
      <td class="num">${fmtBRL(a.valor)}</td>
      <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-editar]').forEach((b) =>
    b.addEventListener('click', () => openAquisicaoModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) =>
    b.addEventListener('click', () => deleteAquisicao(b.dataset.excluir)));
  tbody.querySelectorAll('[data-baixar]').forEach((b) =>
    b.addEventListener('click', () => baixarAnexo(b.dataset.baixar)));
}

// Baixa o anexo da NF pro disco do usuário — mesmo dado que o preview já usa
// (data: URL vinda de api.readAquisicaoAttachment), só que via <a download>
// em vez de mostrar na tela. A extensão vem do próprio data: URL (o mime
// real do arquivo), não de anexoTipo — mais confiável que adivinhar.
function baixarAnexo(id) {
  const a = aquisicoes.find((x) => x.id === id);
  if (!a || !a.anexoPath) return;
  api.readAquisicaoAttachment(a.anexoPath).then((dataUrl) => {
    const mime = (dataUrl.match(/^data:([^;]+);/) || [])[1] || '';
    const ext = mime.includes('pdf') ? 'pdf' : (mime.split('/')[1] || 'webp');
    const nomeBase = (a.notaFiscal || a.fornecedorNome || 'anexo').replace(/[^\w-]+/g, '_');
    const link = document.createElement('a');
    link.href = dataUrl;
    link.download = `NF-${nomeBase}.${ext}`;
    document.body.appendChild(link);
    link.click();
    link.remove();
  }).catch(() => toast('Não foi possível baixar o anexo.', 'error'));
}

// Mostra o anexo atual (foto ou PDF) ou o placeholder "Sem anexo" — nunca
// tenta desenhar um PDF como <img>, mesmo critério de resetFotoPreview em
// products.js pra fotos.
function resetAnexoPreview(a) {
  const preview = document.getElementById('aqAnexoPreview');
  const btnRemover = document.getElementById('btnRemoverAnexo');
  delete preview.dataset.dataUrl;
  delete preview.dataset.tipo;
  if (a && a.anexoPath) {
    preview.dataset.path = a.anexoPath;
    preview.dataset.tipo = a.anexoTipo;
    btnRemover.style.display = 'inline-block';
    if (a.anexoTipo === 'pdf') {
      preview.innerHTML = '<span class="foto-placeholder">📄 PDF</span>';
    } else {
      preview.innerHTML = '<span class="foto-placeholder">Carregando...</span>';
      api.readAquisicaoAttachment(a.anexoPath)
        .then((dataUrl) => { preview.innerHTML = `<img src="${dataUrl}" alt="Anexo da NF" />`; })
        .catch(() => { preview.innerHTML = '<span class="foto-placeholder">Sem anexo</span>'; });
    }
  } else {
    delete preview.dataset.path;
    preview.innerHTML = '<span class="foto-placeholder">Sem anexo</span>';
    btnRemover.style.display = 'none';
  }
}

function fileToBase64(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result).split(',')[1] || '');
    reader.onerror = () => reject(new Error('não foi possível ler o arquivo'));
    reader.readAsDataURL(file);
  });
}

const TIPOS_ANEXO_ACEITOS = ['image/jpeg', 'image/jpg', 'image/png', 'image/webp', 'application/pdf'];

async function onAnexoSelecionado(e) {
  const file = e.target.files && e.target.files[0];
  e.target.value = ''; // permite escolher o mesmo arquivo de novo mais tarde
  if (!file) return;
  if (!TIPOS_ANEXO_ACEITOS.includes(file.type)) {
    toast('Selecione uma imagem (JPG, PNG, WebP) ou um PDF.', 'error');
    return;
  }
  try {
    const base64 = await fileToBase64(file);
    anexoPendenteBase64 = base64;
    anexoPendenteTipo = file.type === 'application/pdf' ? 'pdf' : 'imagem';
    anexoRemovido = false;
    const preview = document.getElementById('aqAnexoPreview');
    delete preview.dataset.path;
    if (anexoPendenteTipo === 'pdf') {
      preview.dataset.dataUrl = `data:${file.type};base64,${base64}`;
      preview.dataset.tipo = 'pdf';
      preview.innerHTML = '<span class="foto-placeholder">📄 PDF</span>';
    } else {
      const dataUrl = `data:${file.type};base64,${base64}`;
      preview.dataset.dataUrl = dataUrl;
      preview.dataset.tipo = 'imagem';
      preview.innerHTML = `<img src="${dataUrl}" alt="Prévia do anexo" />`;
    }
    document.getElementById('btnRemoverAnexo').style.display = 'inline-block';
  } catch (err) {
    toast('Erro ao ler o arquivo: ' + err, 'error');
  }
}

function onRemoverAnexoClick() {
  anexoPendenteBase64 = null;
  anexoPendenteTipo = null;
  anexoRemovido = true;
  const preview = document.getElementById('aqAnexoPreview');
  delete preview.dataset.path;
  delete preview.dataset.dataUrl;
  delete preview.dataset.tipo;
  preview.innerHTML = '<span class="foto-placeholder">Sem anexo</span>';
  document.getElementById('btnRemoverAnexo').style.display = 'none';
}

// PDF nunca abre dentro da caixinha de 72px — sempre em nova aba, do jeito
// que o navegador/webview já sabe mostrar um PDF. Foto usa o mesmo modal
// ampliado do Estoque (#modalFotoAmpliada, compartilhado com Produtos/
// Requisições) — só não reaproveita openFotoAmpliada() porque aquele
// componente busca sempre em imagens/produtos (api.readProductImage); aqui
// o caminho em disco é de anexos/aquisicoes, então busca com
// api.readAquisicaoAttachment.
function ampliarAnexoImagem(origem) {
  const corpo = document.getElementById('fotoAmpliadaCorpo');
  document.getElementById('fotoAmpliadaTitulo').textContent = 'Anexo da Nota Fiscal';
  corpo.innerHTML = '<span class="foto-placeholder">Carregando...</span>';
  openModal('modalFotoAmpliada');

  const mostrar = (dataUrl) => { corpo.innerHTML = `<img src="${dataUrl}" alt="Anexo da Nota Fiscal" />`; };
  const falhou = () => { corpo.innerHTML = '<span class="foto-placeholder">Não foi possível carregar o anexo.</span>'; };
  if (!origem) { falhou(); return; }
  if (origem.startsWith('data:')) { mostrar(origem); return; }
  api.readAquisicaoAttachment(origem).then(mostrar).catch(falhou);
}

function onAnexoPreviewClick() {
  const preview = document.getElementById('aqAnexoPreview');
  const tipo = preview.dataset.tipo;
  if (!tipo) return;
  if (tipo === 'pdf') {
    if (preview.dataset.dataUrl) { window.open(preview.dataset.dataUrl, '_blank'); return; }
    if (preview.dataset.path) {
      api.readAquisicaoAttachment(preview.dataset.path)
        .then((dataUrl) => window.open(dataUrl, '_blank'))
        .catch(() => toast('Não foi possível abrir o PDF.', 'error'));
    }
    return;
  }
  ampliarAnexoImagem(preview.dataset.dataUrl || preview.dataset.path);
}

function openAquisicaoModal(id) {
  if (!fornecedores.length) { toast('Cadastre um fornecedor primeiro (Fornecedores e Prestadores de Serviços).', 'error'); return; }
  const a = id ? aquisicoes.find((x) => x.id === id) : null;
  document.getElementById('modalAquisicaoTitulo').textContent = id ? 'Editar Aquisição' : 'Nova Aquisição';
  document.getElementById('aqId').value = id || '';
  anexoPendenteBase64 = null;
  anexoPendenteTipo = null;
  anexoRemovido = false;
  resetAnexoPreview(a);
  popularFornecedorSelect(a);
  document.getElementById('aqDescricao').value = a ? a.descricao : '';
  document.getElementById('aqNotaFiscal').value = a ? a.notaFiscal : '';
  setMoneyMaskedValue(document.getElementById('aqValor'), a ? a.valor : 0);
  document.getElementById('aqDataCompra').value = a ? a.dataCompra : new Date().toISOString().slice(0, 10);
  document.getElementById('aqObservacoes').value = a ? a.observacoes : '';
  openModal('modalAquisicao');
}

async function saveAquisicao() {
  const id = document.getElementById('aqId').value;
  const fornecedorId = document.getElementById('aqFornecedorId').value;
  const descricao = document.getElementById('aqDescricao').value.trim();
  const dataCompra = document.getElementById('aqDataCompra').value;
  if (!fornecedorId) { toast('Selecione o fornecedor.', 'error'); return; }
  if (!descricao) { toast('Informe a descrição da compra.', 'error'); return; }
  if (!dataCompra) { toast('Informe a data da compra.', 'error'); return; }
  const payload = {
    id: id || uid('aq_'),
    fornecedorId, descricao,
    notaFiscal: document.getElementById('aqNotaFiscal').value.trim(),
    valor: parseMoneyMasked(document.getElementById('aqValor').value),
    dataCompra,
    observacoes: document.getElementById('aqObservacoes').value.trim(),
    createdAt: id ? '' : nowIso(),
  };
  try {
    if (id) await api.updateAquisicao(payload);
    else await api.createAquisicao(payload);

    if (anexoPendenteBase64) {
      await api.uploadAquisicaoAttachment({ aquisicaoId: payload.id, fileBase64: anexoPendenteBase64 });
    } else if (anexoRemovido) {
      await api.deleteAquisicaoAttachment(payload.id);
    }

    await reload();
    closeModal('modalAquisicao');
    toast('Aquisição salva.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function deleteAquisicao(id) {
  const a = aquisicoes.find((x) => x.id === id);
  if (!a) return;
  if (!confirm(`Excluir a aquisição "${a.descricao}"?`)) return;
  try {
    await api.deleteAquisicao(id);
    await reload();
    toast('Aquisição excluída.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
