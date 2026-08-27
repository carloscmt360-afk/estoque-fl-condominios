// Quanto do arrecadado do mês sai da FL para as pessoas — gerentes, suprimentos
// e Delta somados. O acordo da empresa é que a FL fique com 55% do arrecadado,
// então distribuir mais de 45% é o sinal de que o mês fugiu do combinado: é
// esse o número que precisa saltar aos olhos em verde ou vermelho.
//
// Mora num módulo próprio porque as MESMAS três parcelas aparecem no Dashboard
// de Fechamento (gerenciaLiquido, distribuicaoCompras, descontos de Delta) e em
// Programar pagamento (totalGerentes, totalSuprimentos, totalDelta) — são os
// mesmos valores com outro nome, e a porcentagem tem que bater exatamente entre
// as duas telas e os dois impressos. Uma cópia da fórmula em cada lugar sairia
// do ar assim que uma delas mudasse.

import { fmtBRL } from './format.js';

export const LIMITE_DISTRIBUICAO_PCT = 45;

export function distribuicaoDoMes(arrecadado, totalGerentes, totalSuprimentos, totalDelta) {
  const distribuido = (Number(totalGerentes) || 0) + (Number(totalSuprimentos) || 0)
    + (Number(totalDelta) || 0);
  const base = Number(arrecadado) || 0;
  // Mês sem arrecadação nenhuma não tem porcentagem: 0 dividido por 0 não é
  // "0% distribuído" (não sobrou nada pra FL — também não entrou nada), e
  // dividir assim mesmo colocaria Infinity ou NaN na tela. `pct: null` obriga
  // quem desenha a mostrar "—".
  const pct = base > 0 ? (distribuido / base) * 100 : null;
  return {
    distribuido,
    pct,
    // Compara o valor ARREDONDADO, o mesmo que a tela mostra: 45,004% aparece
    // como "45,00%" e ficaria vermelho contradizendo o próprio número, mandando
    // o usuário procurar um estouro que a tela diz não existir. A cor tem que
    // concordar com o que está escrito ao lado dela.
    //
    // Estritamente MAIOR que o limite: 45% exatos ainda é o combinado, então
    // continua verde ("até 45% verde, se passar de 45% vermelho").
    acimaDoLimite: pct !== null && Math.round(pct * 100) / 100 > LIMITE_DISTRIBUICAO_PCT,
  };
}

// As mesmas três parcelas, lidas de um Dashboard de Fechamento. Programar
// pagamento guarda esses mesmos valores com outro nome (ver montarPagamentoSos
// em api.cpp: totalGerentes = gerenciaLiquido, totalSuprimentos = soma de
// distribuicaoCompras, totalDelta = soma dos descontos dos gerentes). É este
// mapeamento que garante a MESMA porcentagem nas duas telas — sem ele, cada uma
// somaria "o que tem à mão" e as duas dariam números diferentes para o mesmo mês.
export function distribuicaoDashboard(d) {
  const suprimentos = (d.distribuicaoCompras || [])
    .reduce((s, l) => s + (Number(l.valor) || 0), 0);
  const delta = (d.gerentes || []).reduce((s, g) => s + (Number(g.descontos) || 0), 0);
  return distribuicaoDoMes(d.arrecadado, d.gerenciaLiquido, suprimentos, delta);
}

// Texto único do rótulo/valor, pra tela e papel dizerem a mesma coisa.
export function distribuicaoPctTexto(pct) {
  return pct === null ? '—' : `${pct.toLocaleString('pt-BR', {
    minimumFractionDigits: 2, maximumFractionDigits: 2,
  })}%`;
}

// Descritor do quadro (sem HTML): as duas telas desenham do mesmo jeito, e o
// impresso reaproveita label/valor. Mês sem arrecadação fica sem cor nenhuma —
// pintar de verde um "—" diria que está tudo certo quando não se sabe.
export function tileDistribuido(dist) {
  const semBase = dist.pct === null;
  return {
    label: '% distribuído',
    value: distribuicaoPctTexto(dist.pct),
    classeTile: semBase ? '' : (dist.acimaDoLimite ? 'tile-acima-meta' : 'tile-dentro-meta'),
    classeValor: semBase ? '' : (dist.acimaDoLimite ? 'acima-meta' : 'dentro-meta'),
    foot: `${fmtBRL(dist.distribuido)} · limite ${LIMITE_DISTRIBUICAO_PCT}%`,
  };
}
