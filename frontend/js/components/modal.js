export function openModal(id) {
  document.getElementById(id).classList.add('open');
}
export function closeModal(id) {
  document.getElementById(id).classList.remove('open');
}
// Nome mantido (era "instala fechar ao clicar no fundo") por compatibilidade
// com quem já importa esta função — o fechamento ao clicar no fundo
// escurecido foi REMOVIDO de propósito: um clique perdido um pixel fora da
// caixa do formulário (comum em modais grandes, tipo Condomínio/Empresa)
// fechava tudo sem aviso e derrubava o que já tinha sido digitado. Agora só
// fecha pelo "✕"/"Cancelar" (clique deliberado) ou depois de salvar.
export function installOverlayClickToClose() {
  // Botões "✕"/"Cancelar" de QUALQUER modal, ligados uma vez no boot — antes
  // isso morava na view de Produtos, o que deixava os modais das outras
  // views sem botão de fechar enquanto Produtos não fosse aberta.
  document.querySelectorAll('[data-close]').forEach((b) => {
    b.addEventListener('click', () => closeModal(b.dataset.close));
  });
}
