import { api } from '../api.js';
import { escapeHtml } from '../format.js';
import { openModal } from './modal.js';

// Foto ampliada: um modal só, compartilhado por qualquer tela que tenha uma
// miniatura de produto (Produtos, Requisições) — usado tanto por um caminho
// no disco (sempre o caso de miniaturas de tabela) quanto por uma data URL em
// memória (o preview do cadastro, para um arquivo recém-escolhido e ainda não
// gravado).
export function openFotoAmpliada(origem, nome) {
  const corpo = document.getElementById('fotoAmpliadaCorpo');
  document.getElementById('fotoAmpliadaTitulo').textContent = nome || 'Foto do produto';
  corpo.innerHTML = '<span class="foto-placeholder">Carregando...</span>';
  openModal('modalFotoAmpliada');

  const mostrar = (dataUrl) => { corpo.innerHTML = `<img src="${dataUrl}" alt="${escapeHtml(nome || 'Foto do produto')}" />`; };
  const falhou = () => { corpo.innerHTML = '<span class="foto-placeholder">Não foi possível carregar a foto.</span>'; };

  if (!origem) { falhou(); return; }
  if (origem.startsWith('data:')) { mostrar(origem); return; }
  api.readProductImage(origem).then(mostrar).catch(falhou);
}
