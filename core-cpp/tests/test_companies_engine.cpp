#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/companies_engine.hpp"

using namespace estoque;

namespace {

constexpr const char* kNow = "2026-08-16T12:00:00.000Z";

struct Cenario {
  Database db{":memory:"};

  Especialidade esp(const std::string& id, const std::string& setor, const std::string& nome) {
    Especialidade e;
    e.id = id;
    e.setor = setor;
    e.nome = nome;
    e.createdAt = kNow;
    return e;
  }

  Empresa empresa(const std::string& id, const std::string& nome,
                  const std::vector<std::string>& especialidades = {}) {
    Empresa e;
    e.id = id;
    e.nome = nome;
    e.createdAt = kNow;
    e.especialidadeIds = especialidades;
    return e;
  }
};

}  // namespace

TEST_CASE("os quatro setores são catálogo fixo, na ordem da tela") {
  const auto& cat = setorCatalog();
  REQUIRE(cat.size() == 4);
  CHECK(cat[0].key == setores::kVendas);
  CHECK(cat[1].key == setores::kContratosManutencoes);
  CHECK(cat[2].key == setores::kTerceirizadas);
  CHECK(cat[3].key == setores::kEngenharia);
  CHECK(cat[1].label == "Contratos / Manutenções");
  CHECK(isKnownSetor(setores::kEngenharia));
  CHECK_FALSE(isKnownSetor("financeiro"));
}

TEST_CASE("especialidade: CRUD e recusa de setor inválido ou nome vazio") {
  Cenario c;
  auto criada = createEspecialidade(c.db, c.esp("e1", setores::kEngenharia, "AVCB"));
  CHECK(criada.nome == "AVCB");
  CHECK(criada.setor == setores::kEngenharia);

  auto editada = updateEspecialidade(c.db, c.esp("e1", setores::kEngenharia, "AVCB — renovação"));
  CHECK(editada.nome == "AVCB — renovação");

  CHECK_THROWS_AS(createEspecialidade(c.db, c.esp("x", "financeiro", "Algo")), std::invalid_argument);
  CHECK_THROWS_AS(createEspecialidade(c.db, c.esp("x", setores::kVendas, "   ")), std::invalid_argument);
  CHECK_THROWS_AS(updateEspecialidade(c.db, c.esp("fantasma", setores::kVendas, "X")), NotFoundError);
}

TEST_CASE("o mesmo nicho pode existir em dois setores, mas não duas vezes no mesmo") {
  Cenario c;
  createEspecialidade(c.db, c.esp("e1", setores::kEngenharia, "Dedetização"));
  // outro setor: permitido
  CHECK_NOTHROW(createEspecialidade(c.db, c.esp("e2", setores::kTerceirizadas, "Dedetização")));
  // mesmo setor: recusado por caixa, por acento e por espaço sobrando — as
  // três formas em que a mesma especialidade viraria duas linhas no Catálogo
  CHECK_THROWS_AS(createEspecialidade(c.db, c.esp("e3", setores::kEngenharia, "DEDETIZAÇÃO")),
                  std::invalid_argument);
  CHECK_THROWS_AS(createEspecialidade(c.db, c.esp("e4", setores::kEngenharia, "dedetizacao")),
                  std::invalid_argument);
  CHECK_THROWS_AS(createEspecialidade(c.db, c.esp("e5", setores::kEngenharia, "  Dedetização  ")),
                  std::invalid_argument);
  CHECK(listEspecialidades(c.db).size() == 2);

  // e um nome de duas palavras não colide por espaço duplicado no meio
  createEspecialidade(c.db, c.esp("e6", setores::kVendas, "Limpeza de caixa"));
  CHECK_THROWS_AS(createEspecialidade(c.db, c.esp("e7", setores::kVendas, "Limpeza  de  caixa")),
                  std::invalid_argument);
  // mas nichos de verdade diferentes continuam passando
  CHECK_NOTHROW(createEspecialidade(c.db, c.esp("e8", setores::kVendas, "Limpeza de fachada")));
}

TEST_CASE("empresa marca várias especialidades, de setores diferentes") {
  Cenario c;
  createEspecialidade(c.db, c.esp("e1", setores::kVendas, "Produtos de limpeza"));
  createEspecialidade(c.db, c.esp("e2", setores::kVendas, "Produtos para piscina"));
  createEspecialidade(c.db, c.esp("e3", setores::kEngenharia, "Impermeabilização"));

  auto criada = createEmpresa(c.db, c.empresa("emp1", "Alfa Ltda", {"e1", "e2", "e3"}));
  CHECK(criada.especialidadeIds.size() == 3);

  // o mesmo id repetido na seleção não vira duas linhas
  auto rep = createEmpresa(c.db, c.empresa("emp2", "Beta Ltda", {"e1", "e1", "e1"}));
  CHECK(rep.especialidadeIds.size() == 1);

  // especialidade inexistente é recusada (marcação órfã sumiria sem avisar)
  CHECK_THROWS_AS(createEmpresa(c.db, c.empresa("emp3", "Gama", {"fantasma"})), NotFoundError);
}

TEST_CASE("editar empresa substitui a seleção de especialidades") {
  Cenario c;
  createEspecialidade(c.db, c.esp("e1", setores::kVendas, "Acessórios para condomínios"));
  createEspecialidade(c.db, c.esp("e2", setores::kEngenharia, "Restauração de fachada"));
  createEmpresa(c.db, c.empresa("emp1", "Alfa", {"e1", "e2"}));

  auto patch = c.empresa("emp1", "Alfa", {"e2"});
  auto editada = updateEmpresa(c.db, patch);
  REQUIRE(editada.especialidadeIds.size() == 1);
  CHECK(editada.especialidadeIds[0] == "e2");
  CHECK(empresasComEspecialidade(c.db, "e1") == 0);
  CHECK(empresasComEspecialidade(c.db, "e2") == 1);
}

TEST_CASE("excluir especialidade em uso é recusado; sem uso, funciona") {
  Cenario c;
  createEspecialidade(c.db, c.esp("e1", setores::kEngenharia, "Limpeza de caixa"));
  createEmpresa(c.db, c.empresa("emp1", "Alfa", {"e1"}));

  CHECK_THROWS_AS(deleteEspecialidade(c.db, "e1"), std::invalid_argument);

  deleteEmpresa(c.db, "emp1");
  CHECK_NOTHROW(deleteEspecialidade(c.db, "e1"));
  CHECK(listEspecialidades(c.db).empty());
}

TEST_CASE("excluir empresa leva as marcações, mas preserva o catálogo") {
  Cenario c;
  createEspecialidade(c.db, c.esp("e1", setores::kVendas, "Produtos de limpeza"));
  createEmpresa(c.db, c.empresa("emp1", "Alfa", {"e1"}));

  deleteEmpresa(c.db, "emp1");
  CHECK(listEmpresas(c.db).empty());
  CHECK(empresasComEspecialidade(c.db, "e1") == 0);
  CHECK(listEspecialidades(c.db).size() == 1);  // o nicho continua no catálogo
}

TEST_CASE("CNPJ: opcional, normalizado, e não repete") {
  Cenario c;
  Empresa a = c.empresa("emp1", "Alfa");
  a.cnpj = "12.345.678/0001-99";
  createEmpresa(c.db, a);

  // mesmo CNPJ com pontuação diferente é o MESMO CNPJ
  Empresa b = c.empresa("emp2", "Beta");
  b.cnpj = "12345678000199";
  CHECK_THROWS_AS(createEmpresa(c.db, b), std::invalid_argument);

  // duas empresas SEM CNPJ convivem (o campo é opcional)
  CHECK_NOTHROW(createEmpresa(c.db, c.empresa("emp3", "Gama")));
  CHECK_NOTHROW(createEmpresa(c.db, c.empresa("emp4", "Delta")));

  // aceita CNPJ alfanumérico (formato novo), sem conferir dígito verificador
  Empresa e = c.empresa("emp5", "Epsilon");
  e.cnpj = "12ABC34501DE35";
  CHECK_NOTHROW(createEmpresa(c.db, e));

  // e o texto digitado é preservado para exibir
  CHECK(findEmpresa(c.db, "emp1")->cnpj == "12.345.678/0001-99");
}

TEST_CASE("canonCnpj mantém só alfanuméricos, em maiúsculas") {
  CHECK(canonCnpj("12.345.678/0001-99") == "12345678000199");
  CHECK(canonCnpj("12abc34501de35") == "12ABC34501DE35");
  CHECK(canonCnpj("   ") == "");
  CHECK(canonCnpj("") == "");
}

TEST_CASE("parceira: nasce Não, e só as parceiras entram em listParceiros") {
  Cenario c;
  createEspecialidade(c.db, c.esp("e1", setores::kEngenharia, "Recarga de extintores"));

  // Quem nunca respondeu a pergunta não é parceira — é o padrão da ficha.
  auto padrao = createEmpresa(c.db, c.empresa("emp1", "Alfa"));
  CHECK_FALSE(padrao.parceira);

  Empresa b = c.empresa("emp2", "Beta", {"e1"});
  b.parceira = true;
  b.telefone = "(11) 4000-1000";
  auto parceira = createEmpresa(c.db, b);
  CHECK(parceira.parceira);

  auto lista = listParceiros(c.db);
  REQUIRE(lista.size() == 1);
  CHECK(lista[0].nome == "Beta");
  // A lista de parceiros serve para ligar: o contato e as especialidades vêm
  // junto, sem uma segunda consulta.
  CHECK(lista[0].telefone == "(11) 4000-1000");
  CHECK(lista[0].especialidadeIds.size() == 1);
}

TEST_CASE("marcar e desmarcar parceira reflete na lista de parceiros") {
  Cenario c;
  createEmpresa(c.db, c.empresa("emp1", "Alfa"));
  CHECK(listParceiros(c.db).empty());

  Empresa vira = c.empresa("emp1", "Alfa");
  vira.parceira = true;
  CHECK(updateEmpresa(c.db, vira).parceira);
  CHECK(listParceiros(c.db).size() == 1);

  // desmarcar tem que sair da lista — senão o Sim seria irreversível
  Empresa volta = c.empresa("emp1", "Alfa");
  volta.parceira = false;
  CHECK_FALSE(updateEmpresa(c.db, volta).parceira);
  CHECK(listParceiros(c.db).empty());
  // e a empresa continua cadastrada: deixar de ser parceira não é excluir
  CHECK(listEmpresas(c.db).size() == 1);
}

TEST_CASE("empresa exige nome e a UF é gravada em maiúsculas") {
  Cenario c;
  CHECK_THROWS_AS(createEmpresa(c.db, c.empresa("x", "  ")), std::invalid_argument);

  Empresa e = c.empresa("emp1", "Alfa");
  e.estado = "sp";
  e.cidade = "São Paulo";
  e.telefone = "(11) 99999-0000";
  e.emails = "contato@alfa.com.br, vendas@alfa.com.br";
  auto criada = createEmpresa(c.db, e);
  CHECK(criada.estado == "SP");
  CHECK(criada.cidade == "São Paulo");
  CHECK(criada.emails == "contato@alfa.com.br, vendas@alfa.com.br");
}
