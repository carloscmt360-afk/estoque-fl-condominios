// Estado da sessão no frontend — quem está logado e o que ele pode.
//
// Isto NÃO é controle de acesso: é conveniência de interface. Quem decide de
// verdade é o C++ (ver core-cpp/include/estoque/api.hpp), que confere a
// permissão em toda operação. O papel daqui é não oferecer ao usuário um botão
// que só vai devolver "sem permissão" depois do clique.

let session = null;

export function setSession(s) {
  session = s;
}

export function getSession() {
  return session;
}

export function currentUser() {
  return session ? session.user : null;
}

export function isLoggedIn() {
  return !!session;
}

export function isSuperadmin() {
  return !!(session && session.user && session.user.role === 'superadmin');
}

/* `action` é uma das quatro do CRUD: 'create' | 'read' | 'update' | 'delete'. */
export function can(feature, action) {
  if (!session) return false;
  if (isSuperadmin()) return true;
  const f = session.permissions && session.permissions[feature];
  return !!(f && f[action]);
}

/* Quem valida requisições: o superadmin sempre; os demais só se o grupo do
   setor conceder `requisicoes.update`. É a mesma regra do C++ (podeValidar em
   api.cpp) — se mudar lá, muda aqui. */
export function canValidateRequests() {
  return isSuperadmin() || can('requisicoes', 'update');
}

export function roleLabel(role) {
  return role === 'superadmin' ? 'Superadministrador' : 'Usuário';
}
