/*
 * tests/unit/test_crypto.c — Unit tests for crypto primitives
 *
 * All test vectors are from published standards / RFCs / NIST.
 * Exercises: SHA-256, SHA-512, HMAC-SHA256, PBKDF2-SHA256, AES-128,
 *            AES-256, Blake2b.
 *
 * Compiled with -DSAKURU_HOST_TEST (via CMake). Each crypto .c file
 * is compiled as a separate translation unit to avoid static-function
 * name collisions (e.g. ror64 in both sha.c and blake2b.c).
 */
#include "../framework/sakuru_test.h"
#include "../../crypto/sha.h"
#include "../../crypto/hmac.h"
#include "../../crypto/pbkdf2.h"
#include "../../crypto/blake2b.h"
#include "../../crypto/aes.h"

#include <stdio.h>
#include <string.h>

/* Helper: hex-decode a string into bytes */
static void hex2bin(const char *hex, u8 *out, size_t out_len) {
    for (size_t i = 0; i < out_len; i++) {
        unsigned v = 0;
        sscanf(hex + i * 2, "%02x", &v);
        out[i] = (u8)v;
    }
}

/* ── SHA-256 (FIPS 180-4 example vectors) ────────────────────────────── */
TEST(SHA256, EmptyString) {
    u8 out[32], expected[32];
    sha256((const u8 *)"", 0, out);
    hex2bin("e3b0c44298fc1c149afbf4c8996fb924"
            "27ae41e4649b934ca495991b7852b855",
            expected, 32);
    EXPECT_MEM_EQ(out, expected, 32);
}

TEST(SHA256, LongerMessage) {
    /* SHA-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") */
    const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    u8 out[32], expected[32];
    sha256((const u8 *)msg, (u32)strlen(msg), out);
    hex2bin("248d6a61d20638b8e5c026930c3e6039"
            "a33ce45964ff2167f6ecedd419db06c1",
            expected, 32);
    EXPECT_MEM_EQ(out, expected, 32);
}

TEST(SHA256, Deterministic) {
    /* Same input → same output (second call identical to first) */
    u8 a[32], b[32];
    const u8 *msg = (const u8 *)"SakuruBoot";
    sha256(msg, 10, a);
    sha256(msg, 10, b);
    EXPECT_MEM_EQ(a, b, 32);
    /* Output must not be all zeros */
    int allzero = 1;
    for (int i = 0; i < 32; i++) if (a[i]) { allzero = 0; break; }
    EXPECT_FALSE(allzero);
}

/* ── SHA-512 ─────────────────────────────────────────────────────────── */
TEST(SHA512, EmptyString) {
    u8 out[64], expected[64];
    sha512((const u8 *)"", 0, out);
    hex2bin("cf83e1357eefb8bdf1542850d66d8007"
            "d620e4050b5715dc83f4a921d36ce9ce"
            "47d0d13c5d85f2b0ff8318d2877eec2f"
            "63b931bd47417a81a538327af927da3e",
            expected, 64);
    EXPECT_MEM_EQ(out, expected, 64);
}

TEST(SHA512, Deterministic) {
    u8 a[64], b[64];
    sha512((const u8 *)"test", 4, a);
    sha512((const u8 *)"test", 4, b);
    EXPECT_MEM_EQ(a, b, 64);
}

/* ── HMAC-SHA256 (RFC 4231 Test Case 1) ──────────────────────────────── */
TEST(HMAC_SHA256, RFC4231_TC1) {
    /* Key = 0x0b * 20, Data = "Hi There" */
    u8 key[20]; memset(key, 0x0b, 20);
    const u8 *data = (const u8 *)"Hi There";
    u8 out[32], expected[32];
    hmac_sha256(key, 20, data, 8, out);
    hex2bin("b0344c61d8db38535ca8afceaf0bf12b"
            "881dc200c9833da726e9376c2e32cff7",
            expected, 32);
    EXPECT_MEM_EQ(out, expected, 32);
}

TEST(HMAC_SHA256, RFC4231_TC2) {
    /* Key = "Jefe", Data = "what do ya want for nothing?" */
    const u8 *key  = (const u8 *)"Jefe";
    const u8 *data = (const u8 *)"what do ya want for nothing?";
    u8 out[32], expected[32];
    hmac_sha256(key, 4, data, (u32)strlen((const char *)data), out);
    hex2bin("5bdcc146bf60754e6a042426089575c7"
            "5a003f089d2739839dec58b964ec3843",
            expected, 32);
    EXPECT_MEM_EQ(out, expected, 32);
}

TEST(HMAC_SHA256, DifferentKeysProduceDifferentResults) {
    u8 key1[4] = {0x01,0x02,0x03,0x04};
    u8 key2[4] = {0x05,0x06,0x07,0x08};
    const u8 *msg = (const u8 *)"hello";
    u8 out1[32], out2[32];
    hmac_sha256(key1, 4, msg, 5, out1);
    hmac_sha256(key2, 4, msg, 5, out2);
    int same = (memcmp(out1, out2, 32) == 0);
    EXPECT_FALSE(same);
}

/* ── PBKDF2-SHA256 ────────────────────────────────────────────────────── */
TEST(PBKDF2_SHA256, BasicVector) {
    /* PBKDF2-SHA256("password","salt",1,32) — from Crypt::PBKDF2 test suite */
    u8 out[32], expected[32];
    pbkdf2(HASH_SHA256,
           (const u8 *)"password", 8,
           (const u8 *)"salt", 4,
           1, out, 32);
    hex2bin("120fb6cffcf8b32c43e7225256c4f837"
            "a86548c92ccc35480805987cb70be17b",
            expected, 32);
    EXPECT_MEM_EQ(out, expected, 32);
}

TEST(PBKDF2_SHA256, Deterministic) {
    u8 a[32], b[32];
    pbkdf2(HASH_SHA256,
           (const u8 *)"sakuru", 6,
           (const u8 *)"boot", 4,
           100, a, 32);
    pbkdf2(HASH_SHA256,
           (const u8 *)"sakuru", 6,
           (const u8 *)"boot", 4,
           100, b, 32);
    EXPECT_MEM_EQ(a, b, 32);
}

TEST(PBKDF2_SHA256, DifferentItersProduceDifferentOutput) {
    u8 a[32], b[32];
    pbkdf2(HASH_SHA256, (const u8 *)"pw", 2,
           (const u8 *)"salt", 4, 1,    a, 32);
    pbkdf2(HASH_SHA256, (const u8 *)"pw", 2,
           (const u8 *)"salt", 4, 1000, b, 32);
    int same = (memcmp(a, b, 32) == 0);
    EXPECT_FALSE(same);
}

/* ── AES-128 (NIST FIPS 197 Appendix B) ─────────────────────────────── */
TEST(AES128, EncryptBlock) {
    u8 key[16], pt[16], ct[16], expected[16];
    hex2bin("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    hex2bin("6bc1bee22e409f96e93d7e117393172a", pt,  16);
    hex2bin("3ad77bb40d7a3660a89ecaf32466ef97", expected, 16);

    AES_CTX ctx;
    aes_init_enc(&ctx, key, 16);
    aes_encrypt_block(&ctx, pt, ct);
    EXPECT_MEM_EQ(ct, expected, 16);
}

TEST(AES128, DecryptBlock) {
    u8 key[16], ct[16], pt[16], expected[16];
    hex2bin("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    hex2bin("3ad77bb40d7a3660a89ecaf32466ef97", ct,  16);
    hex2bin("6bc1bee22e409f96e93d7e117393172a", expected, 16);

    AES_CTX ctx;
    aes_init_dec(&ctx, key, 16);
    aes_decrypt_block(&ctx, ct, pt);
    EXPECT_MEM_EQ(pt, expected, 16);
}

TEST(AES128, EncryptDecryptRoundtrip) {
    u8 key[16] = {0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
                  0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10};
    const u8 plain[16] = "SakuruBoot!1234";
    u8 ct[16], pt[16];

    AES_CTX enc_ctx, dec_ctx;
    aes_init_enc(&enc_ctx, key, 16);
    aes_init_dec(&dec_ctx, key, 16);

    aes_encrypt_block(&enc_ctx, plain, ct);
    aes_decrypt_block(&dec_ctx, ct, pt);
    EXPECT_MEM_EQ(pt, plain, 16);
}

TEST(AES256, EncryptDecryptRoundtrip) {
    u8 key[32];
    for (int i = 0; i < 32; i++) key[i] = (u8)i;
    const u8 plain[16] = {0xde,0xad,0xbe,0xef,0xca,0xfe,0xba,0xbe,
                          0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
    u8 ct[16], pt[16];

    AES_CTX enc_ctx, dec_ctx;
    aes_init_enc(&enc_ctx, key, 32);
    aes_init_dec(&dec_ctx, key, 32);

    aes_encrypt_block(&enc_ctx, plain, ct);
    aes_decrypt_block(&dec_ctx, ct, pt);
    EXPECT_MEM_EQ(pt, plain, 16);
}

/* ── Blake2b ─────────────────────────────────────────────────────────── */
TEST(Blake2b, EmptyUnkeyed) {
    /* blake2b-512 of empty input, no key — from RFC 7693 Appendix A */
    u8 out[64], expected[64];
    blake2b((const u8 *)"", 0, NULL, 0, out, 64);
    hex2bin("786a02f742015903c6c6fd852552d272"
            "912f4740e15847618a86e217f71f5419"
            "d25e1031afee585313896444934eb04b"
            "903a685b1448b755d56f701afe9be2ce",
            expected, 64);
    EXPECT_MEM_EQ(out, expected, 64);
}

TEST(Blake2b, Deterministic) {
    u8 a[64], b[64];
    blake2b((const u8 *)"hello world", 11, NULL, 0, a, 64);
    blake2b((const u8 *)"hello world", 11, NULL, 0, b, 64);
    EXPECT_MEM_EQ(a, b, 64);
}

TEST(Blake2b, KeyedVsUnkeyedDiffer) {
    u8 key[16]; memset(key, 0xAB, 16);
    u8 keyed[64], unkeyed[64];
    blake2b((const u8 *)"data", 4, key,  16, keyed,   64);
    blake2b((const u8 *)"data", 4, NULL,  0, unkeyed, 64);
    int same = (memcmp(keyed, unkeyed, 64) == 0);
    EXPECT_FALSE(same);
}

TEST(Blake2b, ShorterOutput) {
    /* 32-byte output variant */
    u8 out32[32], out64[64];
    blake2b((const u8 *)"SakuruBoot", 10, NULL, 0, out32, 32);
    blake2b((const u8 *)"SakuruBoot", 10, NULL, 0, out64, 64);
    /* First 32 bytes of 64-byte output are NOT the same as 32-byte output */
    /* (blake2b truncation is not a prefix) — just check no crash + non-zero */
    int allzero = 1;
    for (int i = 0; i < 32; i++) if (out32[i]) { allzero = 0; break; }
    EXPECT_FALSE(allzero);
}

int main(void) { return sakuru_run_all(); }
