import { api, errorText, rodandoNoApp } from './api.js';
import { installOverlayClickToClose, openModal, closeModal } from './components/modal.js';
import { toast } from './components/toast.js';
import { initLogoFl } from './components/logoFl.js';
import { setSession, getSession, currentUser, isSuperadmin, can, roleLabel } from './session.js';
import { initLogin, showLogin, hideLogin } from './views/login.js';
import { initDashboard, renderReport } from './views/dashboard.js';
import { initRetrospect, reload as reloadRetrospect } from './views/retrospect.js';
import { initProducts, reload as reloadProducts } from './views/products.js';
import { initDepartments, reload as reloadDepartments } from './views/departments.js';
import { initMovements, reload as reloadMovements } from './views/movements.js';
import { initImportExport } from './views/importExport.js';
import { initRequests, reload as reloadRequests } from './views/requests.js';
import { initUsers, reload as reloadUsers } from './views/users.js';
import { initPermissions, reload as reloadPermissions } from './views/permissions.js';
import { initCondominios, reload as reloadCondominios } from './views/condominios.js';
import { initPrazos, reload as reloadPrazos } from './views/prazos.js';
import { initCatalogoEmpresas, reload as reloadCatalogoEmpresas } from './views/catalogoEmpresas.js';
import { initSetorizacao, reload as reloadSetorizacao } from './views/setorizacao.js';
import { initEmpresas, reload as reloadEmpresas } from './views/empresas.js';
import { initParceiros, reload as reloadParceiros } from './views/parceiros.js';
import { initSosPainel, reload as reloadSosPainel } from './views/sosPainel.js';
import { initAquisicoes, reload as reloadAquisicoes } from './views/aquisicoes.js';
import { initOrcamentos, reload as reloadOrcamentos } from './views/orcamentos.js';
import { initPagamentos, reload as reloadPagamentos } from './views/pagamentos.js';
import { initGerentes, reload as reloadGerentes } from './views/gerentes.js';
import { initCarteiras, reload as reloadCarteiras } from './views/carteiras.js';
import { initSuprimentos, reload as reloadSuprimentos } from './views/suprimentos.js';
import { initDeltaSindicos, reload as reloadDeltaSindicos } from './views/deltaSindicos.js';
import { initServicos, reload as reloadServicos } from './views/servicos.js';
import { initFechamento, reload as reloadFechamento } from './views/fechamento.js';
import { initHistoricoFechamentos, reload as reloadHistoricoFechamentos } from './views/historicoFechamentos.js';
import { initDashboardFechamento, reload as reloadDashboardFechamento } from './views/dashboardFechamento.js';
import { initProgramarPagamento, reload as reloadProgramarPagamento } from './views/programarPagamento.js';
import { initHistoricoPagamentos, reload as reloadHistoricoPagamentos } from './views/historicoPagamentos.js';
import { initSosConfig, reload as reloadSosConfig } from './views/sosConfig.js';

// Catálogo de telas e a permissão que cada uma exige. É daqui que sai o menu:
// o usuário só vê o que ele realmente pode abrir. Isso é conveniência — quem
// barra de verdade é o C++, que confere a permissão em cada chamada.
// Ícones outline (stroke="currentColor", 20x20) — mesmo estilo em toda a
// barra lateral, para não misturar com emoji (que rendem de tamanho e cor
// inconsistentes entre plataformas).
const ICO_DASHBOARD = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 15V9M10 15V5M16 15v-4"/></svg>';
const ICO_RETROSPECT = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4.5" width="14" height="12" rx="2"/><path d="M3 8.5h14M7 2.5v3M13 2.5v3"/></svg>';
const ICO_PRODUCTS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6.5 10 3l7 3.5-7 3.5-7-3.5Z"/><path d="M3 6.5v7L10 17l7-3.5v-7"/><path d="M10 10v7"/></svg>';
const ICO_MOVEMENTS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><path d="M10 6v4l3 2"/></svg>';
const ICO_REQUESTS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="3" width="10" height="14" rx="1.5"/><path d="M8 3V2h4v1M7.5 8h5M7.5 11h5M7.5 14h3"/></svg>';
const ICO_DEPARTMENTS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="3" width="12" height="14" rx="1"/><path d="M7 6.5h1M12 6.5h1M7 9.5h1M12 9.5h1M7 12.5h1M12 12.5h1M8.5 17v-3h3v3"/></svg>';
const ICO_USERS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="7" r="3.2"/><path d="M4 17c0-3.3 2.7-5.5 6-5.5s6 2.2 6 5.5"/></svg>';
const ICO_PERMISSIONS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="9" width="10" height="8" rx="1.5"/><path d="M7 9V6.5a3 3 0 0 1 6 0V9"/></svg>';
const ICO_IMPORT_EXPORT = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h11M12 4l3 3-3 3"/><path d="M16 13H5M8 10l-3 3 3 3"/></svg>';
// Ícones dos MÓDULOS (o primeiro nível do menu)
const ICO_ESTOQUE = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 7 10 3.5 17.5 7v6L10 16.5 2.5 13V7Z"/><path d="M2.5 7 10 10.5 17.5 7M10 10.5v6"/></svg>';
// Compras: carrinho — o módulo em si.
const ICO_COMPRAS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 4h2l1.6 9.2a1.5 1.5 0 0 0 1.5 1.3h6.4a1.5 1.5 0 0 0 1.5-1.3L17 7H6"/><circle cx="8.5" cy="17" r="1.2"/><circle cx="14.5" cy="17" r="1.2"/></svg>';
// Aquisições: uma caixa recebida.
const ICO_AQUISICOES = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6.5 10 3l7 3.5-7 3.5-7-3.5Z"/><path d="M3 6.5v7L10 17l7-3.5v-7"/><path d="M10 10v7"/></svg>';
// Orçamentos: um documento com valor — proposta.
const ICO_ORCAMENTOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 3h7l3 3v11H5z"/><path d="M12 3v3h3"/><path d="M7.5 11.5h5M7.5 14h3.5"/></svg>';
// Pagamentos: uma nota com cifrão — a NF e o dinheiro dela.
const ICO_PAGAMENTOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="14" height="12" rx="1.5"/><path d="M10 6.5v7M11.7 7.8c-.3-.5-1-.8-1.7-.8-1 0-1.8.6-1.8 1.5s.8 1.2 1.8 1.5c1 .3 1.8.6 1.8 1.5s-.8 1.5-1.8 1.5c-.7 0-1.4-.3-1.7-.8"/></svg>';
const ICO_EMPRESAS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 17V5.5A1.5 1.5 0 0 1 4.5 4h6A1.5 1.5 0 0 1 12 5.5V17"/><path d="M12 9h3.5A1.5 1.5 0 0 1 17 10.5V17M2 17h16"/><path d="M6 7.5h3M6 10.5h3M6 13.5h3"/></svg>';
const ICO_SOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><circle cx="10" cy="10" r="2.8"/><path d="M5 5l3 3M15 5l-3 3M5 15l3-3M15 15l-3-3"/></svg>';
const ICO_PRAZOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4.5" width="14" height="12" rx="2"/><path d="M3 8.5h14M7 2.5v3M13 2.5v3"/><path d="M10 11v2.2l1.6 1"/></svg>';
const ICO_ADMIN = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="2.6"/><path d="M10 2.5v2M10 15.5v2M2.5 10h2M15.5 10h2M4.7 4.7l1.4 1.4M13.9 13.9l1.4 1.4M15.3 4.7l-1.4 1.4M6.1 13.9l-1.4 1.4"/></svg>';
const ICO_FORNECEDORES = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M2 8.5h9v6H2zM11 10.5h3.5l2.5 2.5v1.5h-6z"/><circle cx="5" cy="15.5" r="1.5"/><circle cx="14" cy="15.5" r="1.5"/><path d="M4 5.5h5"/></svg>';
const ICO_PARCEIROS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="7" cy="7" r="2.6"/><circle cx="14" cy="8.5" r="2.2"/><path d="M2.5 16c0-2.6 2-4.3 4.5-4.3s4.5 1.7 4.5 4.3"/><path d="M13 12c2.3 0 4 1.5 4 3.7"/></svg>';
// Catálogo: lupa sobre uma lista — é a tela de ACHAR a empresa do segmento.
const ICO_CATALOGO = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 4.5h7M3 8h7M3 11.5h4"/><circle cx="13" cy="12" r="3.5"/><path d="M15.6 14.6 18 17"/></svg>';
// Setorização: quatro blocos — os quatro setores que a tela organiza.
const ICO_SETORIZACAO = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="6" height="6" rx="1.2"/><rect x="11" y="3" width="6" height="6" rx="1.2"/><rect x="3" y="11" width="6" height="6" rx="1.2"/><rect x="11" y="11" width="6" height="6" rx="1.2"/></svg>';
const ICO_CONDOMINIOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M3 17V8l4-3 4 3v9"/><path d="M11 17V10.5l3-2 3 2V17M2 17h16"/><path d="M5.5 10h1M5.5 13h1M8 10h1M8 13h1M13.5 12.5h1M13.5 15h1"/></svg>';
// Painel (SOS): barras — mesma forma de ICO_DASHBOARD, para ler como "visão
// de números" já na lupa do menu.
const ICO_PAINEL_SOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 15V9M10 15V5M16 15v-4"/></svg>';
// Gerentes: uma pessoa (a mesma forma de ICO_USERS, para ler como "pessoa
// responsável" à primeira vista).
const ICO_GERENTES = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="7" r="3.2"/><path d="M4 17c0-3.3 2.7-5.5 6-5.5s6 2.2 6 5.5"/></svg>';
// Carteiras: uma pasta — o "conjunto de condomínios" que cada gerente carrega.
const ICO_CARTEIRAS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 6.5a1.5 1.5 0 0 1 1.5-1.5h3l1.5 2h7a1.5 1.5 0 0 1 1.5 1.5v6a1.5 1.5 0 0 1-1.5 1.5H4a1.5 1.5 0 0 1-1.5-1.5v-8Z"/></svg>';
// Delta Síndicos: o delta (triângulo) sobre a planilha — a comissão do síndico.
const ICO_DELTA_SINDICOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M10 3.5 16.5 15h-13z"/><path d="M7 11.5h6"/></svg>';
// Suprimentos: um crachá — equipe de campo, identificada por categoria.
const ICO_SUPRIMENTOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="3" width="10" height="14" rx="2"/><circle cx="10" cy="8" r="2"/><path d="M7 13.5h6"/></svg>';
// Serviços: a planilha (grade de linhas/colunas).
const ICO_SERVICOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3.5" width="14" height="13" rx="1.5"/><path d="M3 8h14M8 3.5v13"/></svg>';
// Fechamento: um cadeado — trava o mês.
const ICO_FECHAMENTO = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="4.5" y="9" width="11" height="8" rx="1.5"/><path d="M6.5 9V6.5a3.5 3.5 0 0 1 7 0V9"/></svg>';
// Histórico de fechamentos: relógio — o mesmo símbolo de "tempo passado" do resto do app.
const ICO_HISTORICO_FECHAMENTOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><path d="M10 6v4l3 2"/></svg>';
// Configurações (SOS): a mesma engrenagem de ICO_ADMIN.
const ICO_CONFIG_SOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="2.6"/><path d="M10 2.5v2M10 15.5v2M2.5 10h2M15.5 10h2M4.7 4.7l1.4 1.4M13.9 13.9l1.4 1.4M15.3 4.7l-1.4 1.4M6.1 13.9l-1.4 1.4"/></svg>';
// Dashboard de Fechamento: barras de gráfico — é a apresentação financeira do mês, não o "fechar/travar" (ICO_FECHAMENTO, que é um cadeado).
const ICO_DASHBOARD_FECHAMENTO = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 16V9M10 16V4M16 16v-6"/></svg>';
// Programar Pagamento: cifrão — autorizar quem recebe quanto.
const ICO_PROGRAMAR_PAGAMENTO = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M10 3v14M13.5 6.5c0-1.4-1.6-2.5-3.5-2.5s-3.5 1-3.5 2.5S8 8.5 10 9s3.5 1.1 3.5 2.5S11.9 14 10 14s-3.5-1.1-3.5-2.5"/></svg>';
// Histórico de pagamentos: mesmo relógio de ICO_HISTORICO_FECHAMENTOS — mesma ideia de "registro permanente do passado".
const ICO_HISTORICO_PAGAMENTOS = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><path d="M10 6v4l3 2"/></svg>';

/* MÓDULOS do sistema — o primeiro nível do menu.

   O sistema deixou de ser só estoque: as telas de estoque passaram a viver
   dentro do módulo "Estoque", e os demais módulos entram ao lado dele. Um
   módulo com `telas` vira um grupo que abre/fecha; um módulo sem `telas` é um
   botão direto (`view` é a seção que ele abre).

   `visivel()` de um grupo é derivado: o grupo aparece se ao menos uma tela
   dele aparecer — assim quem só pode requisitar continua vendo "Estoque" com
   um item só dentro, e não um grupo vazio. */
const MODULOS = [
  {
    id: 'estoque',
    label: 'Estoque',
    ico: ICO_ESTOQUE,
    telas: [
      { id: 'dashboard', label: 'Relatório Mensal', ico: ICO_DASHBOARD, visivel: () => can('relatorio_mensal', 'read') },
      { id: 'retrospect', label: 'Retrospecto', ico: ICO_RETROSPECT, visivel: () => can('retrospecto', 'read') },
      { id: 'products', label: 'Produtos', ico: ICO_PRODUCTS, visivel: () => can('produtos', 'read') },
      { id: 'movements', label: 'Linha do Tempo', ico: ICO_MOVEMENTS, visivel: () => can('linha_do_tempo', 'read') },
      // Requisições aparece também para quem só pode CRIAR pedido (sem histórico):
      // é a tela onde a criação acontece.
      { id: 'requests', label: 'Requisições', ico: ICO_REQUESTS,
        visivel: () => can('requisicoes', 'read') || can('requisicoes', 'create') },
    ],
  },
  /* Compras. Aquisições e Orçamentos se ligam aos fornecedores cadastrados em
     "Fornecedores e Prestadores de Serviços"; Orçamentos também se liga ao
     condomínio (é uma proposta PARA ele). Gestão de Prazos entrou aqui porque
     também é sobre renovar contrato com fornecedor. */
  {
    id: 'compras',
    label: 'Compras',
    ico: ICO_COMPRAS,
    telas: [
      { id: 'aquisicoes', label: 'Aquisições FL', ico: ICO_AQUISICOES,
        visivel: () => can('aquisicoes', 'read') },
      { id: 'orcamentos', label: 'Orçamentos', ico: ICO_ORCAMENTOS,
        visivel: () => can('orcamentos', 'read') },
      { id: 'prazos', label: 'Gestão de Prazos', ico: ICO_PRAZOS,
        visivel: () => can('gestao_datas', 'read') },
    ],
  },
  // Módulos ainda por construir. Restritos ao superadministrador enquanto são
  // só um espaço reservado: um "em construção" no menu de todo mundo é ruído
  // para quem só quer pedir material. Quando cada um ganhar conteúdo de
  // verdade, troca-se por permissão própria (como as telas de estoque).
  /* Fornecedores e Prestadores de Serviços. A ordem das três telas é a ordem
     de uso, não a de construção: o Catálogo vem primeiro porque é o que se
     abre no dia a dia (achar quem atende uma demanda); Setorização e Cadastro
     são manutenção do que alimenta esse catálogo. */
  {
    id: 'empresas',
    label: 'Fornecedores e Prestadores de Serviços',
    ico: ICO_EMPRESAS,
    telas: [
      { id: 'catalogoEmpresas', label: 'Catálogo de empresas', ico: ICO_CATALOGO,
        visivel: () => can('empresas', 'read') },
      { id: 'setorizacao', label: 'Setorização', ico: ICO_SETORIZACAO,
        visivel: () => can('empresas', 'read') },
      { id: 'empresas', label: 'Cadastro', ico: ICO_FORNECEDORES,
        visivel: () => can('empresas', 'read') },
    ],
  },
  { id: 'condominios', label: 'Condomínios', ico: ICO_CONDOMINIOS, view: 'condominios',
    visivel: () => can('condominios', 'read') },
  /* Gestão SOS. Ganhou a primeira tela de verdade: Parceiros, alimentada pelo
     Sim/Não da ficha da empresa. As demais telas do módulo entram aqui à
     medida que forem construídas. */
  {
    id: 'sos',
    label: 'Gestão SOS',
    ico: ICO_SOS,
    // Gerentes/Carteiras/Suprimentos saíram do menu principal — agora só se
    // chega a eles pela sub-navegação DENTRO de Configurações (ver o
    // ".config-hub-btn" repetido nas 4 telas em index.html e o listener
    // delegado em wireShell), junto com "Percentuais" (o antigo conteúdo de
    // Configurações). Continuam sendo telas de verdade (view própria, JS
    // próprio) — só não aparecem mais soltas aqui.
    telas: [
      { id: 'sosPainel', label: 'Painel', ico: ICO_PAINEL_SOS,
        visivel: () => can('gestao_sos_servicos', 'read') },
      { id: 'servicos', label: 'Serviços', ico: ICO_SERVICOS,
        visivel: () => can('gestao_sos_servicos', 'read') },
      { id: 'dashboardFechamento', label: 'Dashboard de Fechamento', ico: ICO_DASHBOARD_FECHAMENTO,
        visivel: () => can('gestao_sos_servicos', 'read') },
      { id: 'fechamento', label: 'Fechamento', ico: ICO_FECHAMENTO,
        visivel: () => can('gestao_sos_servicos', 'read') },
      { id: 'historicoFechamentos', label: 'Histórico de fechamentos', ico: ICO_HISTORICO_FECHAMENTOS,
        visivel: () => can('gestao_sos_servicos', 'read') },
      { id: 'deltaSindicos', label: 'Delta Síndicos', ico: ICO_DELTA_SINDICOS,
        visivel: () => can('delta_sindicos', 'read') },
      { id: 'programarPagamento', label: 'Programar pagamento', ico: ICO_PROGRAMAR_PAGAMENTO,
        visivel: () => can('gestao_sos_servicos', 'read') },
      { id: 'historicoPagamentos', label: 'Histórico de pagamentos', ico: ICO_HISTORICO_PAGAMENTOS,
        visivel: () => can('gestao_sos_servicos', 'read') },
      { id: 'configuracoesSos', label: 'Configurações', ico: ICO_CONFIG_SOS,
        visivel: () => can('gestao_sos_servicos', 'read') },
      { id: 'parceiros', label: 'Parceiros', ico: ICO_PARCEIROS,
        visivel: () => can('empresas', 'read') },
    ],
  },
  /* Usuários, Departamentos e Permissões valem para o SISTEMA INTEIRO, não só
     para o estoque — um usuário e o setor dele são os mesmos em Empresas, SOS
     e Prazos, e é aqui que se concede acesso a qualquer módulo. Por isso saíram
     de dentro de "Estoque" e viraram um módulo próprio, no fim do menu. */
  {
    id: 'admin',
    label: 'Administração',
    ico: ICO_ADMIN,
    telas: [
      { id: 'users', label: 'Usuários', ico: ICO_USERS, visivel: () => isSuperadmin() },
      { id: 'departments', label: 'Departamentos', ico: ICO_DEPARTMENTS, visivel: () => can('departamentos', 'read') },
      { id: 'permissions', label: 'Permissões', ico: ICO_PERMISSIONS, visivel: () => isSuperadmin() },
    ],
  },
  /* Sempre o ÚLTIMO item do menu. Não é do estoque: o backup salva e restaura
     o banco inteiro, então conforme os outros módulos ganharem dados ele
     passa a cobrir todos eles também. */
  { id: 'importExport', label: 'Importar / Exportar', ico: ICO_IMPORT_EXPORT, view: 'importExport',
    visivel: () => can('importar_exportar', 'read') },
];

/* Módulos com as telas já filtradas pela permissão de quem está logado;
   módulos que ficariam vazios não entram. */
function modulosVisiveis() {
  return MODULOS
    .map((m) => (m.telas ? { ...m, telas: m.telas.filter((t) => t.visivel()) } : m))
    .filter((m) => (m.telas ? m.telas.length > 0 : m.visivel()));
}

/* Primeira tela que o usuário pode abrir — é para onde o app vai ao entrar. */
function primeiraTela(modulos) {
  for (const m of modulos) {
    if (m.telas && m.telas.length) return m.telas[0].id;
    if (!m.telas) return m.view;
  }
  return null;
}

const initialized = new Set();

/* `opts.manterFechados` = não abrir o grupo da tela ativa. Usado só na
   navegação inicial (ver entrarNoApp): ao entrar, o menu começa todo
   minimizado, e abrir um grupo sozinho contrariaria isso. Em qualquer outra
   navegação o grupo abre normalmente — o item ativo nunca pode ficar
   escondido dentro de um grupo fechado. */
async function switchView(view, opts) {
  opts = opts || {};
  document.querySelectorAll('.nav-item').forEach((b) => b.classList.toggle('active', b.dataset.view === view));
  document.querySelectorAll('.nav-grupo').forEach((g) => {
    const contem = !!g.querySelector(`[data-view="${view}"]`);
    // `tem-ativa` marca o grupo mesmo fechado, então a tela aberta continua
    // sinalizada no menu minimizado (ver .nav-grupo.fechado.tem-ativa).
    g.classList.toggle('tem-ativa', contem);
    if (contem && !opts.manterFechados) {
      g.classList.remove('fechado');
      const cab = g.querySelector('[data-abre]');
      if (cab) cab.setAttribute('aria-expanded', 'true');
    }
  });
  document.querySelectorAll('.view').forEach((s) => s.classList.remove('active'));
  const secao = document.getElementById('view-' + view);
  if (secao) secao.classList.add('active');

  try {
    if (!initialized.has(view)) {
      initialized.add(view);
      if (view === 'dashboard') await initDashboard();
      else if (view === 'retrospect') await initRetrospect();
      else if (view === 'products') await initProducts();
      else if (view === 'movements') await initMovements();
      else if (view === 'departments') await initDepartments();
      else if (view === 'requests') await initRequests();
      else if (view === 'aquisicoes') await initAquisicoes();
      else if (view === 'orcamentos') await initOrcamentos();
      else if (view === 'pagamentos') await initPagamentos();
      else if (view === 'users') await initUsers();
      else if (view === 'permissions') await initPermissions();
      else if (view === 'condominios') await initCondominios();
      else if (view === 'prazos') await initPrazos();
      else if (view === 'catalogoEmpresas') await initCatalogoEmpresas();
      else if (view === 'setorizacao') await initSetorizacao();
      else if (view === 'empresas') await initEmpresas();
      else if (view === 'parceiros') await initParceiros();
      else if (view === 'sosPainel') await initSosPainel();
      else if (view === 'gerentes') await initGerentes();
      else if (view === 'carteiras') await initCarteiras();
      else if (view === 'suprimentos') await initSuprimentos();
      else if (view === 'deltaSindicos') await initDeltaSindicos();
      else if (view === 'servicos') await initServicos();
      else if (view === 'fechamento') await initFechamento();
      else if (view === 'historicoFechamentos') await initHistoricoFechamentos();
      else if (view === 'dashboardFechamento') await initDashboardFechamento();
      else if (view === 'programarPagamento') await initProgramarPagamento();
      else if (view === 'historicoPagamentos') await initHistoricoPagamentos();
      else if (view === 'configuracoesSos') await initSosConfig();
      else if (view === 'importExport') initImportExport();
    } else {
      // views já inicializadas recarregam os dados ao voltar a ficar visíveis
      if (view === 'dashboard') await renderReport();
      else if (view === 'retrospect') await reloadRetrospect();
      else if (view === 'products') await reloadProducts();
      else if (view === 'movements') await reloadMovements();
      else if (view === 'departments') await reloadDepartments();
      else if (view === 'requests') await reloadRequests();
      else if (view === 'aquisicoes') await reloadAquisicoes();
      else if (view === 'orcamentos') await reloadOrcamentos();
      else if (view === 'pagamentos') await reloadPagamentos();
      else if (view === 'users') await reloadUsers();
      else if (view === 'permissions') await reloadPermissions();
      else if (view === 'condominios') await reloadCondominios();
      else if (view === 'prazos') await reloadPrazos();
      else if (view === 'catalogoEmpresas') await reloadCatalogoEmpresas();
      else if (view === 'setorizacao') await reloadSetorizacao();
      else if (view === 'empresas') await reloadEmpresas();
      else if (view === 'parceiros') await reloadParceiros();
      else if (view === 'sosPainel') await reloadSosPainel();
      else if (view === 'gerentes') await reloadGerentes();
      else if (view === 'carteiras') await reloadCarteiras();
      else if (view === 'suprimentos') await reloadSuprimentos();
      else if (view === 'deltaSindicos') await reloadDeltaSindicos();
      else if (view === 'servicos') await reloadServicos();
      else if (view === 'fechamento') await reloadFechamento();
      else if (view === 'historicoFechamentos') await reloadHistoricoFechamentos();
      else if (view === 'dashboardFechamento') await reloadDashboardFechamento();
      else if (view === 'programarPagamento') await reloadProgramarPagamento();
      else if (view === 'historicoPagamentos') await reloadHistoricoPagamentos();
      else if (view === 'configuracoesSos') await reloadSosConfig();
      else if (view === 'importExport') initImportExport();
    }
  } catch (e) {
    // Uma view que falha ao carregar não pode derrubar o app inteiro: mostra o
    // motivo e deixa o usuário navegar para outra. Sessão perdida é tratada
    // pelo evento global (ver boot).
    initialized.delete(view);
    toast('Não foi possível carregar esta tela: ' + errorText(e), 'error');
  }
}

const CHEVRON = '<svg class="chevron" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M7.5 5l5 5-5 5"/></svg>';

function renderSidebar() {
  const modulos = modulosVisiveis();
  const nav = document.getElementById('sidebarNav');
  nav.innerHTML = modulos.map((m) => {
    if (!m.telas) {
      return `<button class="nav-item nav-modulo" data-view="${m.view}">` +
        `<span class="ico">${m.ico}</span> ${m.label}</button>`;
    }
    // Nasce FECHADO: ao entrar no sistema o menu mostra só os módulos, e
    // quem quiser ver as telas de um deles abre o que precisa.
    return `<div class="nav-grupo fechado" data-grupo="${m.id}">
      <button class="nav-item nav-modulo nav-grupo-cab" data-abre="${m.id}" aria-expanded="false">
        <span class="ico">${m.ico}</span> ${m.label} ${CHEVRON}
      </button>
      <div class="nav-filhos">
        ${m.telas.map((t) => `<button class="nav-item nav-filho" data-view="${t.id}">` +
          `<span class="ico">${t.ico}</span> ${t.label}</button>`).join('')}
      </div>
    </div>`;
  }).join('');

  nav.querySelectorAll('[data-view]').forEach((btn) => {
    btn.addEventListener('click', () => switchView(btn.dataset.view));
  });
  // Cabeçalho de grupo só abre/fecha — não troca de tela, porque "Estoque"
  // não é uma tela, é um conjunto delas.
  nav.querySelectorAll('[data-abre]').forEach((btn) => {
    btn.addEventListener('click', () => {
      const grupo = btn.closest('.nav-grupo');
      const aberto = grupo.classList.toggle('fechado') === false;
      btn.setAttribute('aria-expanded', String(aberto));
    });
  });
  return modulos;
}

function renderUserBox() {
  const u = currentUser();
  if (!u) return;
  const iniciais = u.name.trim().split(/\s+/).slice(0, 2).map((p) => p[0] || '').join('').toUpperCase();
  document.getElementById('userIniciais').textContent = iniciais || '?';
  document.getElementById('userNome').textContent = u.name;
  document.getElementById('userNome').title = u.email;
  document.getElementById('userPapel').textContent =
    roleLabel(u.role) + (u.departmentName ? ' · ' + u.departmentName : '');
}

async function entrarNoApp(sessao) {
  setSession(sessao);
  initialized.clear();
  hideLogin();
  document.getElementById('startupError').style.display = 'none';
  document.getElementById('appShell').style.display = 'grid';
  renderUserBox();
  const modulos = renderSidebar();
  await initLogoFl();
  await switchView(primeiraTela(modulos) || 'semAcesso', { manterFechados: true });
}

async function sair() {
  try {
    await api.logout();
  } catch (e) {
    // Falhar ao avisar o backend não pode prender ninguém dentro do app: a
    // tela volta para o login de qualquer jeito.
  }
  encerrarSessao();
}

function encerrarSessao(mensagem) {
  setSession(null);
  initialized.clear();
  document.getElementById('appShell').style.display = 'none';
  showLogin(mensagem);
}

async function trocarSenha() {
  const atual = document.getElementById('senhaAtual').value;
  const nova = document.getElementById('senhaNova').value;
  const repetir = document.getElementById('senhaNovaRepetir').value;
  if (!atual || !nova) { toast('Preencha a senha atual e a nova.', 'error'); return; }
  if (nova.length < 8) { toast('A nova senha precisa ter pelo menos 8 caracteres.', 'error'); return; }
  if (nova !== repetir) { toast('As duas senhas novas não conferem.', 'error'); return; }
  try {
    await api.changeOwnPassword(atual, nova);
    closeModal('modalTrocarSenha');
    toast('Senha alterada.', 'success');
  } catch (e) {
    toast('Erro: ' + errorText(e), 'error');
  }
}

/* Gatilho (avatar/nome) + menu de ações, na faixa fixa do rodapé da barra
   lateral. Abre para CIMA (ver .user-menu em components.css) porque é o pé
   da tela — não há espaço abaixo. Fecha ao escolher uma ação, clicar fora
   ou apertar Esc, como qualquer menu suspenso. */
function wireUserMenu() {
  const trigger = document.getElementById('userBoxTrigger');
  const menu = document.getElementById('userMenu');

  function fecharMenuUsuario() {
    menu.classList.remove('open');
    trigger.setAttribute('aria-expanded', 'false');
  }
  function abrirMenuUsuario() {
    menu.classList.add('open');
    trigger.setAttribute('aria-expanded', 'true');
  }

  trigger.addEventListener('click', (e) => {
    e.stopPropagation();
    if (menu.classList.contains('open')) fecharMenuUsuario();
    else abrirMenuUsuario();
  });
  // Qualquer ação escolhida fecha o menu — a própria ação (modal de senha,
  // logout, confirm de encerrar) já assume o controle da tela.
  menu.addEventListener('click', (e) => {
    if (e.target.closest('.user-menu-item')) fecharMenuUsuario();
  });
  document.addEventListener('click', (e) => {
    if (!menu.classList.contains('open')) return;
    if (trigger.contains(e.target) || menu.contains(e.target)) return;
    fecharMenuUsuario();
  });
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') fecharMenuUsuario();
  });
}

function wireShell() {
  wireUserMenu();
  // Sub-navegação de Configurações (Gestão SOS): o mesmo bloco de botões
  // (Percentuais/Gerentes/Carteiras/Suprimentos) se repete no topo das 4
  // telas (ver index.html), então um listener delegado só uma vez no boot
  // já cobre as 4 cópias — nenhuma delas some do DOM entre navegações
  // (switchView só alterna a classe .active da <section>).
  document.querySelectorAll('.config-hub-btn').forEach((b) =>
    b.addEventListener('click', () => switchView(b.dataset.view)));
  document.getElementById('btnSair').addEventListener('click', sair);
  // "Encerrar programa" fecha o app de vez — o X da janela apenas o esconde
  // na bandeja. Só existe no app instalado (no navegador não há o que fechar).
  const btnEncerrar = document.getElementById('btnEncerrarApp');
  if (rodandoNoApp) {
    btnEncerrar.style.display = '';
    btnEncerrar.addEventListener('click', () => {
      // Confirmação porque encerrar tira o app da bandeja: quem só queria
      // trocar de usuário procurava o "Sair" logo ao lado.
      if (confirm('Encerrar o programa? Ele sairá da bandeja do sistema.')) api.encerrarApp();
    });
  }
  document.getElementById('btnTrocarSenha').addEventListener('click', () => {
    ['senhaAtual', 'senhaNova', 'senhaNovaRepetir'].forEach((id) => {
      document.getElementById(id).value = '';
    });
    openModal('modalTrocarSenha');
  });
  document.getElementById('btnSalvarTrocaSenha').addEventListener('click', trocarSenha);

  // Qualquer chamada ao backend que volte "[auth]" (app reaberto, usuário
  // desativado, backup restaurado por cima) derruba para o login — uma vez só,
  // aqui, em vez de em cada view.
  //
  // Note que NÃO há um ouvinte de `estoque:dados-alterados` aqui: quem já
  // precisa dele o escuta por conta própria (a Linha do Tempo), e switchView
  // recarrega toda view ao voltar a ela. Um ouvinte genérico que recarregasse
  // a view atual entraria em laço — a própria view de Produtos dispara o
  // evento no fim do seu reload.
  document.addEventListener('estoque:sessao-perdida', () => {
    if (!getSession()) return;
    encerrarSessao('Sua sessão foi encerrada. Entre novamente.');
  });
}

async function boot() {
  try {
    // Fora do Tauri (aberto direto num navegador): instala o mock de
    // desenvolvimento alimentado por fixtures reais, só para revisão visual.
    if (!(window.__TAURI__ && window.__TAURI__.core)) {
      const { installDevMock } = await import('./devMock.js');
      await installDevMock();
    }

    installOverlayClickToClose();
    wireShell();
    initLogin(entrarNoApp);

    try {
      await api.appStatus();
    } catch (err) {
      showStartupError(String(err));
      document.getElementById('btnRetryInit').addEventListener('click', async () => {
        try {
          await api.retryInit();
          location.reload();
        } catch (e2) {
          showStartupError(String(e2));
        }
      });
      return;
    }

    document.getElementById('startupError').style.display = 'none';

    // A sessão vive no backend: reabrir a janela sem ter saído mantém quem
    // estava logado, e fechar o app derruba a sessão (é o comportamento certo
    // para um app de balcão, onde a máquina é compartilhada).
    const sessao = await api.currentSession();
    if (sessao) await entrarNoApp(sessao);
    else showLogin();
  } catch (err) {
    // Qualquer falha inesperada aqui (ex.: mock de desenvolvimento sem
    // fixtures) nunca deve resultar em tela branca silenciosa — sempre
    // mostra algo acionável, mesmo que a mensagem seja genérica.
    showStartupError(String(err && err.stack ? err.stack : err));
  }
}

function showStartupError(detail) {
  document.getElementById('appShell').style.display = 'none';
  document.getElementById('loginScreen').style.display = 'none';
  document.getElementById('startupError').style.display = 'block';
  document.getElementById('startupErrorDetail').textContent = detail;
}

boot();
