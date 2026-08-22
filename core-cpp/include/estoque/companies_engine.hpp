#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/db.hpp"

// Fornecedores e Prestadores de Serviços.
//
// Duas coisas vivem aqui, e a ordem entre elas é o ponto central do módulo:
//
//   SETOR (catálogo fixo de 4) ──> Especialidade (o "nicho", cadastrável)
//                                        │
//                                        └──< Empresa (N especialidades)
//
// Os SETORES são fixos no código (Vendas, Contratos/Manutenções,
// Terceirizadas, Engenharia) — são as quatro divisões do negócio, não um
// cadastro que o usuário mexe no dia a dia. Mesmo critério do kFeatureCatalog
// de auth_engine: quando a lista é a própria estrutura do sistema, ela mora no
// código, e o banco só guarda o que aponta para ela.
//
// As ESPECIALIDADES são o que o usuário cadastra dentro de cada setor (em
// Vendas: "Produtos de limpeza", "Produtos para piscina"; em Engenharia:
// "AVCB", "Recarga de extintores"...). É essa lista que a tela de Cadastro
// oferece para marcar numa empresa.
//
// Uma empresa tem QUANTAS especialidades precisar, de setores diferentes
// inclusive — uma prestadora que faz dedetização e também vende produto de
// limpeza é uma empresa só, com duas marcações, não dois cadastros.
namespace estoque {

// ---- Setores (catálogo fixo) ----
namespace setores {
constexpr const char* kVendas = "vendas";
constexpr const char* kContratosManutencoes = "contratos_manutencoes";
constexpr const char* kTerceirizadas = "terceirizadas";
constexpr const char* kEngenharia = "engenharia";
}  // namespace setores

struct SetorInfo {
  std::string key;
  std::string label;
};
// Na ordem em que a tela mostra as quatro abas.
const std::vector<SetorInfo>& setorCatalog();
bool isKnownSetor(const std::string& key);
std::string setorLabel(const std::string& key);

// ---- Especialidades (os "nichos" de cada setor) ----
struct Especialidade {
  std::string id;
  std::string setor;  // setores::*
  std::string nome;
  std::string createdAt;
};

std::vector<Especialidade> listEspecialidades(Database& db);
std::optional<Especialidade> findEspecialidade(Database& db, const std::string& id);
// Recusa setor fora do catálogo, nome vazio e nome repetido DENTRO do mesmo
// setor ("Dedetização" pode existir em Engenharia e em Terceirizadas, mas não
// duas vezes na mesma aba).
Especialidade createEspecialidade(Database& db, const Especialidade& input);
Especialidade updateEspecialidade(Database& db, const Especialidade& input);
// Recusa excluir especialidade marcada em alguma empresa — apagar em silêncio
// a marcação de um fornecedor porque o catálogo mudou seria perda de dado.
void deleteEspecialidade(Database& db, const std::string& id);
// Quantas empresas marcam esta especialidade (a tela mostra antes de excluir).
int empresasComEspecialidade(Database& db, const std::string& especialidadeId);

// ---- Empresas ----
struct Empresa {
  std::string id;
  std::string nome;
  std::string nomeFantasia;
  std::string cnpj;
  std::string endereco;
  std::string numero;
  std::string complemento;
  std::string bairro;
  std::string cep;
  std::string cidade;
  std::string estado;   // UF
  std::string telefone;
  std::string emails;   // um ou mais, separados por vírgula
  std::string observacoes;
  // Empresa parceira. É a resposta de um Sim/Não na ficha, e é o que faz a
  // empresa aparecer em Gestão SOS > Parceiros. Fica na própria empresa (e não
  // numa lista separada de parceiros) para não existirem dois cadastros da
  // mesma empresa que podem divergir de telefone.
  bool parceira = false;
  std::string createdAt;
  std::vector<std::string> especialidadeIds;
};

// Só as parceiras, em ordem de nome — é a lista de Gestão SOS > Parceiros.
std::vector<Empresa> listParceiros(Database& db);

std::vector<Empresa> listEmpresas(Database& db);
std::optional<Empresa> findEmpresa(Database& db, const std::string& id);
// `cnpj` é opcional, mas quando informado não pode repetir (comparado só pelos
// caracteres alfanuméricos, então "12.345.678/0001-99" e "12345678000199" são
// o MESMO CNPJ). Não há conferência de dígito verificador de propósito: o
// CNPJ alfanumérico convive com o numérico, e recusar um cadastro legítimo é
// pior do que aceitar um dígito trocado.
Empresa createEmpresa(Database& db, const Empresa& input);
Empresa updateEmpresa(Database& db, const Empresa& input);
void deleteEmpresa(Database& db, const std::string& id);

// Só os caracteres alfanuméricos, em maiúsculas — é a forma canônica usada
// para detectar CNPJ repetido.
std::string canonCnpj(const std::string& cnpj);

}  // namespace estoque
