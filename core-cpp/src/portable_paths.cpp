#include "estoque/portable_paths.hpp"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#else
#include <unistd.h>
#endif

namespace estoque::portable_paths {

std::filesystem::path currentExecutablePath() {
#if defined(_WIN32)
  // Buffer crescente: nunca trava em MAX_PATH (260) — nomes de pasta longos
  // (comuns em "Área de Trabalho\Projetos\...") podem passar disso.
  std::vector<wchar_t> buf(260);
  for (;;) {
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (len == 0) return {};
    if (len < buf.size() - 1) return std::filesystem::path(buf.data());
    buf.resize(buf.size() * 2);
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);  // primeira chamada só descobre o tamanho
  std::vector<char> buf(size);
  if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
  return std::filesystem::canonical(std::filesystem::path(buf.data()));
#else
  // Linux (ambiente de desenvolvimento/CI) — equivalente ao GetModuleFileNameW.
  std::error_code ec;
  auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::path{} : p;
#endif
}

ResolveResult resolveDataDir(const std::filesystem::path& exeDir) {
  ResolveResult result;
  std::filesystem::path dataDir = exeDir / "dados";

  // Sempre chamado, nunca "checa-depois-cria" (evita TOCTOU); idempotente
  // se a pasta já existir.
  std::error_code ec;
  std::filesystem::create_directories(dataDir, ec);
  if (ec) {
    result.ok = false;
    // u8string(), não string(): no Windows, path::string() usa a codepage
    // ANSI local e corrompe acentos (ex.: em "Área de Trabalho"), o que
    // quebraria a validação UTF-8 estrita do rust::String na ponte cxx.
    result.error =
        "Não foi possível criar a pasta de dados em '" + dataDir.u8string() +
        "'. Verifique se o programa está numa pasta com permissão de escrita "
        "(evite rodar de dentro de 'Arquivos de Programas'; copie a pasta do aplicativo "
        "para a Área de Trabalho, Documentos, ou mantenha no pendrive) e tente novamente. "
        "Detalhe técnico: " +
        ec.message();
    return result;
  }

  // create_directories não garante sozinho que o resultado seja de fato uma
  // pasta utilizável em todo filesystem — confirmação explícita.
  bool isDir = std::filesystem::is_directory(dataDir, ec);
  if (ec || !isDir) {
    result.ok = false;
    result.error = "O caminho '" + dataDir.u8string() + "' existe mas não é uma pasta válida.";
    return result;
  }

  result.ok = true;
  result.dataDir = dataDir;
  return result;
}

ResolveResult resolveDataDir() { return resolveDataDir(currentExecutablePath().parent_path()); }

}  // namespace estoque::portable_paths
