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
}
