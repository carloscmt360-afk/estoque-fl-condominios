#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/suprimentos_engine.hpp"

using namespace estoque;

namespace {

constexpr const char* kNow = "2026-08-16T12:00:00.000Z";

struct Cenario {
  Database db{":memory:"};

  Suprimento suprimento(const std::string& id, const std::string& categoria = suprimento_categoria::kGestor) {
    Suprimento s;
    s.id = id;
    s.nome = "Pessoa " + id;
    s.categoria = categoria;
    s.telefone = "11999990000";
    s.email = id + "@fl.com.br";
    s.chavePix = "chave-" + id;
    s.createdAt = kNow;
    return s;
  }
};

}  // namespace

TEST_CASE("suprimento: CRUD básico, numero sequencial e categoria válida") {
  Cenario c;
  auto s1 = createSuprimento(c.db, c.suprimento("s1", suprimento_categoria::kAssistente));
  auto s2 = createSuprimento(c.db, c.suprimento("s2", suprimento_categoria::kVistoriadorPredial));
  CHECK(s1.numero == 1);
  CHECK(s2.numero == 2);
  CHECK(s1.categoria == suprimento_categoria::kAssistente);
  CHECK(s1.chavePix == "chave-s1");

  Suprimento edit = c.suprimento("s1");
  edit.nome = "Novo Nome";
  edit.categoria = suprimento_categoria::kAuxiliar;
  auto editado = updateSuprimento(c.db, edit);
  CHECK(editado.nome == "Novo Nome");
  CHECK(editado.categoria == suprimento_categoria::kAuxiliar);
  CHECK(editado.numero == 1);  // numero não muda na edição

  deleteSuprimento(c.db, "s1");
  CHECK_FALSE(findSuprimento(c.db, "s1").has_value());
  CHECK(listSuprimentos(c.db).size() == 1);
}

TEST_CASE("suprimento: recusa nome vazio e categoria fora do catálogo") {
  Cenario c;
  auto semNome = c.suprimento("x");
  semNome.nome = "   ";
  CHECK_THROWS_AS(createSuprimento(c.db, semNome), std::invalid_argument);

  auto categoriaRuim = c.suprimento("y");
  categoriaRuim.categoria = "gerente";  // não é uma categoria de Suprimento
  CHECK_THROWS_AS(createSuprimento(c.db, categoriaRuim), std::invalid_argument);

  CHECK_THROWS_AS(updateSuprimento(c.db, c.suprimento("fantasma")), NotFoundError);
  CHECK_THROWS_AS(deleteSuprimento(c.db, "fantasma"), NotFoundError);
}

TEST_CASE("categoriaSuprimentoCatalog: as quatro categorias, na ordem, com rótulos") {
  auto cat = categoriaSuprimentoCatalog();
  REQUIRE(cat.size() == 4);
  CHECK(cat[0].key == suprimento_categoria::kGestor);
  CHECK(cat[1].key == suprimento_categoria::kAssistente);
  CHECK(cat[2].key == suprimento_categoria::kAuxiliar);
  CHECK(cat[3].key == suprimento_categoria::kVistoriadorPredial);
  CHECK(categoriaSuprimentoLabel(suprimento_categoria::kVistoriadorPredial) == "Vistoriador predial");
  CHECK(isKnownCategoriaSuprimento(suprimento_categoria::kGestor));
  CHECK_FALSE(isKnownCategoriaSuprimento("fantasma"));
}
