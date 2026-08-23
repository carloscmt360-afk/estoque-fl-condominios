// Mock de desenvolvimento — SÓ para abrir o frontend num navegador comum e
// revisar o layout sem precisar compilar Rust/C++/Tauri. Alimentado por
// fixtures reais (mesmos bytes que o backend C++ geraria), capturadas via
// core-cli. Não é carregado pelo app de produção (o Tauri real nunca define
// window.__TAURI_DEV_MOCK__).
import { useMock } from './api.js';

async function loadJson(path) {
  const res = await fetch(path);
  return res.json();
}

async function loadJsonOptional(path) {
  try {
    const res = await fetch(path);
    return res.ok ? await res.json() : null;
  } catch (e) {
    return null;
  }
}

export async function installDevMock() {
  const [backup, report, products, departments] = await Promise.all([
    loadJson('fixtures/backup.json'),
    loadJson('fixtures/report_2026_06.json'),
    loadJson('fixtures/products.json'),
    loadJson('fixtures/departments.json'),
  ]);
  // Opcional: a fixture do retrospecto só existe se tiver sido gerada de um
  // banco com o histórico da planilha importado (ver core-cli --retrospect).
  const retrospect = await loadJsonOptional('fixtures/retrospect_2026.json');

  // Catálogo de funções: espelha kFeatureCatalog de core-cpp/src/auth_engine.cpp
  // (rótulo vazio = a ação não existe naquela função).
  const FEATURES = [
    { key: 'relatorio_mensal', label: 'Relatório Mensal',
      create: 'Exportar CSV e imprimir', read: 'Abrir a tela e ver os valores', update: '', delete: '' },
    { key: 'retrospecto', label: 'Retrospecto',
      create: 'Exportar CSV e imprimir', read: 'Abrir a tela e ver os valores',
      update: 'Salvar premissas do teto e aplicar como limite dos setores', delete: '' },
    { key: 'produtos', label: 'Produtos',
      create: 'Cadastrar produto e registrar entrada', read: 'Ver o catálogo e a posição de estoque',
      update: 'Editar produto e corrigir estoque', delete: 'Excluir produto' },
    { key: 'linha_do_tempo', label: 'Linha do Tempo',
      create: 'Registrar lançamentos', read: 'Ver as movimentações',
      update: 'Editar um lançamento', delete: 'Excluir um lançamento' },
    { key: 'departamentos', label: 'Departamentos',
      create: 'Criar departamento', read: 'Ver a lista', update: 'Editar departamento',
      delete: 'Excluir departamento' },
    { key: 'importar_exportar', label: 'Importar e exportar',
      create: 'Exportar backup', read: 'Abrir a tela',
      update: 'Importar backup (substitui todos os dados)', delete: '' },
    { key: 'requisicoes', label: 'Requisições',
      create: 'Solicitar materiais', read: 'Ver o histórico de pedidos do próprio departamento',
      update: 'Aprovar, rejeitar e confirmar entrega (o superadmin já pode, por padrão ninguém mais)',
      delete: 'Cancelar um pedido em aberto' },
    { key: 'empresas', label: 'Fornecedores e Prestadores',
      create: 'Cadastrar empresa e criar especialidade na Setorização',
      read: 'Abrir o Catálogo, a Setorização e o Cadastro',
      update: 'Editar empresa e especialidade', delete: 'Excluir empresa e especialidade' },
    { key: 'condominios', label: 'Condomínios',
      create: 'Cadastrar condomínio', read: 'Ver a lista de condomínios',
      update: 'Editar condomínio', delete: 'Excluir condomínio' },
    { key: 'gestao_datas', label: 'Gestão de Prazos',
      create: 'Cadastrar tipo de serviço e vincular um serviço a um condomínio',
      read: 'Ver vínculos, vencimentos e o histórico de renovações',
      update: 'Editar tipo de serviço, editar vínculo e registrar renovação',
      delete: 'Excluir tipo de serviço e vínculo' },
    { key: 'gerentes', label: 'Gerentes e Carteiras',
      create: 'Cadastrar gerente',
      read: 'Ver a lista de gerentes e a carteira (condomínios) de cada um',
      update: 'Editar gerente e alterar os condomínios da carteira',
      delete: 'Excluir gerente' },
    { key: 'gestao_sos_servicos', label: 'Gestão SOS: Serviços e Comissões',
      create: 'Lançar um novo serviço/venda',
      read: 'Ver a planilha de serviços, o histórico de fechamentos, o Painel e as configurações',
      update: 'Editar serviço em aberto, fechar o mês, reabrir um fechamento e alterar as configurações',
      delete: 'Excluir serviço em aberto' },
    { key: 'suprimentos', label: 'Gestão SOS: Suprimentos',
      create: 'Cadastrar suprimento', read: 'Ver a lista de suprimentos',
      update: 'Editar suprimento', delete: 'Excluir suprimento' },
    { key: 'delta_sindicos', label: 'Gestão SOS: Delta Síndicos',
      create: '', read: 'Ver a planilha de Delta Síndicos, puxada automaticamente de Serviços (marcar/desmarcar condomínio atendido pela Delta é feito em Condomínios)',
      update: '', delete: '' },
    { key: 'aquisicoes', label: 'Compras: Aquisições FL',
      create: 'Lançar uma aquisição', read: 'Ver a lista de aquisições',
      update: 'Editar aquisição', delete: 'Excluir aquisição (sem pagamentos lançados)' },
    { key: 'orcamentos', label: 'Compras: Orçamentos',
      create: 'Lançar um orçamento', read: 'Ver a lista de orçamentos',
      update: 'Editar orçamento e mudar o status (aprovado/recusado)', delete: 'Excluir orçamento' },
    { key: 'pagamentos', label: 'Compras: Acompanhamento de pagamentos',
      create: 'Lançar uma NF com as parcelas', read: 'Ver os pagamentos e o status de cada parcela',
      update: 'Editar pagamento e marcar parcela como paga', delete: 'Excluir pagamento' },
  ];

  // Estado mutável em memória — CRUD do mock opera sobre isso, então criar/
  // editar/excluir na sessão de revisão visual funciona de verdade (só não
  // persiste entre reloads da página).
  //
  // Usuários/permissões/requisições existem aqui só para o modo de revisão
  // visual continuar funcionando (senão a tela de login travaria o navegador
  // fora do app). A SENHA é comparada em texto puro: é um mock de layout, não
  // um segundo backend — o de verdade guarda só PBKDF2 (ver crypto.hpp).
  let state = {
    products,
    departments,
    movements: backup.movements,
    users: [{
      id: 'usr_superadmin', name: 'Carlos Matos', email: 'carlos.matos@flcondominios.com.br',
      role: 'superadmin', departmentId: '', active: true, createdAt: '2026-01-01T00:00:00.000Z',
      lastLoginAt: '', senha: 'Mudar@2025',
    }],
    permissionGroups: [],
    requests: [],
    // Começa VAZIO de propósito: é o estado de fábrica do app de verdade
    // (sem janela cadastrada, o sistema não aceita pedido), e é justamente o
    // que precisa ser possível revisar na tela.
    requestWindows: [],
    condominios: [],
    tiposServico: [],
    servicosCondominio: [],
    renovacoes: [],
    especialidades: [],
    empresas: [],
    gerentes: [],
    gerentesProximoNumero: 1,
    suprimentos: [],
    suprimentosProximoNumero: 1,
    sosServicos: [],
    sosFechamentos: [],
    sosDashboards: [],
    sosPagamentos: [],
    appLogo: null,
    // Padrões de fábrica iguais aos de sos_config_padrao (commissions_engine.hpp)
    // — o mock reflete o que o C++ devolveria numa base nova, sem nada salvo.
    sosConfig: {
      porcentagemPadrao: '0', rateioFl: '55', rateioGerentes: '30', rateioSuprimentos: '15',
      suprimentosEncarregado: '78', suprimentosAssistente: '22', metaPorCondominio: '120',
      deltaSindica: '15', deltaGerente: '15', deltaChavePix: '', deltaTitular: '',
    },
    sosProximoNumero: 1,
    comprasAquisicoes: [],
    comprasPagamentos: [],
    // Orçamentos: fluxo de cotação — uma Ordem (presa a um condomínio) tem N
    // Propostas (uma por empresa solicitada). Ver ordemOrcamentoToJsonMock.
    ordensOrcamento: [],
    ordensOrcamentoSeq: 0,
    emailConfig: { host: '', port: '587', username: '', password: '', fromEmail: '', fromName: 'FL Condomínios', useTls: true },
    emailsEnviados: [],  // só pra inspeção manual em teste — sem rede de verdade no navegador
  };
  let sessionUserId = null;

  function uid(p) { return (p || 'id_') + Math.random().toString(36).slice(2, 10); }

  const RESERVADO = ['pendente', 'aprovado'];

  function usuarioAtual() {
    return state.users.find((u) => u.id === sessionUserId) || null;
  }
  function ehSuperadmin() {
    const u = usuarioAtual();
    return !!(u && u.role === 'superadmin');
  }
  function permissoesDe(u) {
    const vazio = {};
    FEATURES.forEach((f) => { vazio[f.key] = { create: false, read: false, update: false, delete: false }; });
    if (!u) return vazio;
    if (u.role === 'superadmin') {
      FEATURES.forEach((f) => { vazio[f.key] = { create: true, read: true, update: true, delete: true }; });
      return vazio;
    }
    const dep = state.departments.find((d) => d.id === u.departmentId);
    const grupo = dep && state.permissionGroups.find((g) => g.id === dep.permissionGroupId);
    if (!grupo) return vazio;
    FEATURES.forEach((f) => { vazio[f.key] = { ...vazio[f.key], ...(grupo.perms[f.key] || {}) }; });
    return vazio;
  }
  function pode(feature, acao) {
    const u = usuarioAtual();
    return !!(u && permissoesDe(u)[feature][acao]);
  }
  function exigirSessao() {
    if (!usuarioAtual()) throw new Error('[auth] sessão não iniciada — faça login para continuar');
  }
  function exigir(feature, acao) {
    exigirSessao();
    if (!pode(feature, acao)) {
      const f = FEATURES.find((x) => x.key === feature);
      throw new Error(`[forbidden] seu perfil não tem permissão para ${acao} em ${f ? f.label : feature}`);
    }
  }
  function exigirSuperadmin(oQue) {
    exigirSessao();
    if (!ehSuperadmin()) throw new Error('[forbidden] só o superadministrador pode ' + oQue);
  }
  function podeValidar() {
    return ehSuperadmin() || pode('requisicoes', 'update');
  }
  function sessaoJson() {
    const u = usuarioAtual();
    if (!u) return null;
    const dep = state.departments.find((d) => d.id === u.departmentId);
    return {
      user: { ...u, senha: undefined, departmentName: dep ? dep.name : '' },
      service: false,
      permissions: permissoesDe(u),
      features: FEATURES,
    };
  }
  function reservadoDe(productId) {
    return state.requests
      .filter((r) => RESERVADO.includes(r.status))
      .flatMap((r) => r.items)
      .filter((i) => i.productId === productId)
      .reduce((s, i) => s + i.qty, 0);
  }
  function acharRequisicao(id) {
    const r = state.requests.find((x) => x.id === id);
    if (!r) throw new Error('requisição não encontrada: ' + id);
    return r;
  }

  // Janela de requisições — espelha request_engine.cpp: "aberta" nunca é um
  // campo gravado, é sempre a comparação contra o instante da pergunta. Todas
  // as datas são ISO UTC de largura fixa, então comparar as strings já dá a
  // ordem cronológica.
  function fimEfetivo(j) {
    return j.closedAt || j.closesAt;
  }
  function janelaAbertaEm(agora) {
    return state.requestWindows.find((j) => j.opensAt <= agora && agora < fimEfetivo(j));
  }
  function proximaJanelaApos(agora) {
    return [...state.requestWindows]
      .filter((j) => j.opensAt > agora && !j.closedAt)
      .sort((a, b) => cmp(a.opensAt, b.opensAt))[0];
  }
  // Mesma regra do windowSituation()/hasPendingInWindow() em api.cpp: uma
  // janela já terminada com pedido pendente daquele período fica "validacao"
  // em vez de "encerrada"/"concluida" — o prazo passou, mas falta decidir.
  function temPendenteNaJanela(j) {
    const fim = j.closedAt || j.closesAt;
    return state.requests.some((r) => r.status === 'pendente' && r.createdAt >= j.opensAt && r.createdAt < fim);
  }
  function situacaoJanela(j, agora) {
    if (j.closedAt) {
      if (j.closedAt <= j.opensAt) return 'cancelada';
      return temPendenteNaJanela(j) ? 'validacao' : 'encerrada';
    }
    if (agora < j.opensAt) return 'agendada';
    if (agora < j.closesAt) return 'aberta';
    return temPendenteNaJanela(j) ? 'validacao' : 'concluida';
  }
  function exigirJanelaAberta() {
    const agora = new Date().toISOString();
    if (janelaAbertaEm(agora)) return;
    const proxima = proximaJanelaApos(agora);
    throw new Error(proxima
      ? `[janela] as requisições estão fechadas no momento — a próxima janela de pedidos abre em ${proxima.opensAt}`
      : '[janela] as requisições estão fechadas no momento — nenhuma janela de pedidos está aberta. ' +
        'Peça ao administrador para abrir um novo período de requisições.');
  }
  function decidir(r, status, nota) {
    const u = usuarioAtual();
    r.status = status;
    r.decidedAt = new Date().toISOString();
    r.decidedByName = u.name;
    r.decisionNote = nota || '';
    return r;
  }

  const cmp = (a, b) => (a < b ? -1 : a > b ? 1 : 0);
  function ordemCronologica(a, b) {
    return cmp(a.date, b.date) || cmp(a.createdAt || '', b.createdAt || '');
  }

  // Espelha nextSku/backfillMissingSkus de inventory_engine.cpp — só pra
  // pré-visualização não ficar sem SKU nem repetir número ao criar produto.
  let skuSeq = state.products.reduce((max, p) => {
    const m = /^CMT(\d{6})$/.exec(p.sku || '');
    return m ? Math.max(max, parseInt(m[1], 10)) : max;
  }, 0);
  function nextSkuMock() {
    skuSeq += 1;
    return 'CMT' + String(skuSeq).padStart(6, '0');
  }
  [...state.products]
    .filter((p) => !p.sku)
    .sort((a, b) => cmp(a.createdAt || '', b.createdAt || ''))
    .forEach((p) => { p.sku = nextSkuMock(); });

  // Espelha classifyCategory/kValidCategories de inventory_engine.cpp — só
  // pra pré-visualização mostrar categoria de verdade (uma das seis, ou
  // "Não Classificado") em vez do texto livre antigo da fixture ("Jardinagem"
  // etc.), que é exatamente o que a migração real faria com dado legado.
  const CATEGORIAS_VALIDAS = ['Papelaria', 'Informática', 'Assembleia', 'Gráfica', 'Brinde', 'Valor'];
  const NAO_CLASSIFICADO = 'Não Classificado';
  const fold = (s) => String(s || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
  const REGRAS = [
    ['Papelaria', ['papel', 'caneta', 'lapis', 'marca-texto', 'marca texto', 'pasta', 'grampeador',
      'clipe', 'post-it', 'caderno', 'envelope', 'fita adesiva', 'borracha', 'corretivo', 'regua', 'tesoura']],
    ['Informática', ['mouse', 'teclado', 'cabo', 'adaptador', 'pendrive', 'pen drive', 'carregador',
      'fonte', 'notebook', 'monitor', 'impressora', 'hd externo', 'ssd', 'webcam', 'roteador', 'no-break']],
    ['Assembleia', ['urna', 'cracha', 'votacao', 'identificador', 'assembleia', 'cedula']],
    ['Gráfica', ['cartao de visita', 'folder', 'adesivo', 'banner', 'convite', 'divisoria', 'impresso', 'grafica']],
    ['Brinde', ['caneca', 'chaveiro', 'agenda', 'sacola', 'garrafa', 'brinde', 'squeeze', 'ecobag']],
  ];
  function classifyCategoryMock(oldCategory, name) {
    for (const v of CATEGORIAS_VALIDAS) if (fold(oldCategory) === fold(v)) return v;
    const foldedName = fold(name);
    for (const [categoria, palavras] of REGRAS) {
      if (palavras.some((p) => foldedName.includes(fold(p)))) return categoria;
    }
    return NAO_CLASSIFICADO;
  }
  state.products
    .filter((p) => !CATEGORIAS_VALIDAS.includes(p.category))
    .forEach((p) => { p.category = classifyCategoryMock(p.category, p.name); });

  function passo(s, m) {
    if (m.type === 'entrada') {
      const base = Math.max(s.qty, 0);
      s.avgCost = base + m.qty > 0 ? (base * s.avgCost + m.qty * (m.unitPrice || 0)) / (base + m.qty) : 0;
      s.qty += m.qty;
    } else if (m.type === 'saida') s.qty -= m.qty;
    else {
      s.qty += m.qty;
      if (m.newAvgCost > 0) s.avgCost = m.newAvgCost;
    }
  }
  /* Espelha inventory_engine.cpp::recomputeProduct, inclusive a preservação
     do offset entre o razão e o saldo gravado — sem isso o mock mostraria um
     comportamento de edição diferente do app real. Também espelha a trava de
     saldo negativo: uma operação cujo saldo final ficaria negativo lança erro
     em vez de gravar (a chamada em memória não é desfeita sozinha, mas o mock
     só existe para revisão visual — o app real usa uma transação SQLite). */
  function recompute(productId, offsetQty) {
    const p = state.products.find((x) => x.id === productId);
    const doProduto = state.movements.filter((m) => m.productId === productId).sort(ordemCronologica);
    const s = { qty: 0, avgCost: 0 };
    for (const m of doProduto) {
      passo(s, m);
      if (m.type === 'ajuste') m.unitPrice = s.avgCost;
      m.resultingQty = s.qty + offsetQty;
      m.resultingAvgCost = s.avgCost;
    }
    if (s.qty + offsetQty < -0.000001) {
      throw new Error('operação deixaria o saldo do produto negativo');
    }
    if (p) { p.qty = s.qty + offsetQty; p.avgCost = s.avgCost; }
  }
  function offsetDe(productId) {
    const p = state.products.find((x) => x.id === productId);
    const s = { qty: 0, avgCost: 0 };
    state.movements.filter((m) => m.productId === productId).sort(ordemCronologica).forEach((m) => passo(s, m));
    return (p ? p.qty : 0) - s.qty;
  }
  function posicaoAntes(productId, date, createdAt, excluirId, offsetQty) {
    const s = { qty: 0, avgCost: 0 };
    state.movements
      .filter((m) => m.productId === productId && m.id !== excluirId &&
        (m.date < date || (m.date === date && (m.createdAt || '') < createdAt)))
      .sort(ordemCronologica)
      .forEach((m) => passo(s, m));
    return { qty: s.qty + offsetQty, avgCost: s.avgCost };
  }

  // ---- fornecedores e prestadores: espelha companies_engine.cpp ----
  // Os quatro setores são catálogo fixo lá; aqui a lista é a MESMA, na mesma
  // ordem, senão a revisão visual mostraria abas que o app não tem.
  const SETORES = [
    { key: 'vendas', label: 'Vendas' },
    { key: 'contratos_manutencoes', label: 'Contratos / Manutenções' },
    { key: 'terceirizadas', label: 'Terceirizadas' },
    { key: 'engenharia', label: 'Engenharia' },
  ];
  function setorLabelMock(key) {
    const s = SETORES.find((x) => x.key === key);
    return s ? s.label : key;
  }
  // Espelha folded(): minúsculas, sem acento, espaços colapsados — é o que
  // impede "Impermeabilizacao" e "Impermeabilização" virarem dois nichos.
  function dobra(s) {
    return String(s || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '')
      .toLowerCase().trim().replace(/\s+/g, ' ');
  }
  function canonCnpjMock(cnpj) {
    return String(cnpj || '').toUpperCase().replace(/[^0-9A-Z]/g, '');
  }
  function exigirEspecialidade(esp, exceptId) {
    if (!SETORES.some((s) => s.key === esp.setor)) throw new Error(`setor inválido: '${esp.setor}'`);
    if (!String(esp.nome || '').trim()) throw new Error('informe o nome da especialidade');
    const alvo = dobra(esp.nome);
    const choque = state.especialidades.find(
      (x) => x.setor === esp.setor && x.id !== exceptId && dobra(x.nome) === alvo);
    if (choque) throw new Error(`já existe "${choque.nome}" em ${setorLabelMock(esp.setor)}`);
  }
  function exigirEmpresa(emp, exceptId) {
    if (!String(emp.nome || '').trim()) throw new Error('informe o nome da empresa');
    const key = canonCnpjMock(emp.cnpj);
    if (!key) return;
    const choque = state.empresas.find((x) => x.id !== exceptId && canonCnpjMock(x.cnpj) === key);
    if (choque) throw new Error(`o CNPJ informado já está cadastrado em "${choque.nome}"`);
  }
  function empresaToJsonMock(e) {
    const especialidades = (e.especialidadeIds || [])
      .map((id) => state.especialidades.find((x) => x.id === id))
      .filter(Boolean)
      .map((x) => ({ ...x, setorLabel: setorLabelMock(x.setor) }));
    return { ...e, especialidades };
  }
  function normalizaEmpresaEntrada(p, exceptId) {
    exigirEmpresa(p, exceptId);
    const ids = [];
    for (const id of p.especialidadeIds || []) {
      if (!id || ids.includes(id)) continue;
      if (!state.especialidades.some((x) => x.id === id)) {
        throw new Error('especialidade não encontrada: ' + id);
      }
      ids.push(id);
    }
    return { ...p, nome: String(p.nome).trim(),
      estado: String(p.estado || '').trim().toUpperCase(),
      parceira: !!p.parceira, especialidadeIds: ids };
  }
  function exigirGerente(g) {
    if (!String(g.nome || '').trim()) throw new Error('informe o nome do gerente');
  }
  function gerenteToJsonMock(g) {
    const condominios = (g.condominioIds || [])
      .map((id) => state.condominios.find((x) => x.id === id))
      .filter(Boolean)
      .map((c) => ({ id: c.id, nome: c.nome }));
    return { ...g, condominios };
  }
  function normalizaGerenteEntrada(g) {
    exigirGerente(g);
    const ids = [];
    for (const id of g.condominioIds || []) {
      if (!id || ids.includes(id)) continue;
      if (!state.condominios.some((x) => x.id === id)) {
        throw new Error('condomínio não encontrado: ' + id);
      }
      ids.push(id);
    }
    return { ...g, nome: String(g.nome).trim(), condominioIds: ids };
  }

  // ---- gestão sos: delta síndicos ----
  // Puxado de sosServicos, nunca de uma tabela própria — mesma lógica de
  // listDeltaSindicos em commissions_engine.cpp: um item por serviço PAGO
  // cujo condomínio tem deltaSindica=true, sindico/porcentagem resolvidos na
  // hora (cadastro do condomínio e sos_config), nunca denormalizados.
  function listDeltaSindicosMock() {
    const pctDelta = configPctMock('deltaSindica', 15);
    const sindicoDoCondominioDelta = new Map(
      state.condominios.filter((c) => c.deltaSindica).map((c) => [c.id, c.sindico]));
    if (!sindicoDoCondominioDelta.size) return [];
    return state.sosServicos
      .filter((s) => s.pago && sindicoDoCondominioDelta.has(s.condominioId))
      .map((s) => ({
        id: s.id, numero: s.numero, condominioId: s.condominioId, condominioNome: s.condominioNome,
        gerenteId: s.gerenteId, gerenteNome: s.gerenteNome,
        sindico: sindicoDoCondominioDelta.get(s.condominioId),
        venda: s.venda, porcentagem: pctDelta, comissao: (s.venda || 0) * pctDelta / 100,
        dataReferencia: s.dataReferencia, observacoes: s.observacoes, createdAt: s.createdAt,
      }))
      .sort((a, b) => b.numero - a.numero);
  }

  // ---- gestão sos: dashboard de fechamento ----
  // Reimplementação em JS do que commissions_engine.cpp::montarDashboard faz
  // de verdade — mesmo critério do resto deste mock (ex.: listDeltaSindicosMock
  // acima): sem isso, revisar a tela de Fechamento num navegador comum
  // (sem compilar Tauri) ficaria impossível.
  function configPctMock(chave, padrao) {
    const n = parseFloat(state.sosConfig[chave]);
    return state.sosConfig[chave] === undefined || state.sosConfig[chave] === '' || Number.isNaN(n)
      ? padrao : n;
  }
  function eficaciaDeMock(producao, condominiosNaCarteira, metaPorCondominio) {
    if (condominiosNaCarteira <= 0 || metaPorCondominio <= 0) return 100;
    const producaoPorCondominio = producao / condominiosNaCarteira;
    if (producaoPorCondominio >= metaPorCondominio) return 100;
    return (producaoPorCondominio / metaPorCondominio) * 100;
  }
  function montarDashboardMock(entrada) {
    if (!ehMesReferenciaValido(entrada.mesReferencia)) {
      throw new Error('data de referência inválida (use mês/ano)');
    }
    const rateioFl = configPctMock('rateioFl', 55);
    const rateioGerentesPadrao = configPctMock('rateioGerentes', 30);
    const rateioSuprimentos = configPctMock('rateioSuprimentos', 15);
    const suprimentosEncarregado = configPctMock('suprimentosEncarregado', 78);
    const suprimentosAssistente = configPctMock('suprimentosAssistente', 22);
    const metaPorCondominio = configPctMock('metaPorCondominio', 120);

    const out = {
      mesReferencia: entrada.mesReferencia,
      flLucro: 0,
      percentualComissao: rateioGerentesPadrao,
      percentualDistribuido: entrada.percentualDistribuido || 0,
      retido: entrada.retido || 0,
      distribuicaoCompras: entrada.distribuicaoCompras || [],
      distribuicaoDelta: entrada.distribuicaoDelta || [],
      observacoes: entrada.observacoes || '',
      arrecadado: 0,
      liberadoParaComissao: 0,
      gerenciaLiquido: 0,
      gerentes: [],
      deltaSindicos: [],
      empresas: [],
    };

    // Só conta quem já foi pago — mesmo critério de montarDashboard em
    // commissions_engine.cpp: venda sem pagamento confirmado não gera
    // comissão pra ninguém.
    const doMes = state.sosServicos.filter((s) => s.dataReferencia === entrada.mesReferencia && s.pago);
    // Arrecadado é a COMISSÃO (venda × porcentagem), não a venda bruta —
    // mesmo critério de montarDashboard em commissions_engine.cpp.
    for (const s of doMes) out.arrecadado += (s.venda || 0) * (s.porcentagem || 0) / 100;
    // FL e o "liberado para comissão" (fatia dos Gerentes) são sempre o
    // rateio de Configurações — mesmo critério do C++ real.
    out.flLucro = out.arrecadado * rateioFl / 100;
    out.liberadoParaComissao = out.arrecadado * rateioGerentesPadrao / 100;

    if (!out.distribuicaoCompras.length) {
      const poolSuprimentos = out.arrecadado * rateioSuprimentos / 100;
      out.distribuicaoCompras = [
        { rotulo: 'Encarregado', valor: poolSuprimentos * suprimentosEncarregado / 100 },
        { rotulo: 'Assistente', valor: poolSuprimentos * suprimentosAssistente / 100 },
      ];
    }

    out.deltaSindicos = listDeltaSindicosMock()
      .filter((d) => d.dataReferencia === entrada.mesReferencia);

    for (const g of state.gerentes) {
      // Produzido = venda dos serviços da carteira do gerente, só com
      // porcentagem > 0 — mesmo critério de montarDashboard em
      // commissions_engine.cpp.
      let produzido = 0;
      for (const s of doMes) {
        if ((g.condominioIds || []).includes(s.condominioId) && (s.porcentagem || 0) > 0) produzido += s.venda || 0;
      }
      let descontos = 0;
      for (const d of out.deltaSindicos) {
        if (d.gerenteId === g.id) descontos += d.comissao || 0;
      }

      const override = (entrada.gerentes || []).find((e) => e.gerenteId === g.id);
      // Carteira é digitada à mão todo mês — sem valor informado, sugere o
      // tamanho real da carteira cadastrada como ponto de partida — só
      // contando os condomínios ATIVOS (ex-cliente ligado à carteira por
      // histórico não conta número nem meta — mesmo critério do C++ real).
      const carteiraAtiva = (g.condominioIds || []).filter((id) => {
        const c = state.condominios.find((x) => x.id === id);
        return !c || c.ativo;
      }).length;
      const carteira = (override && override.carteira > 0) ? override.carteira : carteiraAtiva;
      const meta = metaPorCondominio * carteira;
      let porcentagem, eficacia;
      if (override) {
        porcentagem = override.porcentagem > 0 ? override.porcentagem : rateioGerentesPadrao;
        eficacia = override.eficacia;
      } else {
        porcentagem = rateioGerentesPadrao;
        eficacia = eficaciaDeMock(produzido, carteira, metaPorCondominio);
      }

      const recebido = produzido * porcentagem / 100;
      const retido = recebido * (1 - eficacia / 100);
      let comissao = recebido * eficacia / 100 - descontos;
      if (comissao < 0) comissao = 0;

      out.gerenciaLiquido += comissao;
      out.retido += retido;
      out.gerentes.push({
        gerenteId: g.id, gerenteNome: g.nome, produzido, recebido, carteira, meta, porcentagem, eficacia,
        descontos, comissao, retido,
      });
    }
    out.gerentes.sort((a, b) => a.gerenteNome.localeCompare(b.gerenteNome, 'pt-BR'));

    for (const e of state.empresas.filter((x) => x.parceira)) {
      let recebidos = 0;
      for (const s of doMes) if (s.parceiroId === e.id) recebidos += s.venda || 0;
      out.empresas.push({ empresaId: e.id, empresaNome: e.nome, recebidos });
    }

    return out;
  }

  // ---- gestão sos: pagamentos ----
  // Reimplementação em JS de Api::montarPagamentoSos (api.cpp): monta a
  // proposta a partir do Dashboard de Fechamento já salvo do mês — nunca
  // recalcula a comissão do zero.
  function montarPagamentoSosMock(mesReferencia) {
    const existente = state.sosPagamentos.find((p) => p.mesReferencia === mesReferencia);
    if (existente) return existente;

    const salvos = state.sosDashboards.filter((d) => d.mesReferencia === mesReferencia)
      .sort((a, b) => cmp(b.geradoEm, a.geradoEm));
    if (!salvos.length) {
      throw new Error('Nenhum Dashboard de Fechamento salvo para este mês — feche o mês em ' +
        'Dashboard de Fechamento antes de programar o pagamento.');
    }
    const dash = salvos[0].dados;

    const linhas = [];
    for (const g of dash.gerentes || []) {
      const gerente = state.gerentes.find((x) => x.id === g.gerenteId);
      linhas.push({
        tipo: 'gerente', pessoaId: g.gerenteId, nome: g.gerenteNome,
        chavePix: gerente ? gerente.chavePix : '', valor: g.comissao || 0, autorizado: true,
      });
    }
    const totalDelta = (dash.gerentes || []).reduce((s, g) => s + (g.descontos || 0), 0);

    // "Encarregado" mapeia pra categoria Gestor, "Assistente" pra categoria
    // Assistente (nomes históricos do rateio ≠ nome da categoria cadastrada
    // — ver comentário em Api::montarPagamentoSos). Categoria sem ninguém
    // cadastrado não vira linha; com mais de uma pessoa, divide em partes
    // iguais.
    const ROTULO_PARA_CATEGORIA = { Encarregado: 'gestor', Assistente: 'assistente' };
    for (const l of dash.distribuicaoCompras || []) {
      const categoria = ROTULO_PARA_CATEGORIA[l.rotulo];
      if (!categoria) continue;
      const pessoas = state.suprimentos.filter((s) => s.categoria === categoria);
      if (!pessoas.length) continue;
      const cada = (l.valor || 0) / pessoas.length;
      for (const s of pessoas) {
        linhas.push({ tipo: 'suprimento', pessoaId: s.id, nome: s.nome, chavePix: s.chavePix,
          valor: cada, autorizado: true });
      }
    }

    linhas.push({
      tipo: 'delta', pessoaId: '', nome: state.sosConfig.deltaTitular || 'Delta',
      chavePix: state.sosConfig.deltaChavePix || '', valor: totalDelta, autorizado: true,
    });

    return {
      id: '', mesReferencia, fechado: false, observacoes: '', geradoEm: '', fechadoEm: '', createdAt: '',
      dados: {
        arrecadado: dash.arrecadado || 0, totalGerentes: dash.gerenciaLiquido || 0,
        totalSuprimentos: (dash.distribuicaoCompras || []).reduce((s, l) => s + (l.valor || 0), 0),
        totalDelta, linhas,
      },
    };
  }

  // ---- gestão sos: suprimentos ----
  const CATEGORIAS_SUPRIMENTO = ['gestor', 'assistente', 'auxiliar', 'vistoriador_predial'];
  function exigirSuprimento(s) {
    if (!String(s.nome || '').trim()) throw new Error('informe o nome');
    if (!CATEGORIAS_SUPRIMENTO.includes(s.categoria)) {
      throw new Error('categoria inválida — selecione Gestor, Assistente, Auxiliar ou Vistoriador predial');
    }
  }
  function normalizaSuprimentoEntrada(s) {
    exigirSuprimento(s);
    return { ...s, nome: String(s.nome).trim() };
  }

  // ---- compras: aquisições, orçamentos e pagamentos ----
  function normalizaAquisicaoEntrada(a) {
    const fornecedor = state.empresas.find((x) => x.id === a.fornecedorId);
    if (!fornecedor) throw new Error('fornecedor não encontrado: ' + a.fornecedorId);
    if (!String(a.descricao || '').trim()) throw new Error('informe a descrição da compra');
    const valor = parseFloat(a.valor) || 0;
    if (valor < 0) throw new Error('o valor não pode ser negativo');
    if (!a.dataCompra) throw new Error('data da compra inválida');
    return {
      id: a.id, fornecedorId: fornecedor.id, fornecedorNome: fornecedor.nome,
      descricao: String(a.descricao).trim(), notaFiscal: String(a.notaFiscal || '').trim(),
      valor, dataCompra: a.dataCompra, observacoes: String(a.observacoes || '').trim(),
    };
  }
  // Janela sem resposta antes do declínio automático (mesmo valor de
  // kDiasDeclinioAutomatico em purchases_engine.hpp).
  const DIAS_DECLINIO_AUTOMATICO = 25;

  // Espelha ordemOrcamentoStatusEfetivo (purchases_engine.cpp): "declinado"
  // NUNCA é gravado, é sempre calculado na leitura a partir de
  // dataSolicitacao/reabertoEm contra hojeIso.
  function ordemOrcamentoStatusEfetivoMock(ordem, hojeIso) {
    const aguardando = ordem.status === 'solicitado' || ordem.status === 'enviado_cliente';
    if (!aguardando || !ordem.dataSolicitacao) return ordem.status;
    const base = ordem.reabertoEm || ordem.dataSolicitacao;
    const dias = Math.floor((new Date(hojeIso) - new Date(base)) / 86400000);
    return dias >= DIAS_DECLINIO_AUTOMATICO ? 'declinado' : ordem.status;
  }

  function propostaOrcamentoToJsonMock(p) {
    return { ...p, temResposta: p.valor >= 0 };
  }

  // Sem resposta por último; entre respondidas, do mais caro pro mais barato
  // — mesmo critério de carregarPropostas (purchases_engine.cpp).
  function ordemOrcamentoToJsonMock(ordem, hojeIso) {
    const condominio = state.condominios.find((c) => c.id === ordem.condominioId);
    const propostasOrdenadas = [...ordem.propostas].sort((a, b) => {
      const aSem = a.valor < 0, bSem = b.valor < 0;
      if (aSem !== bSem) return aSem ? 1 : -1;
      if (!aSem && !bSem) return b.valor - a.valor;
      return new Date(a.createdAt) - new Date(b.createdAt);
    });
    return {
      id: ordem.id, numero: ordem.numero, condominioId: ordem.condominioId,
      condominioNome: ordem.condominioNome, condominioEmail: condominio ? (condominio.email || '') : '',
      descricao: ordem.descricao, observacoes: ordem.observacoes, status: ordem.status,
      statusEfetivo: ordemOrcamentoStatusEfetivoMock(ordem, hojeIso),
      propostaRecomendadaId: ordem.propostaRecomendadaId || '',
      propostaAprovadaId: ordem.propostaAprovadaId || '',
      dataSolicitacao: ordem.dataSolicitacao || '', dataEnvioCliente: ordem.dataEnvioCliente || '',
      dataAprovacao: ordem.dataAprovacao || '', reabertoEm: ordem.reabertoEm || '', createdAt: ordem.createdAt,
      propostas: propostasOrdenadas.map(propostaOrcamentoToJsonMock),
    };
  }

  // Sem rede de verdade no navegador (isso é bridge/src/mailer.rs em
  // produção): só confere se há SMTP configurado e, se sim, registra em
  // state.emailsEnviados pra inspeção manual em teste. Devolve uma
  // mensagem de erro (string) em vez de lançar — mesmo critério de
  // enviar_emails_compostos (src-tauri/src/commands.rs): falha de e-mail
  // nunca desfaz a ação que já foi aplicada.
  function enviarEmailMock(to, subject, bodyHtml) {
    if (!String(state.emailConfig.host || '').trim()) {
      return 'configure o servidor de e-mail em Configurações antes de enviar.';
    }
    state.emailsEnviados.push({ to, subject, bodyHtml, enviadoEm: new Date().toISOString() });
    return null;
  }
  function pagamentoToJsonMock(p) {
    const aquisicao = state.comprasAquisicoes.find((x) => x.id === p.aquisicaoId);
    const totalPago = (p.parcelas || []).filter((x) => x.pago).reduce((s, x) => s + x.valor, 0);
    const quitado = (p.parcelas || []).length > 0 && (p.parcelas || []).every((x) => x.pago);
    return {
      ...p,
      aquisicaoDescricao: aquisicao ? aquisicao.descricao : '',
      fornecedorNome: aquisicao ? aquisicao.fornecedorNome : '',
      totalPago, totalEmAberto: (p.valorTotal || 0) - totalPago, quitado,
    };
  }
  function normalizaParcelas(parcelas) {
    if (!parcelas || !parcelas.length) throw new Error('informe ao menos uma parcela');
    return parcelas.map((parc, i) => {
      const valor = parseFloat(parc.valor) || 0;
      if (valor < 0) throw new Error('o valor da parcela não pode ser negativo');
      if (!parc.vencimento) throw new Error('vencimento de parcela inválido');
      return { id: parc.id || uid('parc_'), numero: parc.numero || i + 1, valor,
        vencimento: parc.vencimento, pago: false, dataPagamento: '' };
    });
  }
  function normalizaPagamentoEntrada(p) {
    const aquisicao = state.comprasAquisicoes.find((x) => x.id === p.aquisicaoId);
    if (!aquisicao) throw new Error('aquisição não encontrada: ' + p.aquisicaoId);
    if (!String(p.notaFiscal || '').trim()) throw new Error('informe o número da nota fiscal');
    const valorTotal = parseFloat(p.valorTotal) || 0;
    if (valorTotal < 0) throw new Error('o valor total não pode ser negativo');
    if (!p.dataEmissao) throw new Error('data de emissão inválida');
    return {
      id: p.id, aquisicaoId: aquisicao.id, notaFiscal: String(p.notaFiscal).trim(), valorTotal,
      dataEmissao: p.dataEmissao, observacoes: String(p.observacoes || '').trim(),
    };
  }

  // ---- gestão sos: serviços e fechamentos ----
  function ehMesReferenciaValido(s) {
    return /^\d{4}-(0[1-9]|1[0-2])$/.test(String(s || ''));
  }
  function servicoToJsonMock(s) {
    const comissao = (s.venda || 0) * (s.porcentagem || 0) / 100;
    return { ...s, comissao, fechado: !!s.fechamentoId };
  }
  function normalizaServicoEntrada(input) {
    const condominio = state.condominios.find((c) => c.id === input.condominioId);
    if (!condominio) throw new Error('selecione o condomínio');
    const venda = parseFloat(input.venda) || 0;
    const porcentagem = parseFloat(input.porcentagem) || 0;
    if (venda < 0) throw new Error('a venda não pode ser negativa');
    if (porcentagem < 0 || porcentagem > 100) throw new Error('a porcentagem precisa estar entre 0 e 100');
    if (!ehMesReferenciaValido(input.dataReferencia)) throw new Error('data de referência inválida (use mês/ano)');
    const pago = !!input.pago;
    // Data só faz sentido junto de pago=true — mesmo critério de
    // createServico/updateServico em commissions_engine.cpp.
    const dataPagamento = pago ? String(input.dataPagamento || '').trim() : '';
    if (pago && !dataPagamento) throw new Error('informe a data de pagamento');

    let gerenteNome = '';
    if (input.gerenteId) {
      const g = state.gerentes.find((x) => x.id === input.gerenteId);
      if (!g) throw new Error('gerente não encontrado: ' + input.gerenteId);
      gerenteNome = g.nome;
    }
    let parceiroNome = '';
    if (input.parceiroId) {
      const p = state.empresas.find((x) => x.id === input.parceiroId);
      if (!p) throw new Error('parceiro não encontrado: ' + input.parceiroId);
      if (!p.parceira) throw new Error(`"${p.nome}" não está marcada como parceira`);
      parceiroNome = p.nome;
    }
    return {
      id: input.id, codigo: String(input.codigo || '').trim(),
      condominioId: condominio.id, condominioNome: condominio.nome,
      gerenteId: input.gerenteId || '', gerenteNome,
      parceiroId: input.parceiroId || '', parceiroNome,
      venda, porcentagem, dataReferencia: input.dataReferencia,
      observacoes: String(input.observacoes || '').trim(),
      pago, dataPagamento,
    };
  }

  // Espelham as recusas de dates_engine.cpp. O mock precisa recusar o que o
  // C++ recusa: se ele aceitasse um condomínio sem nome, a revisão visual
  // mostraria uma linha que o app de verdade nunca chega a gravar.
  function exigirNome(nome, mensagem) {
    if (!String(nome || '').trim()) throw new Error(mensagem);
  }
  const LOCALIZACOES_CONDOMINIO = ['centro', 'leste', 'oeste', 'norte', 'sul', 'outra_cidade'];
  function exigirLocalizacao(localizacao) {
    if (localizacao && !LOCALIZACOES_CONDOMINIO.includes(localizacao)) {
      throw new Error(`localização inválida: '${localizacao}'`);
    }
  }
  function exigirTipoServico(t) {
    exigirNome(t.nome, 'informe o nome do serviço');
    if (!(t.prazoDias > 0)) throw new Error('o prazo do serviço precisa ser maior que zero');
  }

  // Espelha estoque::calcularStatus (dates_engine.cpp): vencimento nunca é
  // gravado, só calculado a partir de dataUltimaRenovacao + prazoDias e do
  // "hoje" que o chamador passa.
  function calcularStatusMock(dataUltimaRenovacao, prazoDias, hojeIso) {
    const vence = new Date(dataUltimaRenovacao).getTime() + prazoDias * 86400000;
    const hoje = new Date(hojeIso).getTime();
    const diasRestantes = Math.ceil((vence - hoje) / 86400000);
    const status = diasRestantes < 0 ? 'vencido' : diasRestantes <= 30 ? 'atencao' : 'ok';
    return { dataVencimento: new Date(vence).toISOString(), diasRestantes, status };
  }
  function vinculoToJson(v, hojeIso) {
    const cond = state.condominios.find((c) => c.id === v.condominioId);
    const tipo = state.tiposServico.find((t) => t.id === v.tipoServicoId);
    const st = tipo
      ? calcularStatusMock(v.dataUltimaRenovacao, tipo.prazoDias, hojeIso)
      : { dataVencimento: '', diasRestantes: 0, status: 'ok' };
    return { ...v, condominioNome: cond ? cond.nome : '', tipoServicoNome: tipo ? tipo.nome : '',
      tipoServicoCor: tipo ? tipo.cor : '', prazoDias: tipo ? tipo.prazoDias : 0, ...st };
  }
  function renovacaoToJson(r) {
    const v = state.servicosCondominio.find((x) => x.id === r.servicoCondominioId);
    const cond = v && state.condominios.find((c) => c.id === v.condominioId);
    const tipo = v && state.tiposServico.find((t) => t.id === v.tipoServicoId);
    return { ...r, condominioNome: cond ? cond.nome : '', tipoServicoNome: tipo ? tipo.nome : '' };
  }

  useMock(async (cmd, args) => {
    switch (cmd) {
      case 'app_status': return null;

      // ---- sessão ----
      case 'login': {
        const { email, password } = args.input;
        const u = state.users.find(
          (x) => x.email.toLowerCase() === String(email).trim().toLowerCase() && x.senha === password && x.active);
        if (!u) throw new Error('[auth] e-mail ou senha inválidos');
        u.lastLoginAt = new Date().toISOString();
        sessionUserId = u.id;
        return JSON.stringify(sessaoJson());
      }
      case 'logout': sessionUserId = null; return null;
      case 'current_session': return JSON.stringify(sessaoJson());
      case 'change_own_password': {
        exigirSessao();
        const u = usuarioAtual();
        if (u.senha !== args.input.currentPassword) throw new Error('[auth] a senha atual não confere');
        if (String(args.input.newPassword).length < 8) throw new Error('a senha precisa ter pelo menos 8 caracteres');
        u.senha = args.input.newPassword;
        return null;
      }

      // ---- usuários ----
      case 'list_users': {
        exigirSuperadmin('gerenciar usuários');
        return JSON.stringify(state.users.map((u) => {
          const dep = state.departments.find((d) => d.id === u.departmentId);
          return { ...u, senha: undefined, departmentName: dep ? dep.name : '' };
        }));
      }
      case 'create_user': {
        exigirSuperadmin('criar usuários');
        const u = args.user;
        if (state.users.some((x) => x.email.toLowerCase() === u.email.toLowerCase())) {
          throw new Error('já existe um usuário com o e-mail ' + u.email);
        }
        state.users.push({ ...u, senha: u.password, password: undefined, lastLoginAt: '' });
        return JSON.stringify(u);
      }
      case 'update_user': {
        exigirSuperadmin('editar usuários');
        const i = state.users.findIndex((x) => x.id === args.user.id);
        if (i < 0) throw new Error('usuário não encontrado');
        const senha = state.users[i].senha;
        state.users[i] = { ...state.users[i], ...args.user, senha, password: undefined };
        return JSON.stringify(state.users[i]);
      }
      case 'delete_user':
        exigirSuperadmin('excluir usuários');
        if (args.id === sessionUserId) throw new Error('você não pode excluir o próprio usuário');
        state.users = state.users.filter((u) => u.id !== args.id);
        return null;
      case 'reset_user_password': {
        exigirSuperadmin('redefinir senhas');
        const u = state.users.find((x) => x.id === args.input.id);
        if (!u) throw new Error('usuário não encontrado');
        u.senha = args.input.newPassword;
        return null;
      }

      // ---- permissões ----
      case 'list_permissions':
        exigirSuperadmin('gerenciar permissões');
        return JSON.stringify({
          groups: state.permissionGroups,
          departments: state.departments.map((d) => ({
            ...d,
            permissionGroupId: d.permissionGroupId || '',
            activeUsers: state.users.filter((u) => u.departmentId === d.id && u.active).length,
          })),
          features: FEATURES,
        });
      case 'create_permission_group': {
        exigirSuperadmin('criar grupos de permissão');
        const g = JSON.parse(args.payload);
        state.permissionGroups.push(g);
        return JSON.stringify(g);
      }
      case 'update_permission_group': {
        exigirSuperadmin('editar grupos de permissão');
        const g = JSON.parse(args.payload);
        const i = state.permissionGroups.findIndex((x) => x.id === g.id);
        if (i < 0) throw new Error('grupo não encontrado');
        state.permissionGroups[i] = { ...state.permissionGroups[i], ...g };
        return JSON.stringify(state.permissionGroups[i]);
      }
      case 'delete_permission_group':
        exigirSuperadmin('excluir grupos de permissão');
        state.permissionGroups = state.permissionGroups.filter((g) => g.id !== args.id);
        state.departments.forEach((d) => { if (d.permissionGroupId === args.id) d.permissionGroupId = ''; });
        return null;
      case 'set_department_permission_group': {
        exigirSuperadmin('vincular grupos de permissão a departamentos');
        const d = state.departments.find((x) => x.id === args.input.departmentId);
        if (!d) throw new Error('departamento não encontrado');
        d.permissionGroupId = args.input.groupId;
        return null;
      }

      // ---- requisições ----
      case 'stock_availability':
        exigirSessao();
        if (!pode('produtos', 'read') && !pode('requisicoes', 'read') && !pode('requisicoes', 'create')) {
          throw new Error('[forbidden] seu perfil não tem permissão para acessar: Produtos, Requisições');
        }
        return JSON.stringify(state.products.map((p) => {
          const reserved = reservadoDe(p.id);
          return { productId: p.id, sku: p.sku || '', name: p.name, unit: p.unit, category: p.category || '',
            qty: p.qty, reserved, available: p.qty - reserved, avgCost: p.avgCost, minStock: p.minStock,
            imagePath: p.imagePath || '', thumbnailPath: p.thumbnailPath || '' };
        }));
      case 'list_requests': {
        exigir('requisicoes', 'read');
        const u = usuarioAtual();
        const lista = podeValidar() ? state.requests : state.requests.filter((r) => r.departmentId === u.departmentId);
        return JSON.stringify([...lista].sort((a, b) => cmp(b.createdAt, a.createdAt)));
      }
      case 'create_request': {
        exigir('requisicoes', 'create');
        // Quem valida requisições pode lançar mesmo com a janela fechada — é o
        // caso de registrar o pedido de quem esqueceu de pedir no prazo.
        if (!podeValidar()) exigirJanelaAberta();
        const u = usuarioAtual();
        const p = JSON.parse(args.payload);
        const depId = ehSuperadmin() && p.departmentId ? p.departmentId : u.departmentId;
        const dep = state.departments.find((d) => d.id === depId);
        if (!dep) throw new Error('seu usuário não está atrelado a nenhum departamento');
        const itens = p.items.map((i) => {
          const prod = state.products.find((x) => x.id === i.productId);
          if (!prod) throw new Error('produto não encontrado');
          if (i.qty > prod.qty - reservadoDe(prod.id) + 1e-9) {
            throw new Error(`não há saldo disponível de "${prod.name}"`);
          }
          return { id: i.id || uid('ri_'), productId: prod.id, productName: prod.name,
            unit: prod.unit, qty: i.qty, movementId: '' };
        });
        const r = { id: p.id, departmentId: dep.id, departmentName: dep.name, requesterUserId: u.id,
          requesterName: u.name, status: 'pendente', obs: p.obs || '', createdAt: p.createdAt,
          decidedAt: '', decidedByName: '', decisionNote: '', deliveredAt: '', deliveredByName: '',
          editedAt: '', editedByName: '', items: itens };
        state.requests.push(r);
        return JSON.stringify(r);
      }
      case 'update_request_items': {
        if (!podeValidar()) throw new Error('[forbidden] seu perfil não pode editar os itens de uma requisição');
        const r = acharRequisicao(args.input.id);
        if (!RESERVADO.includes(r.status)) {
          throw new Error(`não é possível editar os itens de uma requisição com situação '${r.status}'`);
        }
        const p = JSON.parse(args.input.payload);
        if (!p.items || !p.items.length) throw new Error('a requisição precisa ter pelo menos um material');
        // Reservado dos OUTROS pedidos (nunca desta própria requisição, que
        // está prestes a ser regravada) — espelha reservedQtyExcluding do C++.
        const reservadoExcluindo = (productId) => state.requests
          .filter((x) => RESERVADO.includes(x.status) && x.id !== r.id)
          .flatMap((x) => x.items)
          .filter((i) => i.productId === productId)
          .reduce((s, i) => s + i.qty, 0);
        const itens = p.items.map((i) => {
          if (!(i.qty > 0)) throw new Error('informe uma quantidade maior que zero em cada item');
          const prod = state.products.find((x) => x.id === i.productId);
          if (!prod) throw new Error('produto não encontrado');
          const disponivel = prod.qty - reservadoExcluindo(prod.id);
          if (i.qty > disponivel + 1e-9) {
            throw new Error(`não há saldo disponível de "${prod.name}": pedido ${i.qty}, disponível ${Math.max(0, disponivel)}`);
          }
          return { id: i.id || uid('ri_'), productId: prod.id, productName: prod.name,
            unit: prod.unit, qty: i.qty, movementId: '' };
        });
        r.items = itens;
        r.editedAt = args.input.nowIso;
        r.editedByName = usuarioAtual().name;
        return JSON.stringify(r);
      }
      case 'approve_request': {
        if (!podeValidar()) throw new Error('[forbidden] seu perfil não pode aprovar requisições');
        const r = acharRequisicao(args.input.id);
        if (r.status !== 'pendente') throw new Error(`não é possível aprovar uma requisição '${r.status}'`);
        return JSON.stringify(decidir(r, 'aprovado', args.input.note));
      }
      case 'reject_request': {
        if (!podeValidar()) throw new Error('[forbidden] seu perfil não pode rejeitar requisições');
        const r = acharRequisicao(args.input.id);
        if (r.status !== 'pendente') throw new Error(`não é possível rejeitar uma requisição '${r.status}'`);
        return JSON.stringify(decidir(r, 'rejeitado', args.input.note));
      }
      case 'cancel_request': {
        exigirSessao();
        const r = acharRequisicao(args.input.id);
        if (!podeValidar()) {
          exigir('requisicoes', 'delete');
          if (r.departmentId !== usuarioAtual().departmentId) {
            throw new Error('[forbidden] esta requisição é de outro departamento');
          }
        }
        if (!RESERVADO.includes(r.status)) throw new Error(`não é possível cancelar uma requisição '${r.status}'`);
        return JSON.stringify(decidir(r, 'cancelado', args.input.note));
      }
      case 'deliver_request': {
        if (!podeValidar()) throw new Error('[forbidden] seu perfil não pode confirmar a entrega de requisições');
        const r = acharRequisicao(args.input.id);
        if (r.status !== 'aprovado') throw new Error(`não é possível entregar uma requisição '${r.status}'`);
        const agora = args.input.nowIso;
        // Espelha deliverRequest do C++: uma saída por item, aos pares
        // requisição↔movimentação, e só então o pedido vira "entregue".
        r.items.forEach((item, idx) => {
          const prod = state.products.find((x) => x.id === item.productId);
          if (!prod || item.qty > prod.qty + 1e-9) {
            throw new Error(`saldo insuficiente de "${item.productName}" para entregar`);
          }
          const dep = state.departments.find((d) => d.id === r.departmentId);
          const offset = offsetDe(item.productId);
          const movementId = `${args.input.movementIdPrefix}_${idx + 1}`;
          state.movements.push({ id: movementId, type: 'saida', productId: item.productId, qty: item.qty,
            unitPrice: posicaoAntes(item.productId, agora, agora, null, offset).avgCost,
            departmentId: r.departmentId, recipient: dep ? dep.name : r.departmentName,
            encarregado: dep ? dep.encarregado : '', requester: r.requesterName,
            date: agora, obs: 'Requisição ' + r.id, resultingQty: 0, resultingAvgCost: 0, createdAt: agora });
          recompute(item.productId, offset);
          item.movementId = movementId;
        });
        r.status = 'entregue';
        r.deliveredAt = agora;
        r.deliveredByName = usuarioAtual().name;
        return JSON.stringify(r);
      }

      // ---- janela de requisições ----
      case 'request_window_status': {
        exigirSessao();
        const agora = new Date().toISOString();
        return JSON.stringify({
          now: agora,
          canManage: podeValidar(),
          open: !!janelaAbertaEm(agora),
          current: janelaAbertaEm(agora) || null,
          next: proximaJanelaApos(agora) || null,
        });
      }
      case 'list_request_windows': {
        if (!podeValidar()) throw new Error('[forbidden] seu perfil não pode gerenciar as janelas de requisição');
        const agora = new Date().toISOString();
        return JSON.stringify([...state.requestWindows]
          .sort((a, b) => cmp(b.opensAt, a.opensAt))
          .map((j) => ({ ...j, situacao: situacaoJanela(j, agora) })));
      }
      case 'create_request_window': {
        if (!podeValidar()) throw new Error('[forbidden] seu perfil não pode abrir janelas de requisição');
        const agora = new Date().toISOString();
        const j = JSON.parse(args.payload);
        if (!j.opensAt || !j.closesAt) throw new Error('informe a data/hora de abertura e de fechamento da janela');
        if (j.closesAt <= j.opensAt) throw new Error('o fechamento da janela precisa ser depois da abertura');
        if (j.closesAt <= agora) throw new Error('o fechamento da janela precisa estar no futuro');
        const conflito = state.requestWindows.find((w) =>
          j.opensAt < fimEfetivo(w) && w.opensAt < j.closesAt);
        if (conflito) throw new Error('esse período se sobrepõe a uma janela já cadastrada');
        const nova = { id: j.id, opensAt: j.opensAt, closesAt: j.closesAt, obs: j.obs || '',
          createdAt: agora, createdByName: usuarioAtual().name, closedAt: '', closedByName: '' };
        state.requestWindows.push(nova);
        return JSON.stringify(nova);
      }
      case 'close_request_window_now': {
        if (!podeValidar()) throw new Error('[forbidden] seu perfil não pode encerrar janelas de requisição');
        const agora = new Date().toISOString();
        const j = state.requestWindows.find((w) => w.id === args.id);
        if (!j) throw new Error('janela de requisições não encontrada');
        if (j.closedAt) throw new Error('essa janela já foi encerrada antes do prazo');
        if (j.closesAt <= agora) throw new Error('essa janela já terminou no horário programado');
        j.closedAt = agora;
        j.closedByName = usuarioAtual().name;
        return JSON.stringify(j);
      }
      case 'delete_request_window': {
        if (!podeValidar()) throw new Error('[forbidden] seu perfil não pode excluir janelas de requisição');
        const agora = new Date().toISOString();
        const i = state.requestWindows.findIndex((w) => w.id === args.id);
        if (i < 0) throw new Error('janela de requisições não encontrada');
        if (state.requestWindows[i].opensAt <= agora) throw new Error('essa janela já começou');
        state.requestWindows.splice(i, 1);
        return null;
      }

      case 'list_products': return JSON.stringify(state.products);
      case 'list_departments': return JSON.stringify(state.departments);
      case 'create_product': {
        const p = { ...args.product, qty: 0, avgCost: 0, sku: nextSkuMock() };
        state.products.push(p);
        return JSON.stringify(p);
      }
      case 'update_product': {
        const i = state.products.findIndex((p) => p.id === args.product.id);
        // O UPDATE real é só "SET name, unit, min_stock, category" — sku, qty,
        // avg_cost e created_at não vêm do chamador (a tela manda qty:0 e
        // avgCost:0 justamente porque o backend os ignora). Espalhar
        // args.product inteiro aqui zeraria o saldo, um estado que o app de
        // verdade nunca produz.
        if (i >= 0) {
          const atual = state.products[i];
          state.products[i] = { ...atual, name: args.product.name, unit: args.product.unit,
            minStock: args.product.minStock, category: args.product.category };
        }
        return JSON.stringify(state.products[i]);
      }
      case 'delete_product':
        state.products = state.products.filter((p) => p.id !== args.id);
        state.movements = state.movements.filter((m) => m.productId !== args.id);
        return null;
      case 'upload_product_image': {
        // Sem filesystem no navegador: o mock não decodifica/reamostra/gera
        // WebP de verdade (isso é o módulo Rust em produção) — só guarda a
        // própria data: URL como se fosse o "caminho", pra revisão visual
        // do fluxo (selecionar → prévia → salvar → reabrir → continua lá).
        const i = args.input;
        const p = state.products.find((x) => x.id === i.productId);
        if (p) {
          const dataUrl = `data:image/webp;base64,${i.fileBase64}`;
          p.imagePath = dataUrl;
          p.thumbnailPath = dataUrl;
        }
        return JSON.stringify(p || null);
      }
      case 'delete_product_image': {
        const i = args.input;
        const p = state.products.find((x) => x.id === i.productId);
        if (p) { p.imagePath = ''; p.thumbnailPath = ''; }
        return JSON.stringify(p || null);
      }
      case 'read_product_image':
        // No mock, o "caminho relativo" já É a data: URL guardada acima.
        return args.relativePath || '';

      // ---- logo da FL ----
      // Sem JSON.stringify aqui: diferente do resto do mock (que imita o
      // retorno em STRING JSON da ponte C++), estes três comandos não
      // passam pelo C++ — são puro Tauri/Rust, cujo invoke() já devolve o
      // valor desserializado direto (String, Option<String> ou nada), sem
      // uma segunda camada de JSON pra desembrulhar (ver api.js: uploadAppLogo/
      // deleteAppLogo/getAppLogo não chamam JSON.parse no retorno).
      case 'upload_app_logo':
        exigirSuperadmin('alterar a logo da FL');
        state.appLogo = `data:image/webp;base64,${args.fileBase64}`;
        return state.appLogo;
      case 'delete_app_logo':
        exigirSuperadmin('alterar a logo da FL');
        state.appLogo = null;
        return null;
      case 'get_app_logo':
        return state.appLogo || null;
      case 'create_department': {
        state.departments.push(args.department);
        return JSON.stringify(args.department);
      }
      case 'update_department': {
        const i = state.departments.findIndex((d) => d.id === args.department.id);
        if (i >= 0) state.departments[i] = { ...state.departments[i], ...args.department };
        return JSON.stringify(state.departments[i]);
      }
      case 'delete_department':
        state.departments = state.departments.filter((d) => d.id !== args.id);
        return null;
      case 'apply_entrada': {
        const i = args.input;
        const offset = offsetDe(i.productId);
        const m = { id: uid('m_'), type: 'entrada', productId: i.productId, qty: i.qty, unitPrice: i.unitPrice,
          supplier: i.supplier, nf: i.nf, departmentId: '', recipient: '', encarregado: '', requester: '',
          date: i.date, obs: i.obs, resultingQty: 0, resultingAvgCost: 0, createdAt: i.createdAt };
        state.movements.push(m);
        recompute(i.productId, offset);
        return JSON.stringify(m);
      }
      case 'apply_saida': {
        const i = args.input;
        const dept = state.departments.find((x) => x.id === i.departmentId);
        const offset = offsetDe(i.productId);
        const m = { id: uid('m_'), type: 'saida', productId: i.productId, qty: i.qty,
          unitPrice: posicaoAntes(i.productId, i.date, i.createdAt, null, offset).avgCost,
          departmentId: dept.id, recipient: dept.name, encarregado: dept.encarregado, requester: i.requester,
          date: i.date, obs: i.obs, resultingQty: 0, resultingAvgCost: 0, createdAt: i.createdAt };
        state.movements.push(m);
        recompute(i.productId, offset);
        return JSON.stringify(m);
      }
      case 'apply_correcao': {
        const i = args.input;
        const offset = offsetDe(i.productId);
        const antes = posicaoAntes(i.productId, i.date, i.createdAt, null, offset);
        const m = { id: uid('m_'), type: 'ajuste', productId: i.productId, qty: i.qtyReal - antes.qty,
          unitPrice: antes.avgCost, departmentId: '', recipient: '', encarregado: '', requester: '',
          obs: i.motivo, date: i.date, resultingQty: 0, resultingAvgCost: 0, createdAt: i.createdAt,
          newAvgCost: i.newAvgCost || 0 };
        state.movements.push(m);
        recompute(i.productId, offset);
        return JSON.stringify(m);
      }
      case 'list_movements':
        return JSON.stringify([...state.movements].sort(ordemCronologica));
      case 'update_movement': {
        const patch = args.patch;
        const m = state.movements.find((x) => x.id === patch.id);
        if (!m) throw new Error('lançamento não encontrado: ' + patch.id);
        const offset = offsetDe(m.productId);
        const date = patch.date || m.date;

        if (m.type === 'entrada') {
          if (!(patch.qty > 0)) throw new Error('a quantidade da entrada deve ser maior que zero');
          Object.assign(m, { qty: patch.qty, unitPrice: patch.unitPrice, supplier: patch.supplier, nf: patch.nf });
        } else if (m.type === 'saida') {
          if (!(patch.qty > 0)) throw new Error('a quantidade da saída deve ser maior que zero');
          const dept = state.departments.find((x) => x.id === patch.departmentId);
          if (!dept) throw new Error('departamento não encontrado: ' + patch.departmentId);
          Object.assign(m, { qty: patch.qty, unitPrice: patch.unitPrice, departmentId: dept.id,
            recipient: dept.name, encarregado: dept.encarregado, requester: patch.requester });
        } else {
          m.qty = patch.qtyReal - posicaoAntes(m.productId, date, m.createdAt, m.id, offset).qty;
        }
        m.obs = patch.obs;
        m.date = date;
        recompute(m.productId, offset);
        return JSON.stringify(m);
      }
      case 'delete_movement': {
        const m = state.movements.find((x) => x.id === args.id);
        if (!m) throw new Error('lançamento não encontrado: ' + args.id);
        const offset = offsetDe(m.productId);
        state.movements = state.movements.filter((x) => x.id !== args.id);
        recompute(m.productId, offset);
        return null;
      }
      case 'compute_report': {
        // fixture pré-computada (junho/2026) — não recalcula ao vivo no mock;
        // suficiente para revisar o layout, que é o objetivo deste modo. Só o
        // bloco de limites é recalculado, porque ele depende do limite mensal
        // que o mock deixa editar na tela de Departamentos.
        const gastoPorDepto = {};
        for (const p of report.pedidos || []) {
          const k = String(p.recipient || '').trim().toUpperCase();
          gastoPorDepto[k] = (gastoPorDepto[k] || 0) + p.qty * p.unitPrice;
        }
        const linhas = state.departments.map((d) => {
          const gasto = gastoPorDepto[String(d.name || '').trim().toUpperCase()] || 0;
          const limite = d.monthlyLimit || 0;
          const pct = limite > 0 ? gasto / limite : null;
          return { id: d.id, name: d.name, encarregado: d.encarregado, limite, gasto,
            temLimite: limite > 0, pct, saldo: limite > 0 ? limite - gasto : null,
            status: limite > 0 ? (pct >= 1 ? 'estourado' : pct >= 0.9 ? 'atencao' : 'ok') : 'sem-limite' };
        });
        const comLim = linhas.filter((l) => l.temLimite);
        return JSON.stringify({ ...report, limites: {
          linhas,
          comLimite: comLim.length,
          atencao: linhas.filter((l) => l.status === 'atencao').length,
          estourado: linhas.filter((l) => l.status === 'estourado').length,
          tetoTotal: comLim.reduce((s, l) => s + l.limite, 0),
          gastoTotal: comLim.reduce((s, l) => s + l.gasto, 0),
        } });
      }
      case 'compute_retrospect':
        // fixture pré-computada (2026) — mesma lógica do compute_report acima.
        if (!retrospect) {
          throw new Error('fixture fixtures/retrospect_2026.json ausente — gere com: ' +
            'cargo run -p core-cli -- --db <estoque.db> --retrospect 2026 --dump-fixtures frontend/fixtures');
        }
        return JSON.stringify(retrospect);
      case 'save_budget_params':
      case 'import_dept_cost_history':
        // sem efeito no mock: o retrospecto vem de fixture pré-computada
        return null;
      case 'backup':
        return JSON.stringify(state);
      case 'restore_backup':
        state = JSON.parse(args.payload);
        return null;

      // ---- fornecedores e prestadores de serviços ----
      case 'list_setorizacao':
        exigir('empresas', 'read');
        return JSON.stringify({
          setores: SETORES.map((s) => ({
            key: s.key,
            label: s.label,
            especialidades: state.especialidades
              .filter((e) => e.setor === s.key)
              .sort((a, b) => cmp(a.nome, b.nome))
              .map((e) => ({
                ...e,
                setorLabel: s.label,
                empresas: state.empresas.filter((x) => (x.especialidadeIds || []).includes(e.id)).length,
              })),
          })),
        });
      case 'create_especialidade': {
        exigir('empresas', 'create');
        const e = JSON.parse(args.payload);
        exigirEspecialidade(e, '');
        const nova = { ...e, nome: String(e.nome).trim() };
        state.especialidades.push(nova);
        return JSON.stringify({ ...nova, setorLabel: setorLabelMock(nova.setor) });
      }
      case 'update_especialidade': {
        exigir('empresas', 'update');
        const e = JSON.parse(args.payload);
        const i = state.especialidades.findIndex((x) => x.id === e.id);
        if (i < 0) throw new Error('especialidade não encontrada: ' + e.id);
        exigirEspecialidade(e, e.id);
        state.especialidades[i] = { ...state.especialidades[i], setor: e.setor, nome: String(e.nome).trim() };
        return JSON.stringify({ ...state.especialidades[i], setorLabel: setorLabelMock(e.setor) });
      }
      case 'delete_especialidade': {
        exigir('empresas', 'delete');
        const alvo = state.especialidades.find((x) => x.id === args.id);
        if (!alvo) throw new Error('especialidade não encontrada: ' + args.id);
        const emUso = state.empresas.filter((x) => (x.especialidadeIds || []).includes(args.id)).length;
        if (emUso > 0) {
          throw new Error(`"${alvo.nome}" está marcada em ${emUso} empresa(s) — desmarque-a nelas antes de excluir`);
        }
        state.especialidades = state.especialidades.filter((x) => x.id !== args.id);
        return null;
      }

      case 'list_empresas':
        exigir('empresas', 'read');
        return JSON.stringify([...state.empresas]
          .sort((a, b) => cmp(a.nome, b.nome))
          .map(empresaToJsonMock));
      case 'list_parceiros':
        exigir('empresas', 'read');
        return JSON.stringify(state.empresas
          .filter((e) => e.parceira)
          .sort((a, b) => cmp(a.nome, b.nome))
          .map(empresaToJsonMock));
      case 'create_empresa': {
        exigir('empresas', 'create');
        const nova = normalizaEmpresaEntrada(JSON.parse(args.payload), '');
        state.empresas.push(nova);
        return JSON.stringify(empresaToJsonMock(nova));
      }
      case 'update_empresa': {
        exigir('empresas', 'update');
        const p = JSON.parse(args.payload);
        const i = state.empresas.findIndex((x) => x.id === p.id);
        if (i < 0) throw new Error('empresa não encontrada: ' + p.id);
        const patch = normalizaEmpresaEntrada(p, p.id);
        state.empresas[i] = { ...state.empresas[i], ...patch, createdAt: state.empresas[i].createdAt };
        return JSON.stringify(empresaToJsonMock(state.empresas[i]));
      }
      case 'delete_empresa':
        exigir('empresas', 'delete');
        state.empresas = state.empresas.filter((x) => x.id !== args.id);
        return null;

      // ---- gestão sos: gerentes e carteiras ----
      case 'list_gerentes':
        exigir('gerentes', 'read');
        return JSON.stringify([...state.gerentes]
          .sort((a, b) => cmp(a.nome, b.nome))
          .map(gerenteToJsonMock));
      case 'create_gerente': {
        exigir('gerentes', 'create');
        const novo = { ...normalizaGerenteEntrada(JSON.parse(args.payload)), numero: state.gerentesProximoNumero++ };
        state.gerentes.push(novo);
        return JSON.stringify(gerenteToJsonMock(novo));
      }
      case 'update_gerente': {
        exigir('gerentes', 'update');
        const p = JSON.parse(args.payload);
        const i = state.gerentes.findIndex((x) => x.id === p.id);
        if (i < 0) throw new Error('gerente não encontrado: ' + p.id);
        const patch = normalizaGerenteEntrada(p);
        state.gerentes[i] = { ...state.gerentes[i], ...patch, createdAt: state.gerentes[i].createdAt };
        return JSON.stringify(gerenteToJsonMock(state.gerentes[i]));
      }
      case 'delete_gerente':
        exigir('gerentes', 'delete');
        state.gerentes = state.gerentes.filter((x) => x.id !== args.id);
        return null;

      // ---- gestão sos: delta síndicos ----
      // Puxado de sosServicos + condominios.deltaSindica — não existe mais
      // create/update/delete aqui.
      case 'list_delta_sindicos':
        exigir('delta_sindicos', 'read');
        return JSON.stringify(listDeltaSindicosMock());

      // ---- gestão sos: dashboard de fechamento ----
      case 'montar_dashboard':
        exigir('gestao_sos_servicos', 'read');
        return JSON.stringify(montarDashboardMock(JSON.parse(args.payload)));
      case 'salvar_dashboard': {
        exigir('gestao_sos_servicos', 'update');
        const p = JSON.parse(args.payload);
        if (!ehMesReferenciaValido(p.mesReferencia)) throw new Error('data de referência inválida (use mês/ano)');
        if (!p.dados) throw new Error('dashboard vazio');
        const agora = new Date().toISOString();
        const snap = {
          id: p.id, mesReferencia: p.mesReferencia, dados: p.dados,
          observacoes: p.observacoes || '', geradoEm: p.geradoEm || agora, createdAt: p.createdAt || agora,
        };
        state.sosDashboards.push(snap);
        return JSON.stringify(snap);
      }
      case 'list_dashboards':
        exigir('gestao_sos_servicos', 'read');
        return JSON.stringify([...state.sosDashboards]
          .sort((a, b) => cmp(b.mesReferencia, a.mesReferencia) || cmp(b.geradoEm, a.geradoEm)));

      // ---- gestão sos: pagamentos ----
      case 'montar_pagamento_sos': {
        exigir('gestao_sos_servicos', 'read');
        const { mesReferencia } = JSON.parse(args.payload);
        return JSON.stringify(montarPagamentoSosMock(mesReferencia));
      }
      case 'salvar_pagamento_sos': {
        exigir('gestao_sos_servicos', 'update');
        const p = JSON.parse(args.payload);
        if (!p.id) throw new Error('id do pagamento é obrigatório');
        if (!ehMesReferenciaValido(p.mesReferencia)) throw new Error('data de referência inválida (use mês/ano)');
        if (!p.dados) throw new Error('pagamento vazio');
        const agora = new Date().toISOString();
        const i = state.sosPagamentos.findIndex((x) => x.id === p.id);
        const fechadoEm = p.fechado ? (p.fechadoEm || agora) : '';
        const snap = {
          id: p.id, mesReferencia: p.mesReferencia, dados: p.dados,
          observacoes: p.observacoes || '', fechado: !!p.fechado,
          geradoEm: p.geradoEm || agora, fechadoEm, createdAt: p.createdAt || agora,
        };
        if (i >= 0) state.sosPagamentos[i] = snap; else state.sosPagamentos.push(snap);
        return JSON.stringify(snap);
      }
      case 'list_pagamentos_sos':
        exigir('gestao_sos_servicos', 'read');
        return JSON.stringify([...state.sosPagamentos]
          .sort((a, b) => cmp(b.mesReferencia, a.mesReferencia) || cmp(b.geradoEm, a.geradoEm)));

      // ---- gestão sos: suprimentos ----
      case 'list_suprimentos':
        exigir('suprimentos', 'read');
        return JSON.stringify([...state.suprimentos].sort((a, b) => cmp(a.nome, b.nome)));
      case 'create_suprimento': {
        exigir('suprimentos', 'create');
        const novo = { ...normalizaSuprimentoEntrada(JSON.parse(args.payload)), numero: state.suprimentosProximoNumero++ };
        state.suprimentos.push(novo);
        return JSON.stringify(novo);
      }
      case 'update_suprimento': {
        exigir('suprimentos', 'update');
        const p = JSON.parse(args.payload);
        const i = state.suprimentos.findIndex((x) => x.id === p.id);
        if (i < 0) throw new Error('suprimento não encontrado: ' + p.id);
        const patch = normalizaSuprimentoEntrada(p);
        state.suprimentos[i] = { ...state.suprimentos[i], ...patch, createdAt: state.suprimentos[i].createdAt };
        return JSON.stringify(state.suprimentos[i]);
      }
      case 'delete_suprimento':
        exigir('suprimentos', 'delete');
        state.suprimentos = state.suprimentos.filter((x) => x.id !== args.id);
        return null;

      // ---- gestão sos: serviços e fechamentos ----
      case 'list_servicos':
        exigir('gestao_sos_servicos', 'read');
        return JSON.stringify([...state.sosServicos]
          .sort((a, b) => b.numero - a.numero)
          .map(servicoToJsonMock));
      case 'create_servico': {
        exigir('gestao_sos_servicos', 'create');
        const patch = normalizaServicoEntrada(JSON.parse(args.payload));
        const novo = { ...patch, numero: state.sosProximoNumero++, fechamentoId: '' };
        state.sosServicos.push(novo);
        return JSON.stringify(servicoToJsonMock(novo));
      }
      case 'update_servico': {
        exigir('gestao_sos_servicos', 'update');
        const p = JSON.parse(args.payload);
        const i = state.sosServicos.findIndex((x) => x.id === p.id);
        if (i < 0) throw new Error('serviço não encontrado: ' + p.id);
        if (state.sosServicos[i].fechamentoId) {
          throw new Error('este serviço já está num mês fechado — reabra o fechamento para editar');
        }
        const patch = normalizaServicoEntrada(p);
        state.sosServicos[i] = { ...state.sosServicos[i], ...patch };
        return JSON.stringify(servicoToJsonMock(state.sosServicos[i]));
      }
      case 'delete_servico': {
        exigir('gestao_sos_servicos', 'delete');
        const alvo = state.sosServicos.find((x) => x.id === args.id);
        if (!alvo) throw new Error('serviço não encontrado: ' + args.id);
        if (alvo.fechamentoId) throw new Error('este serviço já está num mês fechado — reabra o fechamento para excluir');
        state.sosServicos = state.sosServicos.filter((x) => x.id !== args.id);
        return null;
      }
      case 'list_fechamentos':
        exigir('gestao_sos_servicos', 'read');
        return JSON.stringify([...state.sosFechamentos].sort((a, b) => cmp(b.mesReferencia, a.mesReferencia)));
      case 'fechar_mes': {
        exigir('gestao_sos_servicos', 'update');
        const f = JSON.parse(args.payload);
        if (!ehMesReferenciaValido(f.mesReferencia)) throw new Error('data de referência inválida (use mês/ano)');
        // Sem checagem de "já foi fechado": o mesmo mês pode ser fechado mais
        // de uma vez ao longo do tempo (fecha, reabre para corrigir, fecha de
        // novo) — o que importa é ter serviço em aberto agora.
        // Só entra quem já foi pago — mesmo critério de fecharMes em
        // commissions_engine.cpp. Quem não pagou fica em aberto pra um
        // fechamento futuro, assim que for marcado como pago.
        const doMes = state.sosServicos.filter((s) => s.dataReferencia === f.mesReferencia && !s.fechamentoId && s.pago);
        if (!doMes.length) throw new Error('nenhum serviço aberto E pago em ' + f.mesReferencia + ' para fechar');
        const novo = {
          id: f.id, mesReferencia: f.mesReferencia,
          quantidadeServicos: doMes.length,
          totalVenda: doMes.reduce((a, s) => a + s.venda, 0),
          totalComissao: doMes.reduce((a, s) => a + s.venda * s.porcentagem / 100, 0),
          observacoes: String(f.observacoes || '').trim(),
          fechadoEm: f.fechadoEm, reabertoEm: '', createdAt: f.createdAt,
        };
        state.sosFechamentos.push(novo);
        doMes.forEach((s) => { s.fechamentoId = novo.id; });
        return JSON.stringify(novo);
      }
      case 'reabrir_fechamento': {
        exigir('gestao_sos_servicos', 'update');
        const alvo = state.sosFechamentos.find((x) => x.id === args.id);
        if (!alvo) throw new Error('fechamento não encontrado: ' + args.id);
        state.sosServicos.forEach((s) => { if (s.fechamentoId === args.id) s.fechamentoId = ''; });
        // O REGISTRO fica — só marcado como reaberto. Todo fechamento
        // permanece no histórico para sempre (ver commissions_engine.cpp).
        alvo.reabertoEm = new Date().toISOString();
        return null;
      }
      case 'get_sos_config':
        exigir('gestao_sos_servicos', 'read');
        return JSON.stringify(state.sosConfig);
      case 'set_sos_config': {
        exigir('gestao_sos_servicos', 'update');
        const p = JSON.parse(args.payload);
        const num = parseFloat(p.porcentagemPadrao);
        if (p.porcentagemPadrao && (Number.isNaN(num) || num < 0 || num > 100)) {
          throw new Error('a porcentagem padrão precisa estar entre 0 e 100');
        }
        // Cada chamada só grava as chaves que mandou — mesmo critério do
        // Api::setSosConfig real: a tela de Serviços manda só
        // porcentagemPadrao, a de Configurações manda só os percentuais do
        // Dashboard, e nenhuma apaga o que a outra gravou.
        state.sosConfig = { ...state.sosConfig, ...p };
        return null;
      }

      // ---- compras: aquisições ----
      case 'list_aquisicoes':
        exigir('aquisicoes', 'read');
        return JSON.stringify([...state.comprasAquisicoes].sort((a, b) => cmp(b.dataCompra, a.dataCompra)));
      case 'create_aquisicao': {
        exigir('aquisicoes', 'create');
        const novo = { ...normalizaAquisicaoEntrada(JSON.parse(args.payload)), anexoPath: '', anexoTipo: '' };
        state.comprasAquisicoes.push(novo);
        return JSON.stringify(novo);
      }
      case 'update_aquisicao': {
        exigir('aquisicoes', 'update');
        const p = JSON.parse(args.payload);
        const i = state.comprasAquisicoes.findIndex((x) => x.id === p.id);
        if (i < 0) throw new Error('aquisição não encontrada: ' + p.id);
        // Editar outro campo nunca mexe no anexo — mesmo critério do UPDATE
        // real (purchases_engine.cpp), que não toca anexo_path/anexo_tipo.
        const anterior = state.comprasAquisicoes[i];
        state.comprasAquisicoes[i] = {
          ...normalizaAquisicaoEntrada(p), anexoPath: anterior.anexoPath, anexoTipo: anterior.anexoTipo,
        };
        return JSON.stringify(state.comprasAquisicoes[i]);
      }
      case 'delete_aquisicao': {
        exigir('aquisicoes', 'delete');
        if (!state.comprasAquisicoes.some((x) => x.id === args.id)) throw new Error('aquisição não encontrada: ' + args.id);
        if (state.comprasPagamentos.some((x) => x.aquisicaoId === args.id)) {
          throw new Error('existem pagamentos lançados para esta aquisição — exclua-os primeiro');
        }
        state.comprasAquisicoes = state.comprasAquisicoes.filter((x) => x.id !== args.id);
        return null;
      }
      case 'upload_aquisicao_attachment': {
        exigir('aquisicoes', 'update');
        // Sem filesystem no navegador: o mock não redimensiona pra A4 nem
        // decodifica o PDF de verdade (isso é bridge/src/attachments.rs em
        // produção) — só guarda a própria data: URL como se fosse o
        // "caminho", pra revisão visual do fluxo (mesmo critério de
        // upload_product_image acima). O tipo é detectado pela assinatura
        // do PDF (%PDF), mesma regra de is_pdf() no Rust.
        const i = args.input;
        const a = state.comprasAquisicoes.find((x) => x.id === i.aquisicaoId);
        if (!a) throw new Error('aquisição não encontrada: ' + i.aquisicaoId);
        let ehPdf = false;
        try { ehPdf = atob(i.fileBase64.slice(0, 8)).startsWith('%PDF'); } catch { /* trata como imagem */ }
        a.anexoTipo = ehPdf ? 'pdf' : 'imagem';
        a.anexoPath = `data:${ehPdf ? 'application/pdf' : 'image/webp'};base64,${i.fileBase64}`;
        return JSON.stringify(a);
      }
      case 'delete_aquisicao_attachment': {
        exigir('aquisicoes', 'update');
        const a = state.comprasAquisicoes.find((x) => x.id === args.aquisicaoId);
        if (!a) throw new Error('aquisição não encontrada: ' + args.aquisicaoId);
        a.anexoPath = ''; a.anexoTipo = '';
        return JSON.stringify(a);
      }
      case 'read_aquisicao_attachment':
        // No mock, o "caminho relativo" já É a data: URL guardada acima.
        return args.relativePath || '';

      // ---- compras: orçamentos (fluxo de cotação) ----
      case 'list_ordens_orcamento':
        exigir('orcamentos', 'read');
        return JSON.stringify(
          [...state.ordensOrcamento].sort((a, b) => b.numero - a.numero)
            .map((o) => ordemOrcamentoToJsonMock(o, args.nowIso)));

      case 'create_ordem_orcamento': {
        exigir('orcamentos', 'create');
        const p = JSON.parse(args.payload);
        const condominio = state.condominios.find((c) => c.id === p.condominioId);
        if (!condominio) throw new Error('condomínio não encontrado: ' + p.condominioId);
        if (!String(p.descricao || '').trim()) throw new Error('informe a descrição do pedido');
        const ordem = {
          id: p.id, numero: ++state.ordensOrcamentoSeq, condominioId: condominio.id,
          condominioNome: condominio.nome, descricao: String(p.descricao).trim(),
          observacoes: String(p.observacoes || '').trim(), status: 'pendente',
          propostaRecomendadaId: '', propostaAprovadaId: '', dataSolicitacao: '', dataEnvioCliente: '',
          dataAprovacao: '', reabertoEm: '', createdAt: p.createdAt, propostas: [],
        };
        state.ordensOrcamento.push(ordem);
        return JSON.stringify(ordemOrcamentoToJsonMock(ordem, p.createdAt));
      }

      case 'update_ordem_orcamento_info': {
        exigir('orcamentos', 'update');
        const p = JSON.parse(args.payload);
        const ordem = state.ordensOrcamento.find((x) => x.id === p.id);
        if (!ordem) throw new Error('ordem de orçamento não encontrada: ' + p.id);
        if (!String(p.descricao || '').trim()) throw new Error('informe a descrição do pedido');
        ordem.descricao = String(p.descricao).trim();
        ordem.observacoes = String(p.observacoes || '').trim();
        return JSON.stringify(ordemOrcamentoToJsonMock(ordem, ordem.createdAt));
      }

      case 'delete_ordem_orcamento':
        exigir('orcamentos', 'delete');
        state.ordensOrcamento = state.ordensOrcamento.filter((x) => x.id !== args.id);
        return null;

      case 'solicitar_orcamento_para_empresas': {
        exigir('orcamentos', 'update');
        const { payload, nowIso: agora } = args.input;
        const p = JSON.parse(payload);
        const ordem = state.ordensOrcamento.find((x) => x.id === p.ordemId);
        if (!ordem) throw new Error('ordem de orçamento não encontrada: ' + p.ordemId);
        const empresas = Array.isArray(p.empresas) ? p.empresas : [];
        if (!empresas.length) throw new Error('selecione ao menos uma empresa');

        // Não duplica quem já está na ordem — permite chamar de novo pra
        // adicionar só as empresas novas de uma seleção maior.
        const jaAntes = ordem.propostas.map((x) => x.empresaId);
        const novas = [];
        for (const e of empresas) {
          if (!e.empresaId || jaAntes.includes(e.empresaId)) continue;
          const empresa = state.empresas.find((x) => x.id === e.empresaId);
          if (!empresa) throw new Error('empresa não encontrada: ' + e.empresaId);
          const proposta = {
            id: ordem.id + '-' + empresa.id, ordemId: ordem.id, empresaId: empresa.id,
            empresaNome: empresa.nome, valor: -1, anexoPath: '', anexoTipo: '',
            emailEnviadoEm: agora, recomendada: false, createdAt: agora,
          };
          ordem.propostas.push(proposta);
          novas.push(empresa);
        }
        if (!ordem.dataSolicitacao) ordem.dataSolicitacao = agora;
        ordem.status = 'solicitado';

        const falhas = [];
        for (const empresa of novas) {
          const to = String(empresa.emails || '').trim();
          if (!to) continue;
          const erro = enviarEmailMock(to, 'Solicitação de orçamento — ' + ordem.condominioNome,
            'Pedido: ' + ordem.descricao);
          if (erro) falhas.push(empresa.nome + ': ' + erro);
        }
        const out = ordemOrcamentoToJsonMock(ordem, agora);
        if (falhas.length) out.emailErros = falhas;
        return JSON.stringify(out);
      }

      case 'reenviar_solicitacao_proposta': {
        exigir('orcamentos', 'update');
        const { propostaId, nowIso: agora } = args.input;
        const ordem = state.ordensOrcamento.find((x) => x.propostas.some((p) => p.id === propostaId));
        if (!ordem) throw new Error('proposta não encontrada: ' + propostaId);
        const proposta = ordem.propostas.find((p) => p.id === propostaId);
        proposta.emailEnviadoEm = agora;

        const out = ordemOrcamentoToJsonMock(ordem, agora);
        const empresa = state.empresas.find((x) => x.id === proposta.empresaId);
        const to = empresa ? String(empresa.emails || '').trim() : '';
        if (to) {
          const erro = enviarEmailMock(to, 'Solicitação de orçamento — ' + ordem.condominioNome,
            'Pedido: ' + ordem.descricao);
          if (erro) out.emailErros = [empresa.nome + ': ' + erro];
        }
        return JSON.stringify(out);
      }

      case 'set_proposta_valor': {
        exigir('orcamentos', 'update');
        const { propostaId, valor } = args.input;
        if (valor < 0) throw new Error('o valor não pode ser negativo');
        const ordem = state.ordensOrcamento.find((o) => o.propostas.some((x) => x.id === propostaId));
        if (!ordem) throw new Error('proposta não encontrada: ' + propostaId);
        const proposta = ordem.propostas.find((x) => x.id === propostaId);
        proposta.valor = valor;
        return JSON.stringify(propostaOrcamentoToJsonMock(proposta));
      }

      case 'upload_proposta_attachment': {
        exigir('orcamentos', 'update');
        const i = args.input;
        const ordem = state.ordensOrcamento.find((o) => o.id === i.ordemId);
        if (!ordem) throw new Error('ordem de orçamento não encontrada: ' + i.ordemId);
        const proposta = ordem.propostas.find((x) => x.id === i.propostaId);
        if (!proposta) throw new Error('proposta não encontrada: ' + i.propostaId);
        let ehPdf = false;
        try { ehPdf = atob(i.fileBase64.slice(0, 8)).startsWith('%PDF'); } catch { /* trata como imagem */ }
        proposta.anexoTipo = ehPdf ? 'pdf' : 'imagem';
        proposta.anexoPath = `data:${ehPdf ? 'application/pdf' : 'image/webp'};base64,${i.fileBase64}`;
        return JSON.stringify(propostaOrcamentoToJsonMock(proposta));
      }

      case 'delete_proposta_attachment': {
        exigir('orcamentos', 'update');
        const { ordemId, propostaId } = args.input;
        const ordem = state.ordensOrcamento.find((o) => o.id === ordemId);
        if (!ordem) throw new Error('ordem de orçamento não encontrada: ' + ordemId);
        const proposta = ordem.propostas.find((x) => x.id === propostaId);
        if (!proposta) throw new Error('proposta não encontrada: ' + propostaId);
        proposta.anexoPath = ''; proposta.anexoTipo = '';
        return JSON.stringify(propostaOrcamentoToJsonMock(proposta));
      }

      case 'read_proposta_attachment':
        // No mock, o "caminho relativo" já É a data: URL guardada acima.
        return args.relativePath || '';

      case 'marcar_proposta_recomendada': {
        exigir('orcamentos', 'update');
        const { ordemId, propostaId } = args.input;
        const ordem = state.ordensOrcamento.find((o) => o.id === ordemId);
        if (!ordem) throw new Error('ordem de orçamento não encontrada: ' + ordemId);
        const existe = ordem.propostas.some((x) => x.id === propostaId);
        if (!existe) throw new Error('proposta não encontrada nesta ordem: ' + propostaId);
        ordem.propostas.forEach((x) => { x.recomendada = x.id === propostaId; });
        ordem.propostaRecomendadaId = propostaId;
        return JSON.stringify(ordemOrcamentoToJsonMock(ordem, ordem.createdAt));
      }

      case 'desmarcar_proposta_recomendada': {
        exigir('orcamentos', 'update');
        const ordem = state.ordensOrcamento.find((o) => o.id === args.ordemId);
        if (!ordem) throw new Error('ordem de orçamento não encontrada: ' + args.ordemId);
        ordem.propostas.forEach((x) => { x.recomendada = false; });
        ordem.propostaRecomendadaId = '';
        return JSON.stringify(ordemOrcamentoToJsonMock(ordem, ordem.createdAt));
      }

      case 'enviar_orcamento_para_cliente': {
        exigir('orcamentos', 'update');
        const { payload, nowIso: agora } = args.input;
        const p = JSON.parse(payload);
        const ordem = state.ordensOrcamento.find((x) => x.id === p.ordemId);
        if (!ordem) throw new Error('ordem de orçamento não encontrada: ' + p.ordemId);
        if (!ordem.propostas.length) throw new Error('solicite ao menos uma empresa antes de enviar');
        ordem.status = 'enviado_cliente';
        ordem.dataEnvioCliente = agora;

        const condominio = state.condominios.find((c) => c.id === ordem.condominioId);
        const destinatario = String(p.destinatarioEmail || '').trim() ||
          (condominio ? String(condominio.email || '').trim() : '');
        const falhas = [];
        if (destinatario) {
          const erro = enviarEmailMock(destinatario, 'Orçamentos — ' + ordem.descricao, p.mensagemExtra || '');
          if (erro) falhas.push(destinatario + ': ' + erro);
        }
        const out = ordemOrcamentoToJsonMock(ordem, agora);
        if (falhas.length) out.emailErros = falhas;
        return JSON.stringify(out);
      }

      case 'aprovar_proposta_orcamento': {
        exigir('orcamentos', 'update');
        const { ordemId, propostaId, nowIso: agora } = args.input;
        const ordem = state.ordensOrcamento.find((x) => x.id === ordemId);
        if (!ordem) throw new Error('ordem de orçamento não encontrada: ' + ordemId);
        const proposta = ordem.propostas.find((x) => x.id === propostaId);
        if (!proposta) throw new Error('proposta não encontrada nesta ordem: ' + propostaId);
        ordem.status = 'aprovado';
        ordem.propostaAprovadaId = propostaId;
        ordem.dataAprovacao = agora;

        const falhas = [];
        const empresa = state.empresas.find((e) => e.id === proposta.empresaId);
        const to = empresa ? String(empresa.emails || '').trim() : '';
        if (to) {
          const erro = enviarEmailMock(to, 'Proposta aprovada — ' + ordem.descricao, '');
          if (erro) falhas.push(to + ': ' + erro);
        }
        const out = ordemOrcamentoToJsonMock(ordem, agora);
        if (falhas.length) out.emailErros = falhas;
        return JSON.stringify(out);
      }

      case 'reativar_ordem_orcamento': {
        exigir('orcamentos', 'update');
        const { ordemId, nowIso: agora } = args.input;
        const ordem = state.ordensOrcamento.find((x) => x.id === ordemId);
        if (!ordem) throw new Error('ordem de orçamento não encontrada: ' + ordemId);
        ordem.reabertoEm = agora;
        return JSON.stringify(ordemOrcamentoToJsonMock(ordem, agora));
      }

      case 'get_email_config': {
        exigirSuperadmin('configurar o envio de e-mail');
        const c = state.emailConfig;
        return JSON.stringify({
          host: c.host, port: c.port, username: c.username, fromEmail: c.fromEmail,
          fromName: c.fromName, useTls: c.useTls, temSenha: !!c.password,
        });
      }

      case 'set_email_config': {
        exigirSuperadmin('configurar o envio de e-mail');
        const p = JSON.parse(args.payload);
        if ('host' in p) state.emailConfig.host = p.host;
        if ('port' in p) state.emailConfig.port = p.port;
        if ('username' in p) state.emailConfig.username = p.username;
        if ('password' in p) state.emailConfig.password = p.password;
        if ('fromEmail' in p) state.emailConfig.fromEmail = p.fromEmail;
        if ('fromName' in p) state.emailConfig.fromName = p.fromName;
        if ('useTls' in p) state.emailConfig.useTls = !!p.useTls;
        return null;
      }

      case 'send_test_email': {
        exigirSuperadmin('configurar o envio de e-mail');
        const erro = enviarEmailMock(args.to, 'Teste de configuração de e-mail — Estoque FL',
          'Se esta mensagem chegou, a configuração de SMTP está funcionando.');
        if (erro) throw new Error(erro);
        return null;
      }

      // No navegador (este mock) não existe "abrir o Outlook" de verdade —
      // isso é bridge/src/commands.rs::abrir_email_outlook, que chama
      // explorer.exe no Windows. Aqui só registra em emailsEnviados (mesmo
      // lugar que o teste usa) pra dar pra inspecionar em teste manual.
      case 'abrir_email_outlook': {
        state.emailsEnviados.push({ to: args.to, subject: args.subject, bodyHtml: args.body,
          enviadoEm: new Date().toISOString(), viaOutlook: true });
        return null;
      }

      // ---- compras: pagamentos ----
      case 'list_pagamentos':
        exigir('pagamentos', 'read');
        return JSON.stringify([...state.comprasPagamentos]
          .sort((a, b) => cmp(b.dataEmissao, a.dataEmissao)).map(pagamentoToJsonMock));
      case 'create_pagamento': {
        exigir('pagamentos', 'create');
        const input = JSON.parse(args.payload);
        const novo = { ...normalizaPagamentoEntrada(input), parcelas: normalizaParcelas(input.parcelas) };
        state.comprasPagamentos.push(novo);
        return JSON.stringify(pagamentoToJsonMock(novo));
      }
      case 'update_pagamento': {
        exigir('pagamentos', 'update');
        const input = JSON.parse(args.payload);
        const i = state.comprasPagamentos.findIndex((x) => x.id === input.id);
        if (i < 0) throw new Error('pagamento não encontrado: ' + input.id);
        const existente = state.comprasPagamentos[i];
        const header = normalizaPagamentoEntrada({ ...input, aquisicaoId: existente.aquisicaoId });
        // Preserva pago/dataPagamento de parcela com o MESMO id; nova nasce em aberto.
        const parcelas = normalizaParcelas(input.parcelas).map((parc) => {
          const anterior = existente.parcelas.find((x) => x.id === parc.id);
          return anterior ? { ...parc, pago: anterior.pago, dataPagamento: anterior.dataPagamento } : parc;
        });
        state.comprasPagamentos[i] = { ...existente, ...header, parcelas };
        return JSON.stringify(pagamentoToJsonMock(state.comprasPagamentos[i]));
      }
      case 'delete_pagamento':
        exigir('pagamentos', 'delete');
        state.comprasPagamentos = state.comprasPagamentos.filter((x) => x.id !== args.id);
        return null;
      case 'marcar_parcela': {
        exigir('pagamentos', 'update');
        const p = JSON.parse(args.payload);
        const pagamento = state.comprasPagamentos.find((x) => x.id === p.pagamentoId);
        if (!pagamento) throw new Error('pagamento não encontrado: ' + p.pagamentoId);
        const parcela = pagamento.parcelas.find((x) => x.id === p.parcelaId);
        if (!parcela) throw new Error('parcela não encontrada: ' + p.parcelaId);
        if (p.pago && !p.dataPagamento) throw new Error('informe a data do pagamento');
        parcela.pago = !!p.pago;
        parcela.dataPagamento = p.pago ? p.dataPagamento : '';
        return JSON.stringify(pagamentoToJsonMock(pagamento));
      }

      // ---- gestão de prazos ----
      case 'list_condominios':
        return JSON.stringify(state.condominios);
      case 'create_condominio': {
        exigirNome(args.condominio.nome, 'informe o nome do condomínio');
        exigirLocalizacao(args.condominio.localizacao);
        // ativo=true por padrão quando ausente — mesmo default de dto.rs
        // (default_true) no lado real do Tauri.
        const cond = { ativo: true, deltaSindica: false, ...args.condominio };
        state.condominios.push(cond);
        return JSON.stringify(cond);
      }
      case 'update_condominio': {
        const i = state.condominios.findIndex((c) => c.id === args.condominio.id);
        if (i < 0) throw new Error('condomínio não encontrado');
        exigirNome(args.condominio.nome, 'informe o nome do condomínio');
        exigirLocalizacao(args.condominio.localizacao);
        state.condominios[i] = { ...state.condominios[i], ...args.condominio };
        return JSON.stringify(state.condominios[i]);
      }
      case 'delete_condominio': {
        const doCondominio = state.servicosCondominio.filter((v) => v.condominioId === args.id).map((v) => v.id);
        state.renovacoes = state.renovacoes.filter((r) => !doCondominio.includes(r.servicoCondominioId));
        state.servicosCondominio = state.servicosCondominio.filter((v) => v.condominioId !== args.id);
        state.condominios = state.condominios.filter((c) => c.id !== args.id);
        return null;
      }

      case 'list_tipos_servico':
        return JSON.stringify(state.tiposServico);
      case 'create_tipo_servico':
        exigirTipoServico(args.tipo);
        state.tiposServico.push(args.tipo);
        return JSON.stringify(args.tipo);
      case 'update_tipo_servico': {
        const i = state.tiposServico.findIndex((t) => t.id === args.tipo.id);
        if (i < 0) throw new Error('tipo de serviço não encontrado');
        exigirTipoServico(args.tipo);
        state.tiposServico[i] = { ...state.tiposServico[i], ...args.tipo };
        return JSON.stringify(state.tiposServico[i]);
      }
      case 'delete_tipo_servico':
        if (state.servicosCondominio.some((v) => v.tipoServicoId === args.id)) {
          throw new Error('este tipo de serviço está em uso por um ou mais condomínios — remova os vínculos antes de excluí-lo');
        }
        state.tiposServico = state.tiposServico.filter((t) => t.id !== args.id);
        return null;

      case 'list_servicos_condominio':
        return JSON.stringify(state.servicosCondominio.map((v) => vinculoToJson(v, args.input.nowIso)));
      case 'create_servico_condominio': {
        const p = JSON.parse(args.payload);
        if (!state.condominios.some((c) => c.id === p.condominioId)) throw new Error('condomínio não encontrado');
        if (!state.tiposServico.some((t) => t.id === p.tipoServicoId)) throw new Error('tipo de serviço não encontrado');
        const { nowIso: hoje, ...vinculo } = p;
        state.servicosCondominio.push(vinculo);
        return JSON.stringify(vinculoToJson(vinculo, hoje));
      }
      case 'update_servico_condominio': {
        const p = JSON.parse(args.payload);
        const i = state.servicosCondominio.findIndex((v) => v.id === p.id);
        if (i < 0) throw new Error('vínculo de serviço não encontrado');
        const { nowIso: hoje, ...patch } = p;
        state.servicosCondominio[i] = { ...state.servicosCondominio[i], ...patch };
        return JSON.stringify(vinculoToJson(state.servicosCondominio[i], hoje));
      }
      case 'delete_servico_condominio':
        state.renovacoes = state.renovacoes.filter((r) => r.servicoCondominioId !== args.id);
        state.servicosCondominio = state.servicosCondominio.filter((v) => v.id !== args.id);
        return null;

      case 'renovar_servico': {
        const p = JSON.parse(args.payload);
        const v = state.servicosCondominio.find((x) => x.id === p.servicoCondominioId);
        if (!v) throw new Error('vínculo de serviço não encontrado');
        const tipo = state.tiposServico.find((t) => t.id === v.tipoServicoId);
        state.renovacoes.push({ id: p.id, servicoCondominioId: p.servicoCondominioId,
          dataRenovacao: p.dataRenovacao, empresaContratada: p.empresaContratada || '',
          prazoDiasAplicado: tipo ? tipo.prazoDias : 0, observacoes: p.observacoes || '',
          createdAt: p.createdAt });
        v.dataUltimaRenovacao = p.dataRenovacao;
        v.empresaContratada = p.empresaContratada || '';
        return JSON.stringify(vinculoToJson(v, p.nowIso));
      }
      case 'list_renovacoes': {
        const filtro = args.input.servicoCondominioFilter;
        const lista = filtro ? state.renovacoes.filter((r) => r.servicoCondominioId === filtro) : state.renovacoes;
        return JSON.stringify([...lista]
          .sort((a, b) => cmp(b.dataRenovacao, a.dataRenovacao) || cmp(b.createdAt, a.createdAt))
          .map(renovacaoToJson));
      }

      default:
        throw new Error('comando de mock não implementado: ' + cmd);
    }
  });
}
