// Comportamento compartilhado das "planilhas" do sistema (as listas de
// cadastro com tabela + Editar/Excluir): clique numa linha pra destacar ela,
// seleção única (só uma linha marcada por vez, mesmo espírito de rádio, não
// checkbox). Ignora clique em botão/link/campo da própria linha — não pode
// "roubar" o clique de Editar/Excluir nem de um input inline.
//
// Chamada uma vez na wiring de cada view (idempotente via dataset.rowSelectWired)
// — o listener fica no <tbody>, que a view nunca substitui, só o innerHTML
// dele a cada render(), então sobrevive normalmente entre re-renderizações.
export function enableRowSelection(tbody) {
  if (!tbody || tbody.dataset.rowSelectWired) return;
  tbody.dataset.rowSelectWired = '1';
  tbody.addEventListener('click', (e) => {
    if (e.target.closest('button, a, input, select, textarea, label')) return;
    const tr = e.target.closest('tr');
    if (!tr || tr.parentElement !== tbody) return;
    const jaSelecionada = tr.classList.contains('row-selected');
    tbody.querySelectorAll('tr.row-selected').forEach((x) => x.classList.remove('row-selected'));
    if (!jaSelecionada) tr.classList.add('row-selected');
  });
}
