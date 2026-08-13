import { api, errorText } from '../api.js';

// Tela de login. Não guarda estado nenhum: quem manda é o backend (a sessão
// vive no objeto Api em C++), aqui só se coleta e-mail/senha e se avisa quem
// chamou quando entrou.

let wired = false;
let onSuccess = null;

export function initLogin(callback) {
  onSuccess = callback;
  if (wired) return;
  wired = true;
  document.getElementById('loginForm').addEventListener('submit', (e) => {
    e.preventDefault();
    entrar();
  });
}

export function showLogin(mensagem) {
  document.getElementById('appShell').style.display = 'none';
  document.getElementById('startupError').style.display = 'none';
  document.getElementById('loginScreen').style.display = 'flex';
  document.getElementById('loginSenha').value = '';
  mostrarErro(mensagem || '');
  // O e-mail continua preenchido entre tentativas (quase sempre está certo);
  // a senha, nunca.
  const email = document.getElementById('loginEmail');
  if (email.value) document.getElementById('loginSenha').focus();
  else email.focus();
}

export function hideLogin() {
  document.getElementById('loginScreen').style.display = 'none';
}

function mostrarErro(msg) {
  const box = document.getElementById('loginErro');
  box.textContent = msg;
  box.style.display = msg ? 'block' : 'none';
}

async function entrar() {
  const email = document.getElementById('loginEmail').value.trim();
  const senha = document.getElementById('loginSenha').value;
  if (!email || !senha) {
    mostrarErro('Informe e-mail e senha.');
    return;
  }

  const botao = document.getElementById('btnEntrar');
  botao.disabled = true;
  botao.textContent = 'Entrando...';
  try {
    const sessao = await api.login(email, senha);
    mostrarErro('');
    document.getElementById('loginSenha').value = '';
    if (onSuccess) await onSuccess(sessao);
  } catch (e) {
    // A mensagem do backend é sempre a mesma ("e-mail ou senha inválidos"),
    // de propósito: a tela de login não é lugar de descobrir quem existe.
    mostrarErro(errorText(e));
    document.getElementById('loginSenha').value = '';
    document.getElementById('loginSenha').focus();
  } finally {
    botao.disabled = false;
    botao.textContent = 'Entrar';
  }
}
