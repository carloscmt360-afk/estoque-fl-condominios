#pragma once
#include <optional>
#include <string>
#include <vector>

#include "estoque/db.hpp"

// Gestão de Datas: condomínios, tipos de serviço (com prazo de vencimento) e
// os vínculos entre os dois — cada vínculo é "este condomínio contratou este
// serviço, renovado pela última vez em tal data". O histórico de renovações
// é um razão à parte, no mesmo espírito de `movements` para produtos: nunca
// se sobrescreve uma renovação, sempre se acrescenta uma linha nova.
//
//   Condominio ─┐                    ┌─ TipoServico (prazoDias, cor)
//               ├─> ServicoCondominio┤
//               │   (dataUltimaRenovacao)
//               │        │
//               │        └──> Renovacao (histórico, uma linha por renovação)
//
// `dataVencimento` e o status (verde/amarelo/vermelho) NUNCA são gravados —
// são sempre calculados a partir de dataUltimaRenovacao + prazoDias e do
// "hoje" que o chamador passa (mesmo motivo de nowIso no resto do núcleo:
// mantém tudo determinístico e testável, sem ler o relógio do sistema aqui
// dentro). Ver vencimentoIso/statusDe.
namespace estoque {

// Região da cidade onde o condomínio fica — resposta única entre um catálogo
// fixo (mesmo critério de setorCatalog em companies_engine.hpp: é a própria
// estrutura do sistema, não um cadastro que o usuário mexe no dia a dia).
// Vazio = não informado; qualquer outro valor precisa estar no catálogo.
namespace localizacao_condominio {
constexpr const char* kCentro = "centro";
constexpr const char* kLeste = "leste";
constexpr const char* kOeste = "oeste";
constexpr const char* kNorte = "norte";
constexpr const char* kSul = "sul";
constexpr const char* kOutraCidade = "outra_cidade";
}  // namespace localizacao_condominio

struct LocalizacaoInfo {
  std::string key;
  std::string label;
};
// Na ordem em que a tela mostra as opções.
const std::vector<LocalizacaoInfo>& localizacaoCatalog();
bool isKnownLocalizacao(const std::string& key);
std::string localizacaoLabel(const std::string& key);

struct Condominio {
  std::string id;
  std::string nome;
  std::string nomeFantasia;
  std::string cnpj;
  std::string codigo;  // identificação/código do condomínio (referência externa)
  std::string endereco;
  std::string numero;
  std::string complemento;
  std::string bairro;
  std::string cidade;
  std::string estado;  // UF
  std::string cep;
  std::string localizacao;  // localizacao_condominio::* — vazio = não informado
  std::string sindico;
  std::string telefone;
  std::string email;  // do síndico/condomínio — destinatário padrão ao enviar orçamentos por e-mail
  std::string observacoes;
  bool ativo = true;  // Sim/Não, respondido no cadastro — condomínio inativo continua listado normalmente
  // Delta é a síndica deste condomínio? É essa marcação (gerida em Gestão SOS
  // > Delta Síndicos) que faz um serviço lançado aqui puxar automaticamente
  // um lançamento de comissão da Delta (ver listDeltaSindicos em
  // commissions_engine.cpp) — nunca é digitado serviço a serviço.
  bool deltaSindica = false;
  std::string createdAt;
};

struct TipoServico {
  std::string id;
  std::string nome;
  int prazoDias = 0;
  std::string cor;  // hex "#RRGGBB" — só identificação visual, não regra de negócio
  std::string createdAt;
};

struct ServicoCondominio {
  std::string id;
  std::string condominioId;
  std::string tipoServicoId;
  std::string dataUltimaRenovacao;
  std::string empresaContratada;
  std::string observacoes;
  std::string createdAt;
};

struct Renovacao {
  std::string id;
  std::string servicoCondominioId;
  std::string dataRenovacao;
  std::string empresaContratada;
  int prazoDiasAplicado = 0;
  std::string observacoes;
  std::string createdAt;
};

namespace vencimento_status {
constexpr const char* kOk = "ok";             // verde:  mais de 30 dias
constexpr const char* kAtencao = "atencao";    // amarelo: entre 0 e 30 dias
constexpr const char* kVencido = "vencido";    // vermelho: já passou do prazo
}  // namespace vencimento_status

struct StatusVencimento {
  std::string dataVencimento;  // ISO — dataUltimaRenovacao + prazoDias
  int diasRestantes = 0;       // negativo = já venceu
  std::string status;          // vencimento_status::*
};

// Aceita tanto data completa ("...T00:00:00.000Z") quanto só "YYYY-MM-DD"
// (o que `<input type="date">` manda) — normaliza para meia-noite UTC.
StatusVencimento calcularStatus(const std::string& dataUltimaRenovacao, int prazoDias,
                                const std::string& hojeIso);

// ---- Condomínios ----
std::vector<Condominio> listCondominios(Database& db);
std::optional<Condominio> findCondominio(Database& db, const std::string& id);
Condominio createCondominio(Database& db, const Condominio& input);
Condominio updateCondominio(Database& db, const Condominio& input);
// Cascata: os vínculos deste condomínio (e as renovações deles) são
// excluídos junto — sem vínculo órfão apontando para um condomínio que não
// existe mais.
void deleteCondominio(Database& db, const std::string& id);

// ---- Tipos de serviço ----
std::vector<TipoServico> listTiposServico(Database& db);
std::optional<TipoServico> findTipoServico(Database& db, const std::string& id);
TipoServico createTipoServico(Database& db, const TipoServico& input);
TipoServico updateTipoServico(Database& db, const TipoServico& input);
// Recusa excluir um tipo de serviço em uso por algum vínculo — diferente do
// condomínio, aqui não faz sentido apagar em cascata o vínculo de um cliente
// só porque o catálogo de serviços mudou.
void deleteTipoServico(Database& db, const std::string& id);

// ---- Vínculos (Condomínio × TipoServico) ----
std::vector<ServicoCondominio> listServicosCondominio(Database& db);
std::optional<ServicoCondominio> findServicoCondominio(Database& db, const std::string& id);
ServicoCondominio createServicoCondominio(Database& db, const ServicoCondominio& input);
ServicoCondominio updateServicoCondominio(Database& db, const ServicoCondominio& input);
void deleteServicoCondominio(Database& db, const std::string& id);

// ---- Renovações ----
// Grava uma linha nova em Renovacao E atualiza dataUltimaRenovacao do
// vínculo — as duas coisas na mesma transação, como applySaida faz com
// movement+product. `prazoDiasAplicado` é o prazo do tipo de serviço NO
// MOMENTO da renovação (denormalizado: se o prazo padrão do serviço mudar
// depois, o histórico não muda debaixo do usuário).
ServicoCondominio renovarServico(Database& db, const std::string& servicoCondominioId,
                                 const std::string& renovacaoId, const std::string& dataRenovacao,
                                 const std::string& empresaContratada, const std::string& observacoes,
                                 const std::string& createdAt);

// filtro vazio = todas as renovações (tela de Histórico); preenchido = só as
// de um vínculo (aba de histórico dentro do próprio vínculo).
std::vector<Renovacao> listRenovacoes(Database& db, const std::string& servicoCondominioFilter);

}  // namespace estoque
