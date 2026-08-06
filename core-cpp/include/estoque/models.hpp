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
