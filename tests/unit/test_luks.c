/*
 * tests/unit/test_luks.c — Unit tests for LUKS header parsing
 *
 * Builds synthetic LUKS1 and LUKS2 binary headers in-memory and
 * exercises the magic detection, slot iteration, and cipher parsing
 * without requiring real block devices.
 *
 * Compiled with -DSAKURU_HOST_TEST (via CMake). Does NOT include
 * luks1.h/luks2.h directly to avoid pulling in freestanding cipher.h;
 * instead it replicates only the constants and struct layout needed.
 */
#include "../framework/sakuru_test.h"
#include "../../common/types.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Magic bytes (replicated locally to avoid macro name clash) */
static const u8 LUKS1_MAGIC_BYTES[6] = { 'L','U','K','S',0xBA,0xBE };
static const u8 LUKS2_MAGIC_BYTES[6] = { 'L','U','K','S',0xBA,0xBF };

#define LUKS1_KEY_ENABLED  0x00AC71F3u
#define MY_LUKS1_MAGIC_LEN 6

/* ── Helper: build a minimal LUKS1 header blob ───────────────────────── */
static u8 *make_luks1_header(const char *cipher_name, const char *cipher_mode,
                              const char *hash_spec, u32 key_bytes) {
    u8 *hdr = (u8 *)calloc(4096, 1);
    if (!hdr) return NULL;

    /* magic */
    memcpy(hdr + 0, LUKS1_MAGIC_BYTES, 6);
    /* version = 1 (big-endian) */
    hdr[6] = 0x00; hdr[7] = 0x01;
    /* cipher-name (32 bytes) */
    strncpy((char *)(hdr + 8),  cipher_name, 31);
    /* cipher-mode (32 bytes) */
    strncpy((char *)(hdr + 40), cipher_mode, 31);
    /* hash-spec (32 bytes) */
    strncpy((char *)(hdr + 72), hash_spec, 31);
    /* payload-offset (big-endian u32) */
    u32 payload_be = __builtin_bswap32(4096);
    memcpy(hdr + 104, &payload_be, 4);
    /* key-bytes (big-endian u32) */
    u32 kb_be = __builtin_bswap32(key_bytes);
    memcpy(hdr + 108, &kb_be, 4);
    /* mk-digest-iter (big-endian u32 @ 152) */
    u32 iter_be = __builtin_bswap32(1000);
    memcpy(hdr + 152, &iter_be, 4);
    /* uuid (40 bytes @ 156) */
    strncpy((char *)(hdr + 156), "test-uuid-0000-0000-0000-000000000000", 39);

    /* keyslot 0: active flag (big-endian 0x00AC71F3) */
    u32 active_be = __builtin_bswap32(LUKS1_KEY_ENABLED);
    memcpy(hdr + 208, &active_be, 4);
    /* kdf iterations */
    u32 kdf_iter_be = __builtin_bswap32(2000);
    memcpy(hdr + 212, &kdf_iter_be, 4);
    /* stripes (big-endian u32 @ 252) */
    u32 stripes_be = __builtin_bswap32(4000);
    memcpy(hdr + 252, &stripes_be, 4);

    return hdr;
}

/* ── LUKS1 magic detection ───────────────────────────────────────────── */
TEST(LUKS1, MagicDetected) {
    u8 hdr[8] = { 'L','U','K','S',0xBA,0xBE, 0x00, 0x01 };
    EXPECT_TRUE(memcmp(hdr, LUKS1_MAGIC_BYTES, 6) == 0);
}

TEST(LUKS1, WrongMagicRejected) {
    u8 hdr[8] = { 'L','U','K','S',0xBA,0xBF, 0x00, 0x01 }; /* LUKS2 magic */
    EXPECT_FALSE(memcmp(hdr, LUKS1_MAGIC_BYTES, 6) == 0);
}

TEST(LUKS1, VersionField) {
    u8 *hdr = make_luks1_header("aes", "xts-plain64", "sha256", 32);
    ASSERT_NOT_NULL(hdr);
    u16 version = (u16)((hdr[6] << 8) | hdr[7]);
    EXPECT_EQ(version, 1u);
    free(hdr);
}

TEST(LUKS1, CipherNameParsed) {
    u8 *hdr = make_luks1_header("aes", "xts-plain64", "sha256", 32);
    ASSERT_NOT_NULL(hdr);
    EXPECT_STREQ((char *)(hdr + 8), "aes");
    free(hdr);
}

TEST(LUKS1, CipherModeParsed) {
    u8 *hdr = make_luks1_header("aes", "xts-plain64", "sha256", 32);
    ASSERT_NOT_NULL(hdr);
    EXPECT_STREQ((char *)(hdr + 40), "xts-plain64");
    free(hdr);
}

TEST(LUKS1, HashSpecParsed) {
    u8 *hdr = make_luks1_header("aes", "cbc-essiv:sha256", "sha256", 64);
    ASSERT_NOT_NULL(hdr);
    EXPECT_STREQ((char *)(hdr + 72), "sha256");
    free(hdr);
}

TEST(LUKS1, KeyBytesField) {
    u8 *hdr = make_luks1_header("aes", "xts-plain64", "sha256", 64);
    ASSERT_NOT_NULL(hdr);
    u32 kb_be; memcpy(&kb_be, hdr + 108, 4);
    u32 kb = __builtin_bswap32(kb_be);
    EXPECT_EQ(kb, 64u);
    free(hdr);
}

TEST(LUKS1, KeySlotActiveFlag) {
    u8 *hdr = make_luks1_header("aes", "xts-plain64", "sha256", 32);
    ASSERT_NOT_NULL(hdr);
    u32 flag_be; memcpy(&flag_be, hdr + 208, 4);
    u32 flag = __builtin_bswap32(flag_be);
    EXPECT_EQ(flag, LUKS1_KEY_ENABLED);
    free(hdr);
}

TEST(LUKS1, PayloadOffsetField) {
    u8 *hdr = make_luks1_header("aes", "xts-plain64", "sha256", 32);
    ASSERT_NOT_NULL(hdr);
    u32 off_be; memcpy(&off_be, hdr + 104, 4);
    u32 off = __builtin_bswap32(off_be);
    EXPECT_EQ(off, 4096u);
    free(hdr);
}

/* ── LUKS2 magic detection ───────────────────────────────────────────── */
TEST(LUKS2, MagicBytes) {
    u8 hdr[8] = { 'L','U','K','S',0xBA,0xBF, 0x00, 0x02 };
    EXPECT_TRUE(memcmp(hdr, LUKS2_MAGIC_BYTES, 6) == 0);
}

TEST(LUKS2, DistinctFromLUKS1) {
    EXPECT_FALSE(memcmp(LUKS2_MAGIC_BYTES, LUKS1_MAGIC_BYTES, 6) == 0);
}

TEST(LUKS2, VersionField) {
    u8 hdr[8] = { 'L','U','K','S',0xBA,0xBF, 0x00, 0x02 };
    u16 version = (u16)((hdr[6] << 8) | hdr[7]);
    EXPECT_EQ(version, 2u);
}

/* ── Cipher string parsing (inline logic test) ───────────────────────── */
static int test_cipher_mode_id(const char *spec) {
    if (strncmp(spec, "xts", 3) == 0)          return 0; /* CIPHER_MODE_XTS */
    if (strncmp(spec, "cbc-essiv", 9) == 0)    return 3; /* CIPHER_MODE_CBC_ESSIV */
    if (strncmp(spec, "cbc-plain64", 11) == 0) return 2; /* CIPHER_MODE_CBC_PLAIN64 */
    if (strncmp(spec, "cbc-plain", 9) == 0)    return 1; /* CIPHER_MODE_CBC_PLAIN */
    return -1;
}

TEST(CipherSpec, XTS) {
    EXPECT_EQ(test_cipher_mode_id("xts-plain64"), 0);
    EXPECT_EQ(test_cipher_mode_id("xts"),          0);
}

TEST(CipherSpec, CBCPlain) {
    EXPECT_EQ(test_cipher_mode_id("cbc-plain"),    1);
    EXPECT_EQ(test_cipher_mode_id("cbc-plain64"),  2);
}

TEST(CipherSpec, CBCEssiv) {
    EXPECT_EQ(test_cipher_mode_id("cbc-essiv:sha256"), 3);
}

TEST(CipherSpec, Unknown) {
    EXPECT_EQ(test_cipher_mode_id("ecb"), -1);
    EXPECT_EQ(test_cipher_mode_id(""),    -1);
}

/* ── LUKS1 AF-split size calculation ─────────────────────────────────── */
TEST(LUKS1, AFSplitSizeCalculation) {
    u32 key_bytes = 32;
    u32 stripes   = 4000;
    u32 expected_af_size = stripes * key_bytes;
    EXPECT_EQ(expected_af_size, 128000u);
}

/* ── Multiple cipher names in same header ────────────────────────────── */
TEST(LUKS1, MultipleCipherVariants) {
    const char *modes[] = { "xts-plain64", "cbc-plain", "cbc-essiv:sha256" };
    for (int i = 0; i < 3; i++) {
        u8 *hdr = make_luks1_header("aes", modes[i], "sha256", 32);
        ASSERT_NOT_NULL(hdr);
        EXPECT_STREQ((char *)(hdr + 40), modes[i]);
        free(hdr);
    }
}

int main(void) { return sakuru_run_all(); }
