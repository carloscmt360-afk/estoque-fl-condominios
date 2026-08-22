// Caixa de busca acima de um <select> longo.
//
// Com um catálogo de centenas de materiais, achar um item rolando a lista do
// <select> é inviável — esta caixa filtra as opções conforme se digita, sem
// trocar o <select> por um componente customizado (o resto do código continua
// lendo `sel.value` normalmente, e o teclado/leitor de tela continuam
// funcionando como num campo nativo).
//
// A comparação ignora acento e maiúscula/minúscula (paraBusca, em format.js):
// digitar "abracadeira" encontra "ABRAÇADEIRA DE NYLON".
import { paraBusca } from '../format.js';

// selectId -> { input, opcoes: [{value, label, html}] }
const registrados = new Map();

/* Instala a caixa de busca (uma vez só) e guarda a lista completa de opções.

   Chame DEPOIS de preencher o <select>: a lista de opções é fotografada aqui,
   e é dessa cópia que o filtro trabalha — sem ela, filtrar apagaria opções do
   DOM e não haveria como trazê-las de volta ao limpar a busca. */
export function instalarFiltroSelect(selectId, placeholder) {
  const sel = document.getElementById(selectId);
  if (!sel) return;

  let reg = registrados.get(selectId);
  if (!reg) {
    const input = document.createElement('input');
    input.type = 'search';
    input.className = 'filtro-select';
    input.placeholder = placeholder || 'Digite para buscar...';
    // O <select> costuma estar dentro de um .field junto com o <label>;
    // a caixa entra logo acima dele, dentro do mesmo bloco.
    sel.parentElement.insertBefore(input, sel);
    reg = { input, opcoes: [] };
    registrados.set(selectId, reg);

    input.addEventListener('input', () => aplicar(selectId));
    // Enter numa caixa de busca dentro de modal não deve enviar formulário —
    // aqui ele só passa o foco para a lista já filtrada.
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); sel.focus(); }
    });
  }

  reg.opcoes = [...sel.options].map((o) => ({ value: o.value, label: o.textContent, html: o.outerHTML }));
  reg.input.value = '';
  aplicar(selectId);
}

function aplicar(selectId) {
  const reg = registrados.get(selectId);
  const sel = document.getElementById(selectId);
  if (!reg || !sel) return;

  const busca = paraBusca(reg.input.value).trim();
  const anterior = sel.value;
  const visiveis = busca
    ? reg.opcoes.filter((o) => paraBusca(o.label).includes(busca))
    : reg.opcoes;

  if (!visiveis.length) {
    // Um <select> vazio não explica nada; esta opção desabilitada explica.
    sel.innerHTML = '<option value="" disabled selected>Nenhum material encontrado</option>';
    return;
  }

  sel.innerHTML = visiveis.map((o) => o.html).join('');
  // Mantém o item escolhido se ele sobreviveu ao filtro; senão assume o
  // primeiro visível, para o <select> nunca ficar com valor fantasma (um
  // value que não corresponde a nenhuma <option> some silenciosamente).
  sel.value = visiveis.some((o) => o.value === anterior) ? anterior : visiveis[0].value;
  // Trocar o valor por código não dispara evento sozinho, e as telas que
  // dependem da seleção (dica de saldo, custo médio, foto do material) se
  // penduram ora em 'input' (products.js) ora em 'change' (requests.js) —
  // dispara os dois para nenhuma delas ficar exibindo o item anterior.
  if (sel.value !== anterior) {
    sel.dispatchEvent(new Event('input', { bubbles: true }));
    sel.dispatchEvent(new Event('change', { bubbles: true }));
  }
}
