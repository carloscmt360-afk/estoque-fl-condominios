export function openModal(id) {
  document.getElementById(id).classList.add('open');
}
export function closeModal(id) {
  document.getElementById(id).classList.remove('open');
}
export function installOverlayClickToClose() {
  document.querySelectorAll('.overlay').forEach((ov) => {
    ov.addEventListener('click', (e) => { if (e.target === ov) ov.classList.remove('open'); });
  });
  // Botões "✕"/"Cancelar" de QUALQUER modal, ligados uma vez no boot — antes
  // isso morava na view de Produtos, o que deixava os modais das outras
  // views sem botão de fechar enquanto Produtos não fosse aberta.
  document.querySelectorAll('[data-close]').forEach((b) => {
    b.addEventListener('click', () => closeModal(b.dataset.close));
  });
}
