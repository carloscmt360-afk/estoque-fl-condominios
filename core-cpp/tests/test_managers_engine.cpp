#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/managers_engine.hpp"

#include "estoque/dates_engine.hpp"

using namespace estoque;

namespace {

constexpr const char* kNow = "2026-08-16T12:00:00.000Z";

struct Cenario {
  Database db{":memory:"};

  Condominio condominio(const std::string& id) {
    Condominio c;
    c.id = id;
    c.nome = "Condomínio " + id;
    c.createdAt = kNow;
    return c;
  }

  Gerente gerente(const std::string& id, const std::vector<std::string>& carteira = {}) {
    Gerente g;
    g.id = id;
    g.nome = "Gerente " + id;
    g.telefone = "11999990000";
    g.email = id + "@fl.com.br";
    g.createdAt = kNow;
    g.condominioIds = carteira;
    return g;
  }
};

}  // namespace

TEST_CASE("gerente: CRUD básico e recusa de nome vazio") {
  Cenario c;
  auto criado = createGerente(c.db, c.gerente("g1"));
  CHECK(criado.nome == "Gerente g1");
  CHECK(criado.condominioIds.empty());

  Gerente edit = c.gerente("g1");
  edit.nome = "Novo Nome";
  auto editado = updateGerente(c.db, edit);
  CHECK(editado.nome == "Novo Nome");

  Gerente semNome = c.gerente("x");
  semNome.nome = "   ";
  CHECK_THROWS_AS(createGerente(c.db, semNome), std::invalid_argument);
  CHECK_THROWS_AS(updateGerente(c.db, c.gerente("fantasma")), NotFoundError);
  CHECK_THROWS_AS(deleteGerente(c.db, "fantasma"), NotFoundError);
}

TEST_CASE("gerente: numero é sequencial e nunca reciclado; chavePix é gravada") {
  Cenario c;
  auto g1 = createGerente(c.db, c.gerente("g1"));
  auto g2 = createGerente(c.db, c.gerente("g2"));
  CHECK(g1.numero == 1);
  CHECK(g2.numero == 2);

  deleteGerente(c.db, "g2");
  auto g3 = createGerente(c.db, c.gerente("g3"));
  CHECK(g3.numero == 3);  // não reaproveita o 2 excluído

  Gerente edit = c.gerente("g1");
  edit.chavePix = "11999990000";
  auto editado = updateGerente(c.db, edit);
  CHECK(editado.chavePix == "11999990000");
  CHECK(editado.numero == 1);  // numero não muda na edição
}

TEST_CASE("carteira: marcar condomínios na criação e regravar por inteiro na edição") {
  Cenario c;
  createCondominio(c.db, c.condominio("cond1"));
  createCondominio(c.db, c.condominio("cond2"));
  createCondominio(c.db, c.condominio("cond3"));

  auto g = createGerente(c.db, c.gerente("g1", {"cond1", "cond2"}));
  REQUIRE(g.condominioIds.size() == 2);

  // Regrava: solta cond2, mantém cond1, acrescenta cond3 — e um id repetido
  // na entrada não pode virar marcação duplicada.
  auto editado = updateGerente(c.db, c.gerente("g1", {"cond1", "cond3", "cond3"}));
  REQUIRE(editado.condominioIds.size() == 2);
  CHECK(editado.condominioIds[0] == "cond1");
  CHECK(editado.condominioIds[1] == "cond3");

  CHECK_THROWS_AS(createGerente(c.db, c.gerente("g2", {"fantasma"})), NotFoundError);
}

TEST_CASE("excluir gerente solta a carteira; excluir condomínio some da carteira dele") {
  Cenario c;
  createCondominio(c.db, c.condominio("cond1"));
  createGerente(c.db, c.gerente("g1", {"cond1"}));

  deleteCondominio(c.db, "cond1");
  auto g = *findGerente(c.db, "g1");
  CHECK(g.condominioIds.empty());  // ON DELETE CASCADE do lado do condomínio

  deleteGerente(c.db, "g1");
  CHECK_FALSE(findGerente(c.db, "g1").has_value());
}

TEST_CASE("listGerentes vem ordenado por nome, com a carteira resolvida") {
  Cenario c;
  createCondominio(c.db, c.condominio("cond1"));
  createGerente(c.db, c.gerente("gz"));
  createGerente(c.db, c.gerente("ga", {"cond1"}));

  auto lista = listGerentes(c.db);
  REQUIRE(lista.size() == 2);
  CHECK(lista[0].id == "ga");  // "Gerente ga" < "Gerente gz"
  CHECK(lista[0].condominioIds == std::vector<std::string>{"cond1"});
}
