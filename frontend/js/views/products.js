import { api, errorText } from '../api.js';
import { fmtBRL, fmtNum, escapeHtml, uid, nowIso, nowLocalInputValue, paraBusca } from '../format.js';
import { openModal, closeModal } from '../components/modal.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';
import { openFotoAmpliada } from '../components/fotoAmpliada.js';
import { enableRowSelection } from '../components/tableTools.js';
import { instalarFiltroSelect } from '../components/filtroSelect.js';
import { bindMoneyMask, setMoneyMaskedValue, parseMoneyMasked } from '../masks.js';

// As seis categorias administrativas do sistema — fixas, nunca texto livre
// (ver core-cpp/src/inventory_engine.cpp::kValidCategories, a mesma lista).
// "Não Classificado" NÃO está aqui de propósito: é um estado transitório
// que só a migração de dados legados atribui, nunca uma opção oferecida
// pra quem está cadastrando ou editando um produto.
const CATEGORIAS = ['Papelaria', 'Informática', 'Assembleia', 'Gráfica', 'Brinde', 'Valor'];
const NAO_CLASSIFICADO = 'Não Classificado';

let products = [];
let departments = [];
let availability = [];

let wired = false;

// Estado da foto pendente no modal de produto — só é gravada (upload ou
// remoção) quando "Salvar" é clicado (item 3 do pedido: "confirmar a
// imagem"), nunca no instante em que o usuário escolhe o arquivo.
let fotoPendenteBase64 = null;
let fotoRemovida = false;

// Recriado a cada renderTable() — desconectar o anterior evita observar
// células que já saíram do DOM (innerHTML substitui a tabela inteira a
// cada filtro/reload).
let thumbObserver = null;

/* Lançamentos em massa (entrada e baixa): o que o usuário já digitou vive
   AQUI, não nos <input> da tabela.

   A tabela é reconstruída inteira a cada tecla da busca — se o valor morasse
   só no DOM, filtrar para procurar o próximo material apagaria tudo o que já
   tinha sido preenchido. Pior: salvar com um filtro ativo gravaria apenas as
   linhas visíveis, perdendo em silêncio os itens filtrados para fora.
   Guardando por productId, o filtro passa a ser só uma lente sobre a lista —
   o lote acumula até o usuário mandar registrar.

   O valor é guardado como TEXTO (o mesmo que está no campo), e não como
   número: converter só na hora de usar preserva o que a pessoa digitou
   enquanto digita ("1.", "0,5" a meio caminho) sem o campo pular sozinho. */
let bmQtds = {};     // baixa em massa:  productId -> quantidade digitada
let emQtds = {};     // entrada em massa: productId -> quantidade recebida
let emPrecos = {};   // entrada em massa: productId -> preço unitário

// "1,5" (vírgula, como se digita em português) e "1.5" viram o mesmo número;
// vazio/inválido vira NaN, que os filtros de > 0 descartam naturalmente.
function numeroDigitado(txt) {
  if (txt === undefined || txt === null || String(txt).trim() === '') return NaN;
  return parseFloat(String(txt).replace(',', '.'));
}

// Idempotente: a Linha do Tempo também chama isto para abrir os modais de
// novo lançamento (que moram aqui). Sem a guarda, cada chamada empilharia
// mais um listener em cada botão e "Salvar" gravaria duas vezes.
export async function initProducts() {
  if (!wired) {
    wireControls();
    wired = true;
  }
  await reload();
}

function wireControls() {
  document.getElementById('btnNovoProduto').addEventListener('click', () => openProductModal());
  document.getElementById('btnRegistrarEntrada').addEventListener('click', () => openEntradaModal());
  document.getElementById('btnEntradaMassa').addEventListener('click', () => openEntradaMassaModal());
  document.getElementById('btnSalvarEntradaMassa').addEventListener('click', saveEntradaMassa);
  document.getElementById('emBusca').addEventListener('input', renderEntradaMassaTable);
  document.getElementById('emTbody').addEventListener('input', (e) => {
    const tr = e.target.closest('tr[data-produto]');
    if (!tr) return;
    const id = tr.dataset.produto;
    if (e.target.matches('.em-qtd')) {
      if (String(e.target.value).trim() === '') delete emQtds[id];
      else emQtds[id] = e.target.value;
      updateEntradaMassaInfo();
    } else if (e.target.matches('.em-preco')) {
      // Guardado mesmo sem quantidade: quem ajusta o preço antes de digitar
      // a quantidade não pode perder o ajuste ao filtrar.
      emPrecos[id] = e.target.value;
      updateEntradaMassaInfo();
    }
  });
  document.getElementById('btnBaixaProdutos').addEventListener('click', () => openSaidaModal());
  document.getElementById('btnBaixaMassa').addEventListener('click', () => openBaixaMassaModal());
  document.getElementById('btnSalvarBaixaMassa').addEventListener('click', saveBaixaMassa);
  document.getElementById('bmBusca').addEventListener('input', renderBaixaMassaTable);
  document.getElementById('bmDepartamento').addEventListener('change', updateBaixaMassaEncarregado);
  document.getElementById('bmTbody').addEventListener('input', (e) => {
    if (!e.target.matches('.bm-qtd')) return;
    const id = e.target.closest('tr[data-produto]').dataset.produto;
    // Campo esvaziado sai do lote; qualquer outro texto é guardado como veio
    // (a conversão acontece só na hora de somar/gravar).
    if (String(e.target.value).trim() === '') delete bmQtds[id];
    else bmQtds[id] = e.target.value;
    updateBaixaMassaInfo();
  });
  document.getElementById('btnCorrigirEstoque').addEventListener('click', () => openCorrecaoModal());
  document.getElementById('filtroProdutoNome').addEventListener('input', renderTable);
  document.getElementById('filtroProdutoStatus').addEventListener('change', renderTable);
  document.getElementById('filtroProdutoCategoria').addEventListener('change', renderTable);
  enableRowSelection(document.getElementById('productsTbody'));
  document.getElementById('prodCategoria').addEventListener('change', () => {
    document.getElementById('prodCategoriaAviso').style.display =
      document.getElementById('prodCategoria').value === NAO_CLASSIFICADO ? 'block' : 'none';
  });
  document.getElementById('btnEscolherFoto').addEventListener('click', () => document.getElementById('prodFotoInput').click());
  document.getElementById('prodFotoInput').addEventListener('change', onFotoSelecionada);
  document.getElementById('btnRemoverFoto').addEventListener('click', onRemoverFotoClick);
  document.getElementById('prodFotoPreview').addEventListener('click', () => {
    const preview = document.getElementById('prodFotoPreview');
    const origem = preview.dataset.dataUrl || preview.dataset.imagePath;
    if (origem) openFotoAmpliada(origem, document.getElementById('prodNome').value);
  });
  document.getElementById('btnSalvarProduto').addEventListener('click', saveProduct);
  document.getElementById('btnSalvarEntrada').addEventListener('click', saveEntrada);
  document.getElementById('btnSalvarSaida').addEventListener('click', saveSaida);
  document.getElementById('btnSalvarCorrecao').addEventListener('click', saveCorrecao);
  bindMoneyMask(document.getElementById('prodCustoInicial'));
  bindMoneyMask(document.getElementById('prodCustoMedio'));
  bindMoneyMask(document.getElementById('entPreco'));
  ['entProduto', 'entQtd', 'entPreco'].forEach((id) => document.getElementById(id).addEventListener('input', updateEntradaInfo));
  ['saiProduto', 'saiQtd', 'saiDepartamento'].forEach((id) => document.getElementById(id).addEventListener('input', updateSaidaInfo));
  ['corProduto', 'corQtdReal'].forEach((id) => document.getElementById(id).addEventListener('input', updateCorrecaoInfo));
  // Trocar de produto reinicia a quantidade contada com o saldo do NOVO item.
  // Sem isto, a contagem digitada (ou pré-carregada) para o produto anterior
  // fica no campo e vira um ajuste falso contra outro material — o modal
  // mostraria, por exemplo, "Ajuste: -36,29" só porque os dois produtos têm
  // saldos diferentes. Fica em 'change' (e não no 'input' acima) para não
  // apagar o número enquanto o usuário digita a contagem.
  document.getElementById('corProduto').addEventListener('change', semearQtdContada);
}

function semearQtdContada() {
  const p = products.find((x) => x.id === document.getElementById('corProduto').value);
  document.getElementById('corQtdReal').value = p ? p.qty : 0;
  updateCorrecaoInfo();
}

export async function reload() {
  // A disponibilidade traz o que está RESERVADO por requisições em aberto —
  // sem ela, a tela mostraria como livre um saldo que já tem dono.
  [products, departments, availability] = await Promise.all([
    api.listProducts(), api.listDepartments(), api.stockAvailability(),
  ]);
  aplicarPermissoes();
  renderStatGrid();
  renderCategoriaBreakdown();
  renderTable();
  // Os modais desta view também são usados pela Linha do Tempo; avisar que os
  // dados mudaram evita que ela mostre uma tabela defasada depois de um
  // lançamento novo feito de lá.
  document.dispatchEvent(new CustomEvent('estoque:dados-alterados'));
}

/* Esconde o que o usuário não pode fazer. O backend recusaria de qualquer
   forma (ver api.cpp); isto evita oferecer o botão para depois negar. */
function aplicarPermissoes() {
  const mostrar = (id, pode) => { document.getElementById(id).style.display = pode ? '' : 'none'; };
  mostrar('btnNovoProduto', can('produtos', 'create'));
  mostrar('btnRegistrarEntrada', can('produtos', 'create') || can('linha_do_tempo', 'create'));
  mostrar('btnBaixaProdutos', can('produtos', 'create') || can('linha_do_tempo', 'create'));
  mostrar('btnCorrigirEstoque', can('produtos', 'update') || can('linha_do_tempo', 'create'));
}

function reservadoDe(productId) {
  const a = availability.find((x) => x.productId === productId);
  return a ? a.reserved : 0;
}

function renderStatGrid() {
  const valorTotal = products.reduce((s, p) => s + p.qty * p.avgCost, 0);
  const abaixo = products.filter((p) => p.qty <= p.minStock).length;
  const reservado = availability.reduce((s, a) => s + a.reserved, 0);
  document.getElementById('productsStatGrid').innerHTML = [
    { label: 'Produtos cadastrados', value: fmtNum(products.length) },
    { label: 'Valor total em estoque', value: fmtBRL(valorTotal) },
    { label: 'Abaixo do mínimo', value: fmtNum(abaixo), cls: abaixo > 0 ? 'is-critical' : 'is-good' },
    { label: 'Reservado em requisições', value: fmtNum(reservado) },
  ].map((t) => `<div class="stat-tile ${t.cls || ''}"><div class="label">${escapeHtml(t.label)}</div><div class="value">${t.value}</div></div>`).join('');
}

// Indicadores por categoria (item 7 do pedido de categorias): quantos itens,
// quanto em estoque (qtd e R$) e quantos abaixo do mínimo — por categoria,
// nas seis fixas + "Não Classificado" só se houver algum (não polui a
// tabela com uma linha zerada pra quem já reclassificou tudo).
function renderCategoriaBreakdown() {
  const cats = [...CATEGORIAS];
  if (products.some((p) => p.category === NAO_CLASSIFICADO)) cats.push(NAO_CLASSIFICADO);
  const linhas = cats.map((cat) => {
    const doGrupo = products.filter((p) => p.category === cat);
    const valor = doGrupo.reduce((s, p) => s + p.qty * p.avgCost, 0);
    const qtd = doGrupo.reduce((s, p) => s + p.qty, 0);
    const abaixo = doGrupo.filter((p) => p.qty <= p.minStock).length;
    return { cat, n: doGrupo.length, qtd, valor, abaixo };
  });
  document.getElementById('categoriaBreakdownTbody').innerHTML = linhas.map((l) => `<tr>
      <td>${l.cat === NAO_CLASSIFICADO ? `<span class="pill pill-low">${escapeHtml(l.cat)}</span>` : escapeHtml(l.cat)}</td>
      <td class="num">${fmtNum(l.n)}</td>
      <td class="num">${fmtNum(l.qtd)}</td>
      <td class="num">${fmtBRL(l.valor)}</td>
      <td class="num">${l.abaixo > 0 ? fmtNum(l.abaixo) : '<span class="muted">—</span>'}</td>
    </tr>`).join('');
}

function renderTable() {
  const nome = (document.getElementById('filtroProdutoNome').value || '').toLowerCase();
  const status = document.getElementById('filtroProdutoStatus').value;
  const categoria = document.getElementById('filtroProdutoCategoria').value;
  let list = [...products].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  if (nome) list = list.filter((p) => p.name.toLowerCase().includes(nome) || (p.sku || '').toLowerCase().includes(nome));
  if (status === 'low') list = list.filter((p) => p.qty <= p.minStock);
  if (status === 'ok') list = list.filter((p) => p.qty > p.minStock);
  if (categoria) list = list.filter((p) => p.category === categoria);

  const tbody = document.getElementById('productsTbody');
  if (!list.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="13">${products.length === 0 ? 'Nenhum produto cadastrado.' : 'Nenhum produto com esses filtros.'}</td></tr>`;
    return;
  }
  const podeLancar = can('produtos', 'create') || can('linha_do_tempo', 'create');
  tbody.innerHTML = list.map((p) => {
    const low = p.qty <= p.minStock;
    const semClassificar = p.category === NAO_CLASSIFICADO;
    const reservado = reservadoDe(p.id);
    const acoes = [
      podeLancar ? `<button class="btn-sm btn-outline" data-entrada="${p.id}" title="Registrar entrada">↓</button>` : '',
      podeLancar ? `<button class="btn-sm btn-outline" data-saida="${p.id}" title="Registrar baixa">↑</button>` : '',
      can('produtos', 'update') ? `<button class="btn-sm btn-ghost" data-editar="${p.id}">Editar</button>` : '',
      can('produtos', 'delete') ? `<button class="btn-sm btn-danger" data-excluir="${p.id}">Excluir</button>` : '',
    ].filter(Boolean).join('');
    return `<tr>
      <td>${p.thumbnailPath
        ? `<div class="thumb-cell" data-thumb-path="${escapeHtml(p.thumbnailPath)}" data-image-path="${escapeHtml(p.imagePath || p.thumbnailPath)}" data-nome="${escapeHtml(p.name)}" title="Clique para ampliar"><span class="thumb-placeholder"></span></div>`
        : `<div class="thumb-cell"><span class="thumb-placeholder"></span></div>`}</td>
      <td>${escapeHtml(p.sku || '') || '<span class="muted">—</span>'}</td><td><b>${escapeHtml(p.name)}</b></td>
      <td>${p.category ? `<span class="${semClassificar ? 'pill pill-low' : ''}">${escapeHtml(p.category)}</span>` : '<span class="muted">—</span>'}</td>
      <td>${escapeHtml(p.unit)}</td><td class="num">${fmtNum(p.qty)}</td>
      <td class="num">${reservado > 0 ? fmtNum(reservado) : '<span class="muted">—</span>'}</td>
      <td class="num">${fmtNum(p.qty - reservado)}</td><td class="num">${fmtNum(p.minStock)}</td>
      <td class="num">${fmtBRL(p.avgCost)}</td><td class="num">${fmtBRL(p.qty * p.avgCost)}</td>
      <td><span class="pill ${low ? 'pill-low' : 'pill-ok'}">${low ? 'Abaixo do mínimo' : 'OK'}</span></td>
      <td><div class="row-actions">${acoes || '<span class="muted">—</span>'}</div></td></tr>`;
  }).join('');
  tbody.querySelectorAll('[data-entrada]').forEach((b) => b.addEventListener('click', () => openEntradaModal(b.dataset.entrada)));
  tbody.querySelectorAll('[data-saida]').forEach((b) => b.addEventListener('click', () => openSaidaModal(b.dataset.saida)));
  tbody.querySelectorAll('[data-editar]').forEach((b) => b.addEventListener('click', () => openProductModal(b.dataset.editar)));
  tbody.querySelectorAll('[data-excluir]').forEach((b) => b.addEventListener('click', () => deleteProduct(b.dataset.excluir)));
  // Só a célula que TEM foto (data-thumb-path) reage — a caixa vazia não tem
  // nada pra ampliar (ver o CSS: cursor:pointer também só aparece nesse caso).
  tbody.querySelectorAll('.thumb-cell[data-thumb-path]').forEach((el) =>
    el.addEventListener('click', () => openFotoAmpliada(el.dataset.imagePath, el.dataset.nome)));

  // Lazy loading das miniaturas (item 7 do pedido de fotos): só busca o
  // arquivo quando a célula entra na área visível, nunca todas de uma vez.
  if (thumbObserver) thumbObserver.disconnect();
  thumbObserver = new IntersectionObserver((entries) => {
    entries.forEach((entry) => {
      if (!entry.isIntersecting) return;
      const cell = entry.target;
      thumbObserver.unobserve(cell);
      const path = cell.dataset.thumbPath;
      api.readProductImage(path)
        .then((dataUrl) => { cell.innerHTML = `<img src="${dataUrl}" alt="" loading="lazy" />`; })
        .catch(() => {}); // mantém o placeholder — nunca mostra imagem quebrada
    });
  }, { root: document.querySelector('.content'), rootMargin: '200px' });
  tbody.querySelectorAll('.thumb-cell[data-thumb-path]').forEach((el) => thumbObserver.observe(el));
}

function fillProductSelect(selectId, selectedId) {
  const sel = document.getElementById(selectId);
  const sorted = [...products].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  sel.innerHTML = sorted.map((p) => `<option value="${p.id}">${escapeHtml(p.name)} (${fmtNum(p.qty)} ${escapeHtml(p.unit)})</option>`).join('');
  if (selectedId) sel.value = selectedId;
  // Caixa de busca acima da lista — com o catálogo cheio, rolar até o item é
  // inviável. Instalada DEPOIS do preenchimento (ela fotografa as opções).
  instalarFiltroSelect(selectId, 'Digite o nome do material...');
  // Um item pré-escolhido (veio do botão da linha do produto) precisa
  // sobreviver à instalação do filtro, que reescreve o <select>.
  if (selectedId) sel.value = selectedId;
}
function fillDepartmentSelect(selectId) {
  const sel = document.getElementById(selectId);
  const sorted = [...departments].sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  sel.innerHTML = sorted.length
    ? sorted.map((d) => `<option value="${d.id}">${escapeHtml(d.name)}</option>`).join('')
    : '<option value="">Nenhum departamento cadastrado</option>';
}
/* O datalist de solicitantes é preenchido pela Linha do Tempo, que é quem
   carrega os lançamentos (a lista de nomes sai deles). Aqui só NÃO se apaga
   o que já estiver lá — era o que esta função fazia antes, deixando o campo
   sempre sem sugestões. */

// Mostra a foto atual do produto (via miniatura, mais rápida de buscar) ou
// o placeholder "Sem foto" (item 10 do pedido — nunca imagem quebrada nem
// caminho de arquivo na tela).
function resetFotoPreview(p) {
  const preview = document.getElementById('prodFotoPreview');
  const btnRemover = document.getElementById('btnRemoverFoto');
  // data-image-path é o que o clique no preview usa pra buscar a foto GRANDE
  // (item 10 do pedido de fotos): a miniatura carregada aqui é só pra prévia
  // rápida de 72px, nunca o que abre no modal ampliado.
  delete preview.dataset.dataUrl;
  if (p && p.thumbnailPath) {
    preview.dataset.imagePath = p.imagePath || p.thumbnailPath;
    preview.innerHTML = '<span class="foto-placeholder">Carregando...</span>';
    btnRemover.style.display = 'inline-block';
    api.readProductImage(p.thumbnailPath)
      .then((dataUrl) => { preview.innerHTML = `<img src="${dataUrl}" alt="Foto do produto" />`; })
      .catch(() => { preview.innerHTML = '<span class="foto-placeholder">Sem foto</span>'; });
  } else {
    delete preview.dataset.imagePath;
    preview.innerHTML = '<span class="foto-placeholder">Sem foto</span>';
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

const TIPOS_FOTO_ACEITOS = ['image/jpeg', 'image/jpg', 'image/png', 'image/webp'];

async function onFotoSelecionada(e) {
  const file = e.target.files && e.target.files[0];
  e.target.value = ''; // permite escolher o mesmo arquivo de novo mais tarde
  if (!file) return;
  if (!TIPOS_FOTO_ACEITOS.includes(file.type)) {
    toast('Selecione uma imagem JPG, PNG ou WebP.', 'error');
    return;
  }
  try {
    const base64 = await fileToBase64(file);
    fotoPendenteBase64 = base64;
    fotoRemovida = false;
    const preview = document.getElementById('prodFotoPreview');
    const dataUrl = `data:${file.type};base64,${base64}`;
    // Arquivo recém-escolhido ainda não tem caminho no disco (só grava ao
    // Salvar) — o clique pra ampliar usa a mesma data URL já em memória, sem
    // caminho nenhum pra buscar.
    delete preview.dataset.imagePath;
    preview.dataset.dataUrl = dataUrl;
    preview.innerHTML = `<img src="${dataUrl}" alt="Prévia da foto" />`;
    document.getElementById('btnRemoverFoto').style.display = 'inline-block';
  } catch (err) {
    toast('Erro ao ler o arquivo: ' + err, 'error');
  }
}

function onRemoverFotoClick() {
  fotoPendenteBase64 = null;
  fotoRemovida = true;
  const preview = document.getElementById('prodFotoPreview');
  delete preview.dataset.imagePath;
  delete preview.dataset.dataUrl;
  preview.innerHTML = '<span class="foto-placeholder">Sem foto</span>';
  document.getElementById('btnRemoverFoto').style.display = 'none';
}


function openProductModal(id) {
  document.getElementById('modalProdutoTitulo').textContent = id ? 'Editar Produto' : 'Novo Produto';
  document.getElementById('prodId').value = id || '';
  const inicialBox = document.getElementById('prodInicialBox');
  const inicialInfo = document.getElementById('prodInicialInfo');
  const correcaoBox = document.getElementById('prodCorrecaoBox');
  const skuBox = document.getElementById('prodSkuBox');
  const catSelect = document.getElementById('prodCategoria');
  const catAviso = document.getElementById('prodCategoriaAviso');
  // "Não Classificado" só existe como opção quando é o valor ATUAL do
  // produto sendo editado — nunca é oferecido pra cadastro novo, e some do
  // select assim que o usuário troca pra uma categoria de verdade.
  [...catSelect.querySelectorAll(`option[value="${NAO_CLASSIFICADO}"]`)].forEach((o) => o.remove());
  fotoPendenteBase64 = null;
  fotoRemovida = false;
  resetFotoPreview(id ? products.find((x) => x.id === id) : null);
  if (id) {
    const p = products.find((x) => x.id === id);
    document.getElementById('prodNome').value = p.name;
    const unidadeSelect = document.getElementById('prodUnidadeSelect');
    // Defesa contra unidade fora da lista fixa (ex.: "Saco", que faltava
    // aqui até um produto real de cimento/adubo topar com isso): setar
    // .value pra uma option que não existe deixa o <select> em branco, SEM
    // erro nenhum — e a validação de "preencha nome e unidade" barra até
    // salvar coisas sem relação (ex.: só trocar a foto). Se a unidade do
    // produto não estiver entre as options, adiciona ela na hora, do mesmo
    // jeito que já se faz para categoria "Não Classificado" logo abaixo.
    if (p.unit && ![...unidadeSelect.options].some((o) => o.value === p.unit)) {
      unidadeSelect.appendChild(new Option(p.unit, p.unit));
    }
    unidadeSelect.value = p.unit;
    document.getElementById('prodMinimo').value = p.minStock;
    setMoneyMaskedValue(document.getElementById('prodCustoMedio'), p.avgCost);
    skuBox.style.display = 'block';
    document.getElementById('prodSku').value = p.sku || '(sem SKU — abra o app uma vez para gerar)';
    if (p.category === NAO_CLASSIFICADO) {
      const opt = document.createElement('option');
      opt.value = NAO_CLASSIFICADO;
      opt.textContent = NAO_CLASSIFICADO + ' (revisar)';
      catSelect.appendChild(opt);
    }
    catSelect.value = p.category || '';
    catAviso.style.display = p.category === NAO_CLASSIFICADO ? 'block' : 'none';
    inicialBox.style.display = 'none';
    inicialInfo.style.display = 'none';
    correcaoBox.style.display = 'block';
  } else {
    document.getElementById('prodNome').value = '';
    document.getElementById('prodUnidadeSelect').value = 'Unidade';
    document.getElementById('prodMinimo').value = 0;
    catSelect.value = '';
    catAviso.style.display = 'none';
    skuBox.style.display = 'none';
    document.getElementById('prodQtdInicial').value = 0;
    setMoneyMaskedValue(document.getElementById('prodCustoInicial'), 0);
    inicialBox.style.display = 'grid';
    inicialInfo.style.display = 'block';
    correcaoBox.style.display = 'none';
  }
  openModal('modalProduto');
}

async function saveProduct() {
  const id = document.getElementById('prodId').value;
  const name = document.getElementById('prodNome').value.trim();
  const unit = document.getElementById('prodUnidadeSelect').value;
  const minStock = parseFloat(document.getElementById('prodMinimo').value) || 0;
  const category = document.getElementById('prodCategoria').value;
  if (!name || !unit) { toast('Preencha nome e unidade.', 'error'); return; }
  if (!category || category === NAO_CLASSIFICADO) { toast('Selecione uma categoria válida.', 'error'); return; }
  try {
    let productId, sku;
    if (id) {
      const p = products.find((x) => x.id === id);
      const updated = await api.updateProduct({ id, name, unit, minStock, category, qty: 0, avgCost: 0, createdAt: '' });
      productId = id;
      sku = updated.sku;
      const novoCusto = parseMoneyMasked(document.getElementById('prodCustoMedio').value);
      if (p && novoCusto >= 0 && Math.abs(novoCusto - p.avgCost) > 0.0001) {
        const agora = nowIso();
        await api.applyCorrecao({
          movementId: uid('m_'), productId: id, qtyReal: p.qty,
          motivo: 'Correção do custo médio via edição do produto', date: agora, createdAt: agora,
          newAvgCost: novoCusto,
        });
      }
    } else {
      const newId = uid('p_');
      const created = new Date().toISOString();
      // O SKU só existe DEPOIS de criar (o backend que gera) — é por isso
      // que o upload da foto (que precisa do SKU pra saber em que pasta
      // gravar) só pode acontecer depois desta chamada, nunca antes.
      const createdProduct = await api.createProduct({ id: newId, name, unit, minStock, category, qty: 0, avgCost: 0, createdAt: created });
      productId = newId;
      sku = createdProduct.sku;
      const qtdInicial = parseFloat(document.getElementById('prodQtdInicial').value) || 0;
      const custoInicial = parseMoneyMasked(document.getElementById('prodCustoInicial').value);
      if (qtdInicial > 0) {
        await api.applyEntrada({ movementId: uid('m_'), productId: newId, qty: qtdInicial, unitPrice: custoInicial, supplier: 'Saldo inicial', nf: '-', date: created, obs: 'Cadastro inicial do produto', createdAt: created });
      }
    }

    if (fotoPendenteBase64) {
      await api.uploadProductImage({ productId, sku, fileBase64: fotoPendenteBase64 });
    } else if (fotoRemovida) {
      await api.deleteProductImage({ productId, sku });
    }

    await reload();
    closeModal('modalProduto');
    toast('Produto salvo com sucesso.', 'success');
  } catch (e) { toast('Erro ao salvar: ' + e, 'error'); }
}

async function deleteProduct(id) {
  const p = products.find((x) => x.id === id);
  if (!p) return;
  if (!confirm(`Excluir "${p.name}"? O histórico de movimentações dele também será removido.`)) return;
  await api.deleteProduct(id, p.sku);
  await reload();
  toast('Produto excluído.', 'success');
}

function openEntradaModal(productId) {
  fillProductSelect('entProduto', productId);
  document.getElementById('entQtd').value = '';
  setMoneyMaskedValue(document.getElementById('entPreco'), 0);
  document.getElementById('entFornecedor').value = '';
  document.getElementById('entNF').value = '';
  document.getElementById('entObs').value = '';
  document.getElementById('entData').value = nowLocalInputValue();
  updateEntradaInfo();
  openModal('modalEntrada');
}
function updateEntradaInfo() {
  const p = products.find((x) => x.id === document.getElementById('entProduto').value);
  const box = document.getElementById('entInfoBox');
  if (!p) { box.textContent = 'Cadastre um produto primeiro.'; return; }
  const qtd = parseFloat(document.getElementById('entQtd').value) || 0;
  const preco = parseMoneyMasked(document.getElementById('entPreco').value);
  const baseQtd = Math.max(p.qty, 0);
  const novaQtd = p.qty + qtd;
  const novoCusto = (baseQtd + qtd) > 0 ? (baseQtd * p.avgCost + qtd * preco) / (baseQtd + qtd) : 0;
  box.innerHTML = `Estoque atual: <b>${fmtNum(p.qty)} ${p.unit}</b> a ${fmtBRL(p.avgCost)}<br>Após esta entrada: <b>${fmtNum(novaQtd)} ${p.unit}</b> — novo custo médio: <b>${fmtBRL(novoCusto)}</b>`;
}
async function saveEntrada() {
  const p = products.find((x) => x.id === document.getElementById('entProduto').value);
  const qtd = parseFloat(document.getElementById('entQtd').value);
  const preco = parseMoneyMasked(document.getElementById('entPreco').value);
  const fornecedor = document.getElementById('entFornecedor').value.trim();
  const nf = document.getElementById('entNF').value.trim();
  const dataVal = document.getElementById('entData').value;
  const obs = document.getElementById('entObs').value.trim();
  if (!p) { toast('Selecione um produto.', 'error'); return; }
  if (!qtd || qtd <= 0) { toast('Informe uma quantidade válida.', 'error'); return; }
  if (preco <= 0) { toast('Informe um preço válido.', 'error'); return; }
  if (!fornecedor || !nf) { toast('Informe fornecedor e nota fiscal.', 'error'); return; }
  try {
    const dataISO = dataVal ? new Date(dataVal).toISOString() : nowIso();
    await api.applyEntrada({ movementId: uid('m_'), productId: p.id, qty: qtd, unitPrice: preco, supplier: fornecedor, nf, date: dataISO, obs, createdAt: nowIso() });
    await reload();
    closeModal('modalEntrada');
    toast('Entrada registrada.', 'success');
  } catch (e) { toast('Erro: ' + e, 'error'); }
}

function openEntradaMassaModal() {
  document.getElementById('emFornecedor').value = '';
  document.getElementById('emNF').value = '';
  document.getElementById('emObs').value = '';
  document.getElementById('emData').value = nowLocalInputValue();
  document.getElementById('emBusca').value = '';
  emQtds = {};    // lote novo a cada abertura
  emPrecos = {};
  renderEntradaMassaTable();
  openModal('modalEntradaMassa');
}
function renderEntradaMassaTable() {
  const termo = paraBusca(document.getElementById('emBusca').value);
  const list = [...products]
    .filter((p) => !termo || paraBusca(p.name).includes(termo) || paraBusca(p.sku).includes(termo))
    .sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));
  const tbody = document.getElementById('emTbody');
  if (!list.length) {
    tbody.innerHTML = `<tr class="empty-row"><td colspan="4">Nenhum produto encontrado.</td></tr>`;
  } else {
    // Quantidade e preço vêm do lote em memória, não do DOM — é o que faz o
    // filtro não apagar o que já foi digitado. O preço cai no custo médio
    // atual só enquanto ninguém o tiver alterado neste lote.
    tbody.innerHTML = list.map((p) => `<tr data-produto="${p.id}">
        <td><b>${escapeHtml(p.name)}</b>${p.sku ? `<div class="muted">${escapeHtml(p.sku)}</div>` : ''}</td>
        <td class="num">${fmtNum(p.qty)} ${escapeHtml(p.unit)}</td>
        <td class="num"><input type="number" class="em-qtd" min="0" step="any" placeholder="0" value="${escapeHtml(emQtds[p.id] || '')}" /></td>
        <td class="num"><input type="number" class="em-preco" min="0" step="any" value="${escapeHtml(emPrecos[p.id] !== undefined ? emPrecos[p.id] : (p.avgCost || ''))}" /></td>
      </tr>`).join('');
  }
  updateEntradaMassaInfo();
}

/* Itens do lote de entrada (independente do filtro). O preço usado é o que
   estiver no lote ou, se intocado, o custo médio atual do produto. */
function itensEntradaMassa() {
  return Object.keys(emQtds)
    .map((id) => {
      const p = products.find((x) => x.id === id);
      const preco = emPrecos[id] !== undefined ? numeroDigitado(emPrecos[id]) : (p ? p.avgCost : NaN);
      return { p, qtd: numeroDigitado(emQtds[id]), preco };
    })
    .filter((i) => i.p && i.qtd > 0)
    .sort((a, b) => a.p.name.localeCompare(b.p.name, 'pt-BR'));
}

function updateEntradaMassaInfo() {
  const escolhidos = itensEntradaMassa();
  const box = document.getElementById('emInfoBox');
  if (!escolhidos.length) {
    box.classList.remove('info-box-lote');
    box.textContent = 'Nenhuma quantidade preenchida ainda.';
    return;
  }
  const total = escolhidos.reduce((s, i) => s + i.qtd * (i.preco > 0 ? i.preco : 0), 0);
  box.classList.add('info-box-lote');
  box.innerHTML =
    `<b>${escolhidos.length} produto${escolhidos.length > 1 ? 's' : ''} no lote</b> — total: <b>${fmtBRL(total)}</b>` +
    '<ul class="lote-lista">' +
    escolhidos.map((i) => `<li><b>${fmtNum(i.qtd)} ${escapeHtml(i.p.unit)}</b> — ${escapeHtml(i.p.name)}` +
      ` <span class="muted">a ${i.preco > 0 ? fmtBRL(i.preco) : '—'}</span>` +
      `<button type="button" class="lote-tirar" data-tirar="${i.p.id}" title="Tirar do lote">✕</button></li>`).join('') +
    '</ul>';
  box.querySelectorAll('[data-tirar]').forEach((b) => b.addEventListener('click', () => {
    delete emQtds[b.dataset.tirar];
    const campo = document.querySelector(`#emTbody tr[data-produto="${b.dataset.tirar}"] .em-qtd`);
    if (campo) campo.value = '';
    updateEntradaMassaInfo();
  }));
}
async function saveEntradaMassa() {
  const fornecedor = document.getElementById('emFornecedor').value.trim();
  const nf = document.getElementById('emNF').value.trim();
  const obs = document.getElementById('emObs').value.trim();
  const dataVal = document.getElementById('emData').value;
  if (!fornecedor || !nf) { toast('Informe fornecedor e nota fiscal.', 'error'); return; }

  // Do lote em memória, não das linhas visíveis (ver o comentário de emQtds):
  // com filtro ativo, ler o DOM gravaria só parte da nota.
  const linhas = itensEntradaMassa()
    .map((i) => ({ productId: i.p.id, nome: i.p.name, qty: i.qtd, preco: i.preco }));
  if (!linhas.length) { toast('Preencha a quantidade de ao menos um produto.', 'error'); return; }
  const semPreco = linhas.find((l) => isNaN(l.preco) || l.preco < 0);
  if (semPreco) { toast(`Informe um preço válido para ${semPreco.nome}.`, 'error'); return; }

  const dataISO = dataVal ? new Date(dataVal).toISOString() : nowIso();
  try {
    for (const l of linhas) {
      await api.applyEntrada({ movementId: uid('m_'), productId: l.productId, qty: l.qty, unitPrice: l.preco, supplier: fornecedor, nf, date: dataISO, obs, createdAt: nowIso() });
    }
    await reload();
    closeModal('modalEntradaMassa');
    toast(`${linhas.length} entrada${linhas.length > 1 ? 's' : ''} registrada${linhas.length > 1 ? 's' : ''}.`, 'success');
  } catch (e) { toast('Erro: ' + e, 'error'); }
}

/* ---------------------------------------------- baixa em massa por depto

   O mesmo departamento costuma pedir vários materiais no mesmo dia, e nem
   sempre o pedido entra pela tela de Requisições — quem está no almoxarifado
   lança tudo aqui de uma vez. Departamento, data do pedido, solicitante e
   observação valem para TODOS os itens preenchidos, então cada saída gerada
   fica idêntica à que a requisição teria produzido (mesmo departamento,
   mesma data) e o relatório de custo por setor continua batendo. */
function openBaixaMassaModal() {
  fillDepartmentSelect('bmDepartamento');
  document.getElementById('bmSolicitante').value = '';
  document.getElementById('bmObs').value = '';
  document.getElementById('bmData').value = nowLocalInputValue();
  document.getElementById('bmBusca').value = '';
  bmQtds = {};  // lote novo a cada abertura
  updateBaixaMassaEncarregado();
  renderBaixaMassaTable();
  openModal('modalBaixaMassa');
}

function updateBaixaMassaEncarregado() {
  const dept = departments.find((x) => x.id === document.getElementById('bmDepartamento').value);
  document.getElementById('bmEncarregadoHint').textContent = dept ? 'Encarregado: ' + dept.encarregado : '';
}

function renderBaixaMassaTable() {
  const termo = paraBusca(document.getElementById('bmBusca').value);
  // Só materiais COM saldo: oferecer para baixa o que está zerado seria
  // convidar a um erro que o backend recusaria no fim da fila.
  const list = [...products]
    .filter((p) => p.qty > 0)
    .filter((p) => !termo || paraBusca(p.name).includes(termo) || paraBusca(p.sku).includes(termo))
    .sort((a, b) => a.name.localeCompare(b.name, 'pt-BR'));

  const tbody = document.getElementById('bmTbody');
  if (!list.length) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="5">Nenhum material com saldo encontrado.</td></tr>';
    updateBaixaMassaInfo();
    return;
  }
  tbody.innerHTML = list.map((p) => {
    const reservado = reservadoDe(p.id);
    return `<tr data-produto="${p.id}">
      <td>${p.thumbnailPath
        ? `<div class="thumb-cell" data-thumb-path="${escapeHtml(p.thumbnailPath)}" data-image-path="${escapeHtml(p.imagePath || p.thumbnailPath)}" data-nome="${escapeHtml(p.name)}" title="Clique para ampliar"><span class="thumb-placeholder"></span></div>`
        : '<div class="thumb-cell"><span class="thumb-placeholder"></span></div>'}</td>
      <td><b>${escapeHtml(p.name)}</b>${p.sku ? `<div class="muted">${escapeHtml(p.sku)}</div>` : ''}</td>
      <td class="num">${fmtNum(p.qty)} ${escapeHtml(p.unit)}
        ${reservado > 0 ? `<div class="muted">${fmtNum(reservado)} reservado em requisições</div>` : ''}</td>
      <td class="num"><input type="number" class="bm-qtd" min="0" max="${p.qty}" step="any" placeholder="0" value="${escapeHtml(bmQtds[p.id] || '')}" /></td>
      <td class="num bm-valor">—</td>
    </tr>`;
  }).join('');

  // Miniaturas: carrega e liga o clique-para-ampliar, como na tabela principal.
  tbody.querySelectorAll('.thumb-cell[data-thumb-path]').forEach((el) => {
    api.readProductImage(el.dataset.thumbPath)
      .then((dataUrl) => { el.innerHTML = `<img src="${dataUrl}" alt="" loading="lazy" />`; })
      .catch(() => {});
    el.addEventListener('click', () => openFotoAmpliada(el.dataset.imagePath, el.dataset.nome));
  });
  updateBaixaMassaInfo();
}

/* Itens do lote (independente do filtro): [{p, qtd}] em ordem alfabética. */
function itensBaixaMassa() {
  return Object.keys(bmQtds)
    .map((id) => ({ p: products.find((x) => x.id === id), qtd: numeroDigitado(bmQtds[id]) }))
    .filter((i) => i.p && i.qtd > 0)
    .sort((a, b) => a.p.name.localeCompare(b.p.name, 'pt-BR'));
}

function updateBaixaMassaInfo() {
  const escolhidos = itensBaixaMassa();
  const total = escolhidos.reduce((s, i) => s + i.qtd * i.p.avgCost, 0);
  const excedidos = escolhidos.filter((i) => i.qtd > i.p.qty);

  // Valor por linha e destaque de excesso — só das linhas VISÍVEIS, que são
  // as únicas que existem no DOM agora; a conta acima já cobriu o resto.
  [...document.querySelectorAll('#bmTbody tr[data-produto]')].forEach((tr) => {
    const p = products.find((x) => x.id === tr.dataset.produto);
    const qtd = numeroDigitado(bmQtds[tr.dataset.produto]);
    const celula = tr.querySelector('.bm-valor');
    if (!p || !(qtd > 0)) { celula.textContent = '—'; tr.classList.remove('linha-excedida'); return; }
    celula.textContent = fmtBRL(qtd * p.avgCost);
    tr.classList.toggle('linha-excedida', qtd > p.qty);
  });

  const box = document.getElementById('bmInfoBox');
  if (excedidos.length) {
    box.innerHTML = '<b>Quantidade acima do estoque:</b> ' +
      escapeHtml(excedidos.map((i) => `${i.p.name} (só há ${fmtNum(i.p.qty)} ${i.p.unit})`).join(' · '));
    box.classList.add('info-box-erro');
    return;
  }
  box.classList.remove('info-box-erro');
  if (!escolhidos.length) {
    box.classList.remove('info-box-lote');
    box.textContent = 'Nenhuma quantidade preenchida ainda.';
    return;
  }
  // Lista o que já está no lote: com filtro ativo, os itens escolhidos antes
  // saem da tela, e sem esta lista não haveria como conferir o pedido inteiro
  // antes de registrar.
  box.classList.add('info-box-lote');
  box.innerHTML =
    `<b>${escolhidos.length} ${escolhidos.length > 1 ? 'materiais' : 'material'} no lote</b> — ` +
    `valor estimado: <b>${fmtBRL(total)}</b>` +
    '<ul class="lote-lista">' +
    escolhidos.map((i) => `<li><b>${fmtNum(i.qtd)} ${escapeHtml(i.p.unit)}</b> — ${escapeHtml(i.p.name)}` +
      `<button type="button" class="lote-tirar" data-tirar="${i.p.id}" title="Tirar do lote">✕</button></li>`).join('') +
    '</ul>';
  box.querySelectorAll('[data-tirar]').forEach((b) => b.addEventListener('click', () => {
    delete bmQtds[b.dataset.tirar];
    const campo = document.querySelector(`#bmTbody tr[data-produto="${b.dataset.tirar}"] .bm-qtd`);
    if (campo) campo.value = '';
    updateBaixaMassaInfo();
  }));
}

async function saveBaixaMassa() {
  const dept = departments.find((x) => x.id === document.getElementById('bmDepartamento').value);
  const dataVal = document.getElementById('bmData').value;
  const obs = document.getElementById('bmObs').value.trim();
  const solicitante = document.getElementById('bmSolicitante').value.trim();
  if (!dept) { toast('Selecione um departamento.', 'error'); return; }

  // Vem do estado do lote, NÃO das linhas visíveis: com um filtro ativo a
  // tabela mostra só parte dos materiais, e ler o DOM aqui gravaria apenas
  // esses, perdendo o resto do pedido sem avisar ninguém.
  const linhas = itensBaixaMassa().map((i) => ({ productId: i.p.id, qty: i.qtd }));
  if (!linhas.length) { toast('Preencha a quantidade de ao menos um material.', 'error'); return; }

  // Revalida contra o saldo antes de gravar QUALQUER coisa: as saídas são
  // lançadas uma a uma, então deixar uma linha inválida passar significaria
  // metade das baixas registradas e metade não.
  const invalida = linhas.map((l) => ({ l, p: products.find((x) => x.id === l.productId) }))
    .find(({ l, p }) => !p || l.qty > p.qty);
  if (invalida) {
    toast(invalida.p
      ? `${invalida.p.name}: quantidade maior que o disponível (${fmtNum(invalida.p.qty)} ${invalida.p.unit}).`
      : 'Um dos materiais não existe mais — feche e abra a janela.', 'error');
    return;
  }

  const botao = document.getElementById('btnSalvarBaixaMassa');
  botao.disabled = true;
  const dataISO = dataVal ? new Date(dataVal).toISOString() : nowIso();
  const feitas = [];
  try {
    for (const l of linhas) {
      await api.applySaida({ movementId: uid('m_'), productId: l.productId, qty: l.qty,
        departmentId: dept.id, date: dataISO, obs, requester: solicitante, createdAt: nowIso() });
      feitas.push(l);
    }
    await reload();
    closeModal('modalBaixaMassa');
    toast(`${feitas.length} baixa${feitas.length > 1 ? 's' : ''} registrada${feitas.length > 1 ? 's' : ''} para ${dept.name}.`, 'success');
  } catch (e) {
    // Uma falha no meio do lote deixa as anteriores já gravadas (cada saída é
    // sua própria transação no backend). Dizer QUANTAS entraram evita que o
    // usuário repita o lote inteiro e duplique as baixas.
    await reload();
    toast(`Erro após registrar ${feitas.length} de ${linhas.length}: ${errorText(e)}. ` +
      'Confira a Linha do Tempo antes de repetir.', 'error');
  } finally {
    botao.disabled = false;
  }
}

function openSaidaModal(productId) {
  fillProductSelect('saiProduto', productId);
  fillDepartmentSelect('saiDepartamento');
  document.getElementById('saiQtd').value = '';
  document.getElementById('saiObs').value = '';
  document.getElementById('saiSolicitante').value = '';
  document.getElementById('saiData').value = nowLocalInputValue();
  updateSaidaInfo();
  openModal('modalSaida');
}
function updateSaidaInfo() {
  const p = products.find((x) => x.id === document.getElementById('saiProduto').value);
  const dept = departments.find((x) => x.id === document.getElementById('saiDepartamento').value);
  document.getElementById('saiEncarregadoHint').textContent = dept ? 'Encarregado: ' + dept.encarregado : '';
  const box = document.getElementById('saiInfoBox');
  if (!p) { box.textContent = 'Cadastre um produto primeiro.'; return; }
  const qtd = parseFloat(document.getElementById('saiQtd').value) || 0;
  box.innerHTML = `Estoque disponível: <b>${fmtNum(p.qty)} ${p.unit}</b><br>Após esta baixa: <b>${fmtNum(p.qty - qtd)} ${p.unit}</b> — valor: <b>${fmtBRL(qtd * p.avgCost)}</b>`;
}
async function saveSaida() {
  const p = products.find((x) => x.id === document.getElementById('saiProduto').value);
  const qtd = parseFloat(document.getElementById('saiQtd').value);
  const dept = departments.find((x) => x.id === document.getElementById('saiDepartamento').value);
  const dataVal = document.getElementById('saiData').value;
  const obs = document.getElementById('saiObs').value.trim();
  const solicitante = document.getElementById('saiSolicitante').value.trim();
  if (!p) { toast('Selecione um produto.', 'error'); return; }
  if (!qtd || qtd <= 0) { toast('Informe uma quantidade válida.', 'error'); return; }
  if (!dept) { toast('Selecione um departamento.', 'error'); return; }
  if (qtd > p.qty) { toast(`Quantidade maior que o disponível (${fmtNum(p.qty)} ${p.unit}).`, 'error'); return; }
  try {
    const dataISO = dataVal ? new Date(dataVal).toISOString() : nowIso();
    await api.applySaida({ movementId: uid('m_'), productId: p.id, qty: qtd, departmentId: dept.id, date: dataISO, obs, requester: solicitante, createdAt: nowIso() });
    await reload();
    closeModal('modalSaida');
    toast('Baixa registrada.', 'success');
  } catch (e) { toast('Erro: ' + e, 'error'); }
}

function openCorrecaoModal(productId) {
  fillProductSelect('corProduto', productId);
  document.getElementById('corMotivo').value = '';
  document.getElementById('corData').value = nowLocalInputValue();
  semearQtdContada();
  openModal('modalCorrecao');
}
function updateCorrecaoInfo() {
  const p = products.find((x) => x.id === document.getElementById('corProduto').value);
  const box = document.getElementById('corInfoBox');
  if (!p) { box.textContent = 'Cadastre um produto primeiro.'; return; }
  const qtdReal = parseFloat(document.getElementById('corQtdReal').value);
  const delta = (isNaN(qtdReal) ? 0 : qtdReal) - p.qty;
  box.innerHTML = `Estoque no sistema: <b>${fmtNum(p.qty)} ${p.unit}</b><br>Ajuste: <b>${delta > 0 ? '+' : ''}${fmtNum(delta)} ${p.unit}</b> (custo médio permanece ${fmtBRL(p.avgCost)})`;
}
async function saveCorrecao() {
  const p = products.find((x) => x.id === document.getElementById('corProduto').value);
  const qtdReal = parseFloat(document.getElementById('corQtdReal').value);
  const motivo = document.getElementById('corMotivo').value.trim();
  const dataVal = document.getElementById('corData').value;
  if (!p) { toast('Selecione um produto.', 'error'); return; }
  if (isNaN(qtdReal) || qtdReal < 0) { toast('Informe a quantidade contada.', 'error'); return; }
  if (!motivo) { toast('Informe o motivo.', 'error'); return; }
  try {
    const dataISO = dataVal ? new Date(dataVal).toISOString() : nowIso();
    await api.applyCorrecao({ movementId: uid('m_'), productId: p.id, qtyReal: qtdReal, motivo, date: dataISO, createdAt: nowIso() });
    await reload();
    closeModal('modalCorrecao');
    toast('Estoque corrigido.', 'success');
  } catch (e) { toast('Erro: ' + e, 'error'); }
}

export { products as productsCache };
