#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "estoque/dates_engine.hpp"

using namespace estoque;

namespace {

constexpr const char* kHoje = "2026-08-16T12:00:00.000Z";

struct Cenario {
  Database db{":memory:"};

  Condominio condominio(const std::string& id = "cond1") {
    Condominio c;
    c.id = id;
    c.nome = "Edifício Central";
    c.endereco = "Rua das Flores, 100";
    c.sindico = "Maria";
    c.telefone = "11999990000";
    c.createdAt = kHoje;
    return c;
  }

  TipoServico tipoServico(int prazoDias, const std::string& id = "tipo1") {
    TipoServico t;
    t.id = id;
    t.nome = "Manutenção de elevador";
    t.prazoDias = prazoDias;
    t.cor = "#2f6fed";
    t.createdAt = kHoje;
    return t;
  }
};

}  // namespace

TEST_CASE("calcularStatus deriva vencimento e status a partir do prazo, sem gravar nada") {
  // Renovado há 10 dias, prazo de 40 dias -> vence em 30 dias -> "atencao".
  auto st = calcularStatus("2026-08-06T00:00:00.000Z", 40, kHoje);
  CHECK(st.diasRestantes == 30);
  CHECK(st.status == vencimento_status::kAtencao);

  // Prazo bem folgado -> "ok" (verde).
  auto ok = calcularStatus("2026-08-06T00:00:00.000Z", 90, kHoje);
  CHECK(ok.status == vencimento_status::kOk);
  CHECK(ok.diasRestantes == 80);

  // Vencimento já passado -> "vencido" (vermelho), dias negativos.
  auto vencido = calcularStatus("2026-01-01T00:00:00.000Z", 30, kHoje);
  CHECK(vencido.status == vencimento_status::kVencido);
  CHECK(vencido.diasRestantes < 0);
}

TEST_CASE("calcularStatus aceita data só (YYYY-MM-DD), como manda <input type=date>") {
  auto st = calcularStatus("2026-08-06", 40, "2026-08-16");
  CHECK(st.diasRestantes == 30);
  CHECK(st.status == vencimento_status::kAtencao);
}

TEST_CASE("CRUD de condomínio") {
  Cenario c;
  Condominio criado = createCondominio(c.db, c.condominio());
  CHECK(criado.nome == "Edifício Central");

  Condominio edit = criado;
  edit.nome = "Edifício Central II";
  edit.sindico = "João";
  Condominio atualizado = updateCondominio(c.db, edit);
  CHECK(atualizado.nome == "Edifício Central II");
  CHECK(atualizado.sindico == "João");

  CHECK(listCondominios(c.db).size() == 1);
  CHECK_THROWS_AS(createCondominio(c.db, Condominio{}), std::invalid_argument);

  Condominio inexistente;
  inexistente.id = "inexistente";
  inexistente.nome = "X";
  inexistente.createdAt = kHoje;
  CHECK_THROWS_AS(updateCondominio(c.db, inexistente), NotFoundError);
}

TEST_CASE("aviso prévio: agenda a saída, mas só efetiva ativo/código quando a data vence") {
  Cenario c;
  Condominio input = c.condominio();
  input.codigo = "1128";
  createCondominio(c.db, input);

  auto agendado = iniciarAvisoPrevio(c.db, "cond1", "2026-09-15", "9999");
  CHECK(agendado.avisoPrevioAte == "2026-09-15");
  CHECK(agendado.avisoPrevioNovoCodigo == "9999");
  CHECK(agendado.ativo == true);
  CHECK(agendado.codigo == "1128");  // nada muda ainda, só ficou agendado

  // Antes da data: listCondominios/aplicarAvisosPrevioVencidos não mexem em nada.
  aplicarAvisosPrevioVencidos(c.db, "2026-09-10T12:00:00.000Z");
  auto antes = *findCondominio(c.db, "cond1");
  CHECK(antes.ativo == true);
  CHECK(antes.codigo == "1128");
  CHECK(antes.avisoPrevioAte == "2026-09-15");

  // Na data (ou depois): aplica de vez e limpa a agenda.
  aplicarAvisosPrevioVencidos(c.db, "2026-09-16T12:00:00.000Z");
  auto depois = *findCondominio(c.db, "cond1");
  CHECK(depois.ativo == false);
  CHECK(depois.codigo == "9999");
  CHECK(depois.avisoPrevioAte.empty());
  CHECK(depois.avisoPrevioNovoCodigo.empty());
}

TEST_CASE("aviso prévio: exige condomínio ativo, data e novo código; pode ser cancelado antes de vencer") {
  Cenario c;
  createCondominio(c.db, c.condominio());

  CHECK_THROWS_AS(iniciarAvisoPrevio(c.db, "cond1", "", "9999"), std::invalid_argument);
  CHECK_THROWS_AS(iniciarAvisoPrevio(c.db, "cond1", "2026-09-15", ""), std::invalid_argument);
  CHECK_THROWS_AS(iniciarAvisoPrevio(c.db, "inexistente", "2026-09-15", "9999"), NotFoundError);

  iniciarAvisoPrevio(c.db, "cond1", "2026-09-15", "9999");
  auto cancelado = cancelarAvisoPrevio(c.db, "cond1");
  CHECK(cancelado.avisoPrevioAte.empty());
  CHECK(cancelado.avisoPrevioNovoCodigo.empty());
  CHECK(cancelado.ativo == true);  // nunca tinha mudado, cancelar não desfaz o que não aconteceu

  // Vencido o prazo original, mas como foi cancelado antes, não aplica nada.
  aplicarAvisosPrevioVencidos(c.db, "2026-12-01T00:00:00.000Z");
  CHECK(findCondominio(c.db, "cond1")->ativo == true);

  // Condomínio já inativo não pode receber um novo aviso prévio.
  auto inativo = c.condominio("cond2");
  inativo.ativo = false;
  createCondominio(c.db, inativo);
  CHECK_THROWS_AS(iniciarAvisoPrevio(c.db, "cond2", "2026-09-15", "9999"), std::invalid_argument);
}

TEST_CASE("condomínio: campos do cadastro estendido (CNPJ, código, endereço detalhado, localização)") {
  Cenario c;
  Condominio input = c.condominio();
  input.nomeFantasia = "Central";
  input.cnpj = "61.848.529/0001-54";
  input.codigo = "1128";
  input.cep = "01222-000";
  input.complemento = "Bloco A";
  input.bairro = "Vila Buarque";
  input.cidade = "São Paulo";
  input.estado = "SP";
  input.localizacao = localizacao_condominio::kCentro;
  auto criado = createCondominio(c.db, input);
  CHECK(criado.codigo == "1128");
  CHECK(criado.cidade == "São Paulo");
  CHECK(criado.localizacao == localizacao_condominio::kCentro);

  Condominio ruim = input;
  ruim.id = "x";
  ruim.localizacao = "nordeste";
  CHECK_THROWS_AS(createCondominio(c.db, ruim), std::invalid_argument);

  // Vazio é aceito — "não informado" é uma resposta legítima.
  Condominio semLocalizacao = input;
  semLocalizacao.id = "y";
  semLocalizacao.localizacao = "";
  CHECK_NOTHROW(createCondominio(c.db, semLocalizacao));
}

TEST_CASE("localizacaoCatalog: sete opções fixas, na ordem da tela") {
  const auto& cat = localizacaoCatalog();
  REQUIRE(cat.size() == 7);
  CHECK(cat[0].key == localizacao_condominio::kCentro);
  CHECK(cat[6].key == localizacao_condominio::kOutraCidade);
  CHECK(cat[6].label == "Outra cidade");
  CHECK(isKnownLocalizacao(localizacao_condominio::kSul));
  CHECK(isKnownLocalizacao(localizacao_condominio::kNoroeste));
  CHECK(localizacaoLabel(localizacao_condominio::kNoroeste) == "Noroeste");
  CHECK_FALSE(isKnownLocalizacao("nordeste"));
}

TEST_CASE("excluir tipo de serviço em uso é recusado; sem uso, funciona") {
  Cenario c;
  createCondominio(c.db, c.condominio());
  createTipoServico(c.db, c.tipoServico(30));

  ServicoCondominio input;
  input.id = "sc1";
  input.condominioId = "cond1";
  input.tipoServicoId = "tipo1";
  input.dataUltimaRenovacao = kHoje;
  input.createdAt = kHoje;
  createServicoCondominio(c.db, input);

  CHECK_THROWS_AS(deleteTipoServico(c.db, "tipo1"), std::invalid_argument);

  deleteServicoCondominio(c.db, "sc1");
  deleteTipoServico(c.db, "tipo1");  // agora não está mais em uso
  CHECK(listTiposServico(c.db).empty());
}

TEST_CASE("excluir um condomínio leva junto seus vínculos e o histórico de renovações") {
  Cenario c;
  createCondominio(c.db, c.condominio());
  createTipoServico(c.db, c.tipoServico(30));

  ServicoCondominio input;
  input.id = "sc1";
  input.condominioId = "cond1";
  input.tipoServicoId = "tipo1";
  input.dataUltimaRenovacao = "2026-01-01T00:00:00.000Z";
  input.createdAt = kHoje;
  createServicoCondominio(c.db, input);
  renovarServico(c.db, "sc1", "ren1", kHoje, "Empresa X", "", kHoje);

  CHECK(listServicosCondominio(c.db).size() == 1);
  CHECK(listRenovacoes(c.db, "").size() == 1);

  deleteCondominio(c.db, "cond1");

  CHECK(listServicosCondominio(c.db).empty());
  CHECK(listRenovacoes(c.db, "").empty());
}

TEST_CASE("renovarServico grava histórico, atualiza o vínculo e denormaliza o prazo aplicado") {
  Cenario c;
  createCondominio(c.db, c.condominio());
  createTipoServico(c.db, c.tipoServico(30));

  ServicoCondominio input;
  input.id = "sc1";
  input.condominioId = "cond1";
  input.tipoServicoId = "tipo1";
  input.dataUltimaRenovacao = "2026-01-01T00:00:00.000Z";
  input.empresaContratada = "Empresa Antiga";
  input.createdAt = kHoje;
  createServicoCondominio(c.db, input);

  ServicoCondominio atualizado =
      renovarServico(c.db, "sc1", "ren1", "2026-08-15T00:00:00.000Z", "Empresa Nova", "Trocou de prestador", kHoje);
  CHECK(atualizado.dataUltimaRenovacao == "2026-08-15T00:00:00.000Z");
  CHECK(atualizado.empresaContratada == "Empresa Nova");

  auto hist = listRenovacoes(c.db, "sc1");
  REQUIRE(hist.size() == 1);
  CHECK(hist[0].prazoDiasAplicado == 30);
  CHECK(hist[0].empresaContratada == "Empresa Nova");

  // Mudar o prazo padrão do tipo de serviço depois não deve alterar o
  // histórico já gravado.
  TipoServico tipoEditado = c.tipoServico(60);
  updateTipoServico(c.db, tipoEditado);
  CHECK(listRenovacoes(c.db, "sc1")[0].prazoDiasAplicado == 30);
}

TEST_CASE("vínculo recusa condomínio/tipo de serviço inexistente") {
  Cenario c;
  ServicoCondominio input;
  input.id = "sc1";
  input.condominioId = "cond-fantasma";
  input.tipoServicoId = "tipo-fantasma";
  input.dataUltimaRenovacao = kHoje;
  input.createdAt = kHoje;
  CHECK_THROWS_AS(createServicoCondominio(c.db, input), NotFoundError);
}
