#include "estoque/models.hpp"

#include <stdexcept>

namespace estoque {

std::string movementTypeToString(MovementType t) {
  switch (t) {
    case MovementType::Entrada:
      return "entrada";
    case MovementType::Saida:
      return "saida";
    case MovementType::Ajuste:
      return "ajuste";
  }
  throw std::runtime_error("MovementType desconhecido");
}

MovementType movementTypeFromString(const std::string& s) {
  if (s == "entrada") return MovementType::Entrada;
  if (s == "saida") return MovementType::Saida;
  if (s == "ajuste") return MovementType::Ajuste;
  throw std::runtime_error("tipo de movimentação inválido: '" + s + "'");
}

}  // namespace estoque
