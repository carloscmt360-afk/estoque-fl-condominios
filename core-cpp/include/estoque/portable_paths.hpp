#pragma once
#include <filesystem>
#include <string>

// Resolução do armazenamento portátil: o banco SEMPRE fica numa subpasta
// "dados" ao lado de onde o executável estiver rodando — nunca em AppData,
// nunca em outro lugar "esperto". Isso é o que faz o app funcionar
// identicamente rodando de um pendrive em qualquer PC.
namespace estoque::portable_paths {

struct ResolveResult {
  bool ok = false;
  std::filesystem::path dataDir;  // válido só se ok==true
  std::string error;               // mensagem acionável para a UI, só se ok==false
};

// Caminho absoluto do executável em execução (GetModuleFileNameW no Windows,
// /proc/self/exe no Linux). Não trata atalhos (.lnk) — o SO já resolve o
// atalho para o caminho real antes de criar o processo.
std::filesystem::path currentExecutablePath();

// Núcleo testável: recebe o diretório do executável como parâmetro
// (em vez de descobri-lo sozinho), o que permite testar aqui mesmo, sem
// depender de onde o teste é executado — inclusive o caso de diretório
// somente-leitura. Sempre chama create_directories (idempotente), nunca
// checa-depois-cria. Nunca cai num local alternativo silencioso em caso de
// falha — sinaliza erro para o chamador decidir o que fazer (mostrar modal
// com "Tentar novamente", nunca crash).
ResolveResult resolveDataDir(const std::filesystem::path& exeDir);

// Conveniência para produção: resolve o diretório do executável sozinho e
// delega para resolveDataDir(exeDir). Testes usam a versão acima, injetando
// o diretório.
ResolveResult resolveDataDir();

}  // namespace estoque::portable_paths
