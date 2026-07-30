let timer = null;
export function toast(msg, type) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.className = type || '';
  t.style.display = 'block';
  clearTimeout(timer);
  timer = setTimeout(() => { t.style.display = 'none'; }, 3200);
}
