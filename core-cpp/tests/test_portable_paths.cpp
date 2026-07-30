#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <sys/stat.h>

#include <cstdlib>
#include <fstream>

#include "doctest.h"
#include "estoque/portable_paths.hpp"

using namespace estoque::portable_paths;
namespace fs = std::filesystem;

namespace {

fs::path makeTempDir(const std::string& suffix) {
  fs::path base = fs::temp_directory_path() / ("estoque_test_" + suffix);
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return base;
}

}  // namespace

TEST_CASE("caso normal: cria dados/ ao lado do diretório do executável") {
  fs::path exeDir = makeTempDir("normal");
  auto r = resolveDataDir(exeDir);
  REQUIRE(r.ok);
  CHECK(r.dataDir == exeDir / "dados");
  CHECK(fs::is_directory(r.dataDir));
  fs::remove_all(exeDir);
}

TEST_CASE("idempotente: dados/ já existente não é um problema") {
  fs::path exeDir = makeTempDir("idempotente");
  fs::create_directories(exeDir / "dados");
  std::ofstream(exeDir / "dados" / "estoque.db") << "conteúdo existente";

  auto r = resolveDataDir(exeDir);
  REQUIRE(r.ok);
  CHECK(fs::exists(exeDir / "dados" / "estoque.db"));  // não apaga o que já estava lá
  fs::remove_all(exeDir);
}

TEST_CASE("diretório-pai somente-leitura: falha de forma clara, nunca crash ou fallback silencioso") {
  fs::path exeDir = makeTempDir("readonly");
  chmod(exeDir.c_str(), 0555);  // r-xr-xr-x — sem permissão de escrita

  auto r = resolveDataDir(exeDir);

  chmod(exeDir.c_str(), 0755);  // restaura para conseguir limpar depois
  fs::remove_all(exeDir);

  CHECK(!r.ok);
  CHECK(!r.error.empty());
  CHECK(r.error.find("permissão de escrita") != std::string::npos);
}

TEST_CASE("caminho com espaços e acentos funciona (caso real: 'Área de trabalho')") {
  fs::path baseDir = makeTempDir("acentos");
  fs::path exeDir = baseDir / "Área de Trabalho" / "Projetos Carlos FL";
  std::error_code ec;
  fs::create_directories(exeDir, ec);
  REQUIRE(!ec);

  auto r = resolveDataDir(exeDir);
  REQUIRE(r.ok);
  CHECK(fs::is_directory(r.dataDir));
  fs::remove_all(baseDir);
}
