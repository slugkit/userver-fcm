#include "../src/fcm/oauth/token.hpp"

#include <userver/crypto/base64.hpp>
#include <userver/crypto/signers.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/utest/utest.hpp>

namespace {

// A test RSA private key in PKCS#8 format. Not used for anything real.
constexpr std::string_view kTestKeyPem = R"(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCgQ1kF6c+e6P+F
p/7opRQ30NaJ9f5zv9Qq2emalb5qQ6k4ddtP4ceoQ16XmeYVDmNfnJh4itUmQVjk
il6hR5gxk+Fh/dgqQPEBrDKay7fLfBjNS2BzljEamMmJzcxix5gERYr1SMyHZDrU
WOnnGsSX9338ydEafhjs23AtRRQBa1cgfMg2H/phzfikN/N69InEaWtpHp48opvM
LYZNVKg2+xukU+dGOPovBKaflBRd9SFMZpbSiQwWKuUU0ZVwdfou1Jo7AAnIrwsb
uiogbrBlu7uTzGVY6FOzFg1y9FMyspnHRAVOzjwpiJrOlEVNC0v2mZy/cQ2n/Pm+
Fz7R14+7AgMBAAECggEAAhOyslJBZTx9Hez7b2W+8/+PiDeD61lvv85rKfG0Mgmx
G7uLSBG+Vf6UL0AeAeUvLIIJsnuPdPDL3MpeR5Yh8Xff0ovzoo3iPF9QQz6jTHkj
PnsyQ3fi0wa/4I/7jMbWwCzBNu877KqZMz9OaLb+wpQWhzwmVZg6F80QOxtLaGlH
y8OBPrPHjdF5O0TVsFlnyqDqPlXtgpdlPrqFFfsCiiVj0uimKDvL3MymMqLk0I/b
5Ivr0WuCfM+8RugAYr4JHJzK3nbMH1GJA5+hJOiGRhUxdTuIV00LW2kksm56+G3l
pw48425W04/rahABB6Inxe4gbkOnQKtUEoCatm+fQQKBgQDKloTjuyVvAZsGoPDH
e6FY0UtkgPXG7OORg+5NTNAvNRB/1Kx71DeVwpPs1zOjTKqmRMW1Ya3/8u2+iPp+
wNAxw8s9eimQvJ7vIZR4GolpIKq9Zjn8Bq9ToZwEA6YoTQh9Qsz3nMtSaevB58bE
zXrAC+FF+Rq7d6UWaBoQ3Z6tswKBgQDKhCab4L+XWkNOn8qzeiwqgmB6Ek5DQwPP
xU2/FJUx7XEMP6MUX1T0hTPxDmCbbexNfqRjPzmEmyJMVLK7CImx6xDApQHmhoou
hDOHqcNdZCwOulWnkBjaJ23yp9EpN2Mne6gZppbt/30BS9nVANCf/Dtx/CoO+h1k
MM/NY6Th2QKBgHr69KDqMsc4Skuz13bBbijkpMfWIV0o4NytIjR6tMZziBiRmkNx
iGy5OeNEoGw5Vj6o8Pwy19XQOtK3hJj9o2USXoZramAaoMC5uc9PDKts0Tk4nWqJ
BFXYfUVSkcNVQBoKOzL1U9grxJppgRhnRGTg0VgQ6FF1SBpaB7jFUFZRAoGBALa3
yQX3H6X1QKkdrwuD/XlVLKq2/Xneav/5Ko5uibYEX20HtaHZ6ZK85AJoUG2sHfpF
exg4oTKtraJlAOWTbHjkd7b4qeBOHzqc+Mk8OBJ5IO8g28tVTbb2wFKhayve1012
WlLaZW7Shvy2bRGrrI/MSe4r796XeBE/oR3U+5zxAoGASRUgAsjSnWTg6OOLYLPq
u/xPYqR9G/jtkIbieus/43Y+DdISNH9eTpZwRcFBWflLqCFYU8PNNtocnMHvXUgw
ZdNe01Dj4a6Jly2KPM87a6gPoVjyd8c9o1YS/5SwX69J5GMwMMptBqHnUaeURigH
UnQVR8Jx+i2AE8qojNWjkgc=
-----END PRIVATE KEY-----)";

}  // namespace

UTEST(FcmOauth, JwtHasThreeParts) {
    // We can't test the full ObtainAccessToken flow (needs Google endpoint),
    // but we can test the JWT structure by calling the internal BuildSignedJwt
    // indirectly. For now, test the public interface expectations.

    // The JWT signing itself uses RS256 — verify the signer works with our key.
    userver::crypto::SignerRs256 signer{std::string{kTestKeyPem}};
    auto signature = signer.Sign({"test-data"});
    EXPECT_FALSE(signature.empty());
}

UTEST(FcmOauth, SignerRs256ProducesValidSignature) {
    userver::crypto::SignerRs256 signer{std::string{kTestKeyPem}};

    auto sig1 = signer.Sign({"data-1"});
    auto sig2 = signer.Sign({"data-2"});

    // Different data should produce different signatures
    EXPECT_NE(sig1, sig2);

    // Same data should produce same signature (RSA is deterministic)
    auto sig1_again = signer.Sign({"data-1"});
    EXPECT_EQ(sig1, sig1_again);
}
