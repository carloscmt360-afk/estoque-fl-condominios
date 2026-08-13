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
  function passo(s, m) {
    if (m.type === 'entrada') {
      const base = Math.max(s.qty, 0);
      s.avgCost = base + m.qty > 0 ? (base * s.avgCost + m.qty * (m.unitPrice || 0)) / (base + m.qty) : 0;
      s.qty += m.qty;
    } else if (m.type === 'saida') s.qty -= m.qty;
    else s.qty += m.qty;
  }
  /* Espelha inventory_engine.cpp::recomputeProduct, inclusive a preservação
     do offset entre o razão e o saldo gravado — sem isso o mock mostraria um
     comportamento de edição diferente do app real. */
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
          return { productId: p.id, name: p.name, unit: p.unit, category: p.category || '',
            qty: p.qty, reserved, available: p.qty - reserved, avgCost: p.avgCost, minStock: p.minStock };
        }));
      case 'list_requests': {
        exigir('requisicoes', 'read');
        const u = usuarioAtual();
        const lista = podeValidar() ? state.requests : state.requests.filter((r) => r.departmentId === u.departmentId);
        return JSON.stringify([...lista].sort((a, b) => cmp(b.createdAt, a.createdAt)));
      }
      case 'create_request': {
        exigir('requisicoes', 'create');
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
          items: itens };
        state.requests.push(r);
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

      case 'list_products': return JSON.stringify(state.products);
      case 'list_departments': return JSON.stringify(state.departments);
      case 'create_product': {
        const p = { ...args.product, qty: 0, avgCost: 0 };
        state.products.push(p);
        return JSON.stringify(p);
      }
      case 'update_product': {
        const i = state.products.findIndex((p) => p.id === args.product.id);
        if (i >= 0) state.products[i] = { ...state.products[i], ...args.product };
        return JSON.stringify(state.products[i]);
      }
      case 'delete_product':
        state.products = state.products.filter((p) => p.id !== args.id);
        state.movements = state.movements.filter((m) => m.productId !== args.id);
        return null;
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
          obs: i.motivo, date: i.date, resultingQty: 0, resultingAvgCost: 0, createdAt: i.createdAt };
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
      default:
        throw new Error('comando de mock não implementado: ' + cmd);
    }
  });
}
