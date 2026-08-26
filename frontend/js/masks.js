// Máscaras de digitação para campos de cadastro (telefone, CNPJ, CEP, dinheiro)
// — padrão pedido explicitamente para o sistema inteiro:
//   telefone fixo:      (XX)XXXX-XXXX
//   celular/whatsapp:   (XX)X.XXXX-XXXX
//   CNPJ:               XX.XXX.XXX/XXXX-XX
//   CEP:                XXXXX-XXX
//   dinheiro:           R$ 0.000,00
//
// Cada formatador é uma função pura (string -> string), testável sem DOM;
// bindMask/bindMoneyMask são os únicos pedaços que tocam <input>.

function soDigitos(s) {
  return String(s || '').replace(/\D/g, '');
}

// Só um campo "Telefone" em cada cadastro (nunca fixo/celular separados) —
// o formato muda sozinho conforme a quantidade de dígitos: até 10 dígitos é
// tratado como fixo, o 11º dígito (o "9" do celular) vira o formato com
// ponto do celular/WhatsApp.
export function maskTelefone(value) {
  const d = soDigitos(value).slice(0, 11);
  if (!d) return '';
  if (d.length <= 2) return `(${d}`;
  const ddd = d.slice(0, 2);
  const resto = d.slice(2);
  if (d.length <= 10) {
    if (resto.length <= 4) return `(${ddd})${resto}`;
    return `(${ddd})${resto.slice(0, 4)}-${resto.slice(4)}`;
  }
  // 11 dígitos: celular/WhatsApp — (XX)X.XXXX-XXXX
  const nono = resto.slice(0, 1);
  const fim = resto.slice(1);
  if (fim.length <= 4) return `(${ddd})${nono}.${fim}`;
  return `(${ddd})${nono}.${fim.slice(0, 4)}-${fim.slice(4, 8)}`;
}

export function maskCnpj(value) {
  const d = soDigitos(value).slice(0, 14);
  let out = d.slice(0, 2);
  if (d.length > 2) out += '.' + d.slice(2, 5);
  if (d.length > 5) out += '.' + d.slice(5, 8);
  if (d.length > 8) out += '/' + d.slice(8, 12);
  if (d.length > 12) out += '-' + d.slice(12, 14);
  return out;
}

export function maskCep(value) {
  const d = soDigitos(value).slice(0, 8);
  if (d.length > 5) return d.slice(0, 5) + '-' + d.slice(5);
  return d;
}

// Máscara de dinheiro estilo "calculadora": os dígitos digitados preenchem
// da direita para a esquerda (sempre os 2 últimos são os centavos), então a
// posição do cursor no meio do texto não importa — é o mesmo comportamento
// dos caixas eletrônicos e apps bancários brasileiros. `raw` é a string de
// dígitos (sem zeros à esquerda além do necessário) que representa centavos.
export function maskMoneyFromDigits(raw) {
  const d = soDigitos(raw).replace(/^0+(?=\d)/, '');
  const centavos = (d || '0').padStart(3, '0');
  const inteiro = centavos.slice(0, -2);
  const dec = centavos.slice(-2);
  const inteiroFmt = inteiro.replace(/\B(?=(\d{3})+(?!\d))/g, '.');
  return `R$ ${inteiroFmt},${dec}`;
}

// Da string formatada ("R$ 1.234,56") de volta para o número (1234.56) —
// só extrai os dígitos e divide por 100, já que a formatação acima é sempre
// puramente digit-driven (nunca ambígua).
export function parseMoneyMasked(value) {
  const d = soDigitos(value);
  if (!d) return 0;
  return parseInt(d, 10) / 100;
}

// Liga um formatador simples (telefone/CNPJ/CEP) a um <input type="text">:
// a cada digitação, reescreve o valor já mascarado. Formatadores da esquerda
// pra direita como estes sempre colocam o cursor no fim — aceitável para
// campos curtos e de padrão fixo como estes.
export function bindMask(input, formatterFn) {
  input.addEventListener('input', () => {
    input.value = formatterFn(input.value);
  });
}

// Liga a máscara de dinheiro a um <input type="text">.
//
// NUNCA reformata a partir do texto já exibido (input.value): um campo
// preenchido com "R$ 0,00" tem três dígitos "0" nele, e se o cursor não
// estiver garantidamente no fim (o que um clique do usuário no meio do
// campo pode mudar), o dígito novo entra misturado com esses zeros e
// corrompe o valor silenciosamente. Em vez disso, os dígitos "de verdade"
// (centavos) ficam guardados à parte em `input.dataset.moneyDigits` — dígito
// digitado sempre entra no FIM dessa string, backspace sempre tira do fim,
// e o texto exibido é só a formatação dela. `input` (paste, autofill etc.)
// é o único caminho que ainda lê o texto exibido, como uma rede de segurança.
export function bindMoneyMask(input) {
  if (input.dataset.moneyDigits === undefined) {
    input.dataset.moneyDigits = soDigitos(input.value) || '0';
  }
  const render = () => {
    input.value = maskMoneyFromDigits(input.dataset.moneyDigits);
    input.setSelectionRange(input.value.length, input.value.length);
  };
  input.addEventListener('keydown', (e) => {
    if (e.key >= '0' && e.key <= '9') {
      e.preventDefault();
      const atual = input.dataset.moneyDigits === '0' ? '' : input.dataset.moneyDigits;
      input.dataset.moneyDigits = (atual + e.key).slice(-12);
      render();
    } else if (e.key === 'Backspace' || e.key === 'Delete') {
      e.preventDefault();
      input.dataset.moneyDigits = input.dataset.moneyDigits.slice(0, -1) || '0';
      render();
    }
  });
  // Rede de segurança para colar texto (Ctrl+V) — não há tecla individual
  // pra interceptar, então aqui sim vale reler o texto inteiro que apareceu.
  input.addEventListener('input', () => {
    input.dataset.moneyDigits = soDigitos(input.value) || '0';
    render();
  });
}

// Preenche o campo já mascarado a partir de um número (ex.: ao abrir um
// modal de edição) — mesma fonte de verdade (dataset.moneyDigits) usada
// durante a digitação, pra digitar em seguida continuar a partir daqui.
export function setMoneyMaskedValue(input, numero) {
  const centavos = Math.max(0, Math.round((Number(numero) || 0) * 100));
  input.dataset.moneyDigits = String(centavos);
  input.value = maskMoneyFromDigits(input.dataset.moneyDigits);
}
