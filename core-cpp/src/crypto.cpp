#include "estoque/crypto.hpp"

#include <cstring>
#include <random>
#include <stdexcept>

namespace estoque::crypto {

namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void sha256Compress(uint32_t state[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; ++i) {
    uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t temp1 = h + S1 + ch + kK[i] + w[i];
    uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = S0 + maj;
    h = g; g = f; f = e; e = d + temp1;
    d = c; c = b; b = a; a = temp1 + temp2;
  }
  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

constexpr size_t kBlockSize = 64;
constexpr size_t kDigestSize = 32;

}  // namespace

std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

  size_t full = data.size() / kBlockSize;
  for (size_t i = 0; i < full; ++i) sha256Compress(state, data.data() + i * kBlockSize);

  // Padding: 0x80, zeros, e o comprimento em bits (big-endian, 64 bits).
  uint8_t tail[128] = {0};
  size_t rest = data.size() - full * kBlockSize;
  if (rest > 0) std::memcpy(tail, data.data() + full * kBlockSize, rest);
  tail[rest] = 0x80;
  size_t tailBlocks = (rest + 1 + 8 > kBlockSize) ? 2 : 1;
  uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
  for (int i = 0; i < 8; ++i) {
    tail[tailBlocks * kBlockSize - 1 - i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xff);
  }
  for (size_t i = 0; i < tailBlocks; ++i) sha256Compress(state, tail + i * kBlockSize);

  std::vector<uint8_t> out(kDigestSize);
  for (int i = 0; i < 8; ++i) {
    out[i * 4] = static_cast<uint8_t>(state[i] >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(state[i]);
  }
  return out;
}

std::vector<uint8_t> hmacSha256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& message) {
  std::vector<uint8_t> k = key.size() > kBlockSize ? sha256(key) : key;
  k.resize(kBlockSize, 0);

  std::vector<uint8_t> inner(kBlockSize);
  std::vector<uint8_t> outer(kBlockSize);
  for (size_t i = 0; i < kBlockSize; ++i) {
    inner[i] = static_cast<uint8_t>(k[i] ^ 0x36);
    outer[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
  }
  inner.insert(inner.end(), message.begin(), message.end());
  std::vector<uint8_t> innerHash = sha256(inner);
  outer.insert(outer.end(), innerHash.begin(), innerHash.end());
  return sha256(outer);
}

std::vector<uint8_t> pbkdf2Sha256(const std::string& password, const std::vector<uint8_t>& salt,
                                  int iterations, size_t dkLen) {
  if (iterations <= 0) throw std::invalid_argument("PBKDF2: número de rodadas deve ser positivo");
  std::vector<uint8_t> pass(password.begin(), password.end());
  std::vector<uint8_t> out;
  out.reserve(dkLen);

  uint32_t block = 1;
  while (out.size() < dkLen) {
    std::vector<uint8_t> input = salt;
    input.push_back(static_cast<uint8_t>(block >> 24));
    input.push_back(static_cast<uint8_t>(block >> 16));
    input.push_back(static_cast<uint8_t>(block >> 8));
    input.push_back(static_cast<uint8_t>(block));

    std::vector<uint8_t> u = hmacSha256(pass, input);
    std::vector<uint8_t> acc = u;
    for (int i = 1; i < iterations; ++i) {
      u = hmacSha256(pass, u);
      for (size_t j = 0; j < acc.size(); ++j) acc[j] ^= u[j];
    }
    out.insert(out.end(), acc.begin(), acc.end());
    ++block;
  }
  out.resize(dkLen);
  return out;
}

std::string toHex(const std::vector<uint8_t>& bytes) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (uint8_t b : bytes) {
    out.push_back(digits[b >> 4]);
    out.push_back(digits[b & 0x0f]);
  }
  return out;
}

std::vector<uint8_t> fromHex(const std::string& hex) {
  if (hex.size() % 2 != 0) throw std::invalid_argument("hex com número ímpar de dígitos");
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("dígito hex inválido");
  };
  std::vector<uint8_t> out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    out.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
  }
  return out;
}

std::string randomHex(size_t byteCount) {
  std::random_device rd;
  std::vector<uint8_t> bytes(byteCount);
  for (size_t i = 0; i < byteCount; ++i) bytes[i] = static_cast<uint8_t>(rd() & 0xff);
  return toHex(bytes);
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
  // Comprimentos diferentes já são "não confere", mas ainda assim varre-se a
  // string inteira para o tempo não depender de onde está a diferença.
  unsigned char diff = a.size() == b.size() ? 0 : 1;
  size_t n = a.size() < b.size() ? a.size() : b.size();
  for (size_t i = 0; i < n; ++i) {
    diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  }
  return diff == 0;
}

}  // namespace estoque::crypto
