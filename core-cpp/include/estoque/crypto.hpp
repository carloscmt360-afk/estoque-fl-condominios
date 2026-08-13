#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Primitivas criptográficas mínimas para guardar senha de usuário — SHA-256,
// HMAC-SHA256 e PBKDF2-HMAC-SHA256, implementados aqui em C++ puro.
//
// Por que não uma biblioteca: o projeto inteiro é deliberadamente sem
// dependências externas no núcleo (o único terceiro é o amálgama do SQLite e
// o nlohmann/json, ambos header/arquivo único versionados em third_party/).
// Puxar OpenSSL para o build MSVC do Tauri custaria muito mais do que as ~150
// linhas abaixo, que são RFC 6234/RFC 2898 diretas e estão cobertas por
// vetores oficiais em tests/test_crypto.cpp.
//
// O que NÃO se faz aqui, de propósito: guardar senha em texto puro, guardar
// só um SHA-256 "cru" (rápido demais para força bruta) ou reaproveitar salt
// entre usuários. Cada usuário tem salt próprio de 128 bits e a derivação usa
// PBKDF2 com kDefaultIterations rodadas.
namespace estoque::crypto {

// Número de rodadas do PBKDF2 usado ao gravar uma senha nova. É guardado
// junto do hash (users.password_iterations), então aumentar este valor no
// futuro não invalida as senhas já existentes: elas continuam sendo
// verificadas com o número de rodadas com que foram criadas.
constexpr int kDefaultIterations = 100000;

std::vector<uint8_t> sha256(const std::vector<uint8_t>& data);
std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& message);

// Deriva `dkLen` bytes da senha. `iterations` deve ser > 0.
std::vector<uint8_t> pbkdf2Sha256(const std::string& password, const std::vector<uint8_t>& salt,
                                  int iterations, size_t dkLen);

std::string toHex(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> fromHex(const std::string& hex);

// Bytes aleatórios do CSPRNG do sistema (std::random_device: /dev/urandom no
// Linux, rand_s no MSVC). É a ÚNICA fonte de não-determinismo do core-cpp, e
// existe porque um salt previsível anularia o PBKDF2 — id e timestamp
// continuam vindo do chamador, como no resto do núcleo.
std::string randomHex(size_t byteCount);

// Comparação em tempo constante — não sai mais cedo no primeiro byte
// diferente, para não vazar por tempo quanto do hash foi acertado.
bool constantTimeEquals(const std::string& a, const std::string& b);

}  // namespace estoque::crypto
