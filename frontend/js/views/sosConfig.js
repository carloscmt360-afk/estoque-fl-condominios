import { api, errorText } from '../api.js';
import { toast } from '../components/toast.js';
import { can } from '../session.js';

// Gestão SOS > Configurações.
//
// A porcentagem padrão só PRÉ-PREENCHE o campo ao lançar um novo serviço —
// nunca é uma regra fixa. Para corrigir a porcentagem de um lançamento já
// feito, edita-se o próprio serviço na tela de Serviços (botão Editar).

let wired = false;

// Campos do painel "Dashboard de Fechamento — percentuais" — cada grupo tem
// sua própria checagem de soma (renderSomas), mas todos são salvos juntos
// pela mesma função: são todos a mesma "aba de percentuais" (trocar um sem
// revisar os outros do mesmo grupo é o erro mais provável — grupo que não
// soma 100% —, então salvar tudo de uma vez deixa o aviso de soma visível
// no momento exato em que valeria a pena parar e conferir). Os campos de
// pagamento da Delta (texto, não percentual) moraram nesta mesma aba até a
// aba própria "Delta" (view-deltaConfig) existir — continuam salvos pela
// mesma função/tabela, só com um botão "Salvar" próprio naquela aba.
const CAMPOS_DASHBOARD_CONFIG = [
  ['cfgRateioFl', 'rateioFl'], ['cfgRateioGerentes', 'rateioGerentes'],
  ['cfgRateioSuprimentos', 'rateioSuprimentos'],
  ['cfgSuprimentosEncarregado', 'suprimentosEncarregado'], ['cfgSuprimentosAssistente', 'suprimentosAssistente'],
  ['cfgDeltaSindica', 'deltaSindica'], ['cfgDeltaGerente', 'deltaGerente'],
  ['cfgMetaPorCondominio', 'metaPorCondominio'],
];

export async function initSosConfig() {
  if (!wired) {
    wired = true;
    document.getElementById('btnSalvarSosConfig').addEventListener('click', salvarPadrao);
    document.getElementById('btnSalvarDashboardConfig').addEventListener('click', salvarDashboardConfig);
    document.getElementById('btnSalvarDeltaConfig').addEventListener('click', salvarDashboardConfig);
    ['cfgRateioFl', 'cfgRateioGerentes', 'cfgRateioSuprimentos',
     'cfgSuprimentosEncarregado', 'cfgSuprimentosAssistente',
     'cfgDeltaSindica', 'cfgDeltaGerente'].forEach((id) =>
      document.getElementById(id).addEventListener('input', renderSomas));
  }
  await reload();
}

// Alguns valores antigos foram salvos com o float inteiro (ex.: "55.000000")
// — arredonda pra 1 casa decimal na exibição, que é a precisão que faz
// sentido pra um percentual de rateio. Continua um <input type="number">
// comum (ponto, não vírgula); é o próprio navegador quem mostra no formato
// local.
function fmt1(v) {
  const n = parseFloat(v);
  return Number.isFinite(n) ? n.toFixed(1) : '';
}

export async function reload() {
  const config = await api.getSosConfig();

  const podeEditar = can('gestao_sos_servicos', 'update');
  document.getElementById('cfgPorcentagemPadrao').value = fmt1(config.porcentagemPadrao);
  document.getElementById('btnSalvarSosConfig').style.display = podeEditar ? '' : 'none';
  document.getElementById('cfgPorcentagemPadrao').disabled = !podeEditar;

  for (const [id, chave] of CAMPOS_DASHBOARD_CONFIG) {
    document.getElementById(id).value = fmt1(config[chave]);
    document.getElementById(id).disabled = !podeEditar;
  }
  // Dados de pagamento da Delta — texto, não percentual, mas salvos junto
  // no mesmo botão (mesmo painel).
  document.getElementById('cfgDeltaTitular').value = config.deltaTitular || '';
  document.getElementById('cfgDeltaTitular').disabled = !podeEditar;
  document.getElementById('cfgDeltaChavePix').value = config.deltaChavePix || '';
  document.getElementById('cfgDeltaChavePix').disabled = !podeEditar;
  document.getElementById('btnSalvarDashboardConfig').style.display = podeEditar ? '' : 'none';
  document.getElementById('btnSalvarDeltaConfig').style.display = podeEditar ? '' : 'none';
  renderSomas();
}

function numDe(id) {
  return parseFloat(document.getElementById(id).value) || 0;
}

// Só avisa — nunca bloqueia salvar. Uma soma diferente de 100%/30% pode ser
// proposital num mês de transição entre acordos; o dono da conta decide.
function pillSoma(elId, soma, esperado) {
  const el = document.getElementById(elId);
  const ok = Math.abs(soma - esperado) < 0.01;
  el.className = `pill ${ok ? 'pill-ok' : 'pill-low'}`;
  el.textContent = ok ? `✓ soma ${esperado}%` : `soma ${soma.toLocaleString('pt-BR')}% — esperado ${esperado}%`;
}

function renderSomas() {
  const fl = numDe('cfgRateioFl'), ger = numDe('cfgRateioGerentes'), sup = numDe('cfgRateioSuprimentos');
  pillSoma('cfgSomaRateio', fl + ger + sup, 100);

  const enc = numDe('cfgSuprimentosEncarregado'), ass = numDe('cfgSuprimentosAssistente');
  pillSoma('cfgSomaSuprimentos', enc + ass, 100);

  const delta = numDe('cfgDeltaSindica'), gerDelta = numDe('cfgDeltaGerente');
  document.getElementById('cfgDeltaBaseTexto').textContent = ger.toLocaleString('pt-BR');
  pillSoma('cfgSomaDelta', delta + gerDelta, ger);
}

async function salvarPadrao() {
  const valor = document.getElementById('cfgPorcentagemPadrao').value.trim();
  const num = parseFloat(valor);
  if (valor && (Number.isNaN(num) || num < 0 || num > 100)) {
    toast('A porcentagem padrão precisa estar entre 0 e 100.', 'error');
    return;
  }
  try {
    await api.setSosConfig({ porcentagemPadrao: valor ? fmt1(num) : '0' });
    toast('Configurações salvas.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}

async function salvarDashboardConfig() {
  const payload = {};
  for (const [id, chave] of CAMPOS_DASHBOARD_CONFIG) {
    const valor = document.getElementById(id).value.trim();
    const num = parseFloat(valor);
    if (valor && (Number.isNaN(num) || num < 0)) {
      toast('Todos os percentuais precisam ser números válidos, não negativos.', 'error');
      return;
    }
    payload[chave] = valor ? fmt1(num) : '0';
  }
  payload.deltaTitular = document.getElementById('cfgDeltaTitular').value.trim();
  payload.deltaChavePix = document.getElementById('cfgDeltaChavePix').value.trim();
  try {
    await api.setSosConfig(payload);
    toast('Percentuais e dados de pagamento salvos.', 'success');
  } catch (e) { toast('Erro: ' + errorText(e), 'error'); }
}
