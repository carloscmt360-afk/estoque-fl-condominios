#pragma once
#include <string>

// Modelo de domínio puro — sem std::string exótico, sem cxx, sem SQLite nas
// assinaturas. Espelha 1:1 o modelo que já existia em localStorage no app
// web (products/movements/departments), para a migração via backup JSON
// ser direta.
namespace estoque {

struct Product {
  std::string id;
  std::string name;
  std::string unit;
  double minStock = 0;
  std::string category;
  double qty = 0;
  double avgCost = 0;
  std::string createdAt;  // ISO 8601
  // Identificador de estoque no formato CMT###### — gerado automaticamente
  // (nunca pelo chamador) e permanente: uma vez atribuído, nunca muda nem é
  // reciclado. Ver inventory_engine::nextSku.
  std::string sku;
  // Caminhos RELATIVOS (à pasta onde o executável está, nunca absolutos —
  // ver bridge/src/images.rs) da foto principal (≤800×800) e da miniatura
  // (≤200×200), sempre .webp. "" = produto sem foto (opcional, sempre foi
  // e continua sendo). O core-cpp nunca lê o arquivo em si, só guarda a
  // referência — quem decodifica/gera a imagem é o módulo Rust; manter
  // essa fronteira é o que permite trocar o armazenamento por nuvem no
  // futuro sem mexer no núcleo de negócio.
  std::string imagePath;
  std::string thumbnailPath;
};

enum class MovementType { Entrada, Saida, Ajuste };

std::string movementTypeToString(MovementType t);
MovementType movementTypeFromString(const std::string& s);

struct Movement {
  std::string id;
  MovementType type;
  std::string productId;
  double qty = 0;
  double unitPrice = 0;
  std::string supplier;
  std::string nf;
  std::string departmentId;
  std::string recipient;    // nome do departamento, denormalizado no lançamento
  std::string encarregado;  // idem — histórico não muda se o depto for renomeado depois
  std::string requester;
  std::string obs;
  std::string date;       // data do lançamento (pode ser retroativa)
  // Só usado num ajuste: correção manual do custo médio, opcional. 0 = o
  // ajuste não mexe no custo (comportamento padrão, igual sempre foi).
  double newAvgCost = 0;
  double resultingQty = 0;
  double resultingAvgCost = 0;
  std::string createdAt;  // timestamp real de criação — desempate cronológico quando `date` empata
};

struct Department {
  std::string id;
  std::string name;
  std::string encarregado;
  // Teto de gasto mensal acordado com o setor, em R$. 0 = sem limite definido
  // (é o padrão, e é o que faz o app não inventar alerta para quem nunca
  // configurou um limite).
  double monthlyLimit = 0;
  std::string createdAt;
};

}  // namespace estoque
