#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string>

#include "doctest.h"
#include "estoque/crypto.hpp"

using namespace estoque::crypto;

namespace {

std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

}  // namespace

// Os vetores abaixo são os oficiais (FIPS 180-4 para SHA-256, RFC 4231 para
// HMAC, RFC 8018/vetores públicos para PBKDF2-HMAC-SHA256). Eles são a razão
// de existir deste arquivo: uma implementação própria de criptografia sem
// vetores conhecidos é indistinguível de uma implementação errada.
TEST_CASE("SHA-256 bate com os vetores do FIPS 180-4") {
  CHECK(toHex(sha256(bytes("abc"))) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(toHex(sha256(bytes(""))) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  // 56 bytes: exercita o caminho de padding que precisa de um bloco EXTRA
  CHECK(toHex(sha256(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  // exatamente 64 bytes: bloco cheio, padding inteiro num bloco novo
  CHECK(toHex(sha256(bytes(std::string(64, 'a')))) ==
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

TEST_CASE("HMAC-SHA256 bate com os vetores da RFC 4231") {
  CHECK(toHex(hmacSha256(std::vector<uint8_t>(20, 0x0b), bytes("Hi There"))) ==
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
  CHECK(toHex(hmacSha256(bytes("Jefe"), bytes("what do ya want for nothing?"))) ==
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
  // chave MAIOR que o bloco (131 bytes): tem que ser reduzida por SHA-256 antes
  CHECK(toHex(hmacSha256(std::vector<uint8_t>(131, 0xaa),
                         bytes("Test Using Larger Than Block-Size Key - Hash Key First"))) ==
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST_CASE("PBKDF2-HMAC-SHA256 bate com os vetores públicos") {
  CHECK(toHex(pbkdf2Sha256("password", bytes("salt"), 1, 32)) ==
        "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
  CHECK(toHex(pbkdf2Sha256("password", bytes("salt"), 2, 32)) ==
        "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
  CHECK(toHex(pbkdf2Sha256("password", bytes("salt"), 4096, 32)) ==
        "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
  // dkLen > 32 obriga a derivar mais de um bloco (T1|T2), caminho que o uso
  // do app não exercita mas que uma mudança futura pode passar a exercitar
  CHECK(toHex(pbkdf2Sha256("passwordPASSWORDpassword",
                           bytes("saltSALTsaltSALTsaltSALTsaltSALTsalt"), 4096, 40)) ==
        "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9");
}

TEST_CASE("hex vai e volta") {
  auto original = bytes("qualquer coisa \x01\x02\xff");
  CHECK(fromHex(toHex(original)) == original);
  CHECK_THROWS(fromHex("abc"));   // número ímpar de dígitos
  CHECK_THROWS(fromHex("zz"));    // dígito inválido
}

TEST_CASE("randomHex devolve o tamanho pedido e não se repete") {
  std::string a = randomHex(16);
  std::string b = randomHex(16);
  CHECK(a.size() == 32);
  CHECK(b.size() == 32);
  CHECK(a != b);  // 128 bits: colidir aqui significa gerador quebrado, não azar
}

TEST_CASE("comparação em tempo constante ainda compara certo") {
  CHECK(constantTimeEquals("abc", "abc"));
  CHECK_FALSE(constantTimeEquals("abc", "abd"));
  CHECK_FALSE(constantTimeEquals("abc", "abcd"));
  CHECK_FALSE(constantTimeEquals("", "a"));
  CHECK(constantTimeEquals("", ""));
}
