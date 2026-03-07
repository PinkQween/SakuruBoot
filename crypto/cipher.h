#pragma once
#include "aes.h"

/*
 * Disk encryption modes used by LUKS.
 *
 * cipher_mode_id values:
 *   CIPHER_XTS  — XTS-AES (LUKS2 default, most LUKS1)
 *   CIPHER_CBC_PLAIN   — CBC with plain (sector-number) IV
 *   CIPHER_CBC_PLAIN64 — CBC with 64-bit plain IV
 *   CIPHER_CBC_ESSIV   — CBC with ESSIV (SHA256) IV
 */

#define CIPHER_XTS          1
#define CIPHER_CBC_PLAIN    2
#define CIPHER_CBC_PLAIN64  3
#define CIPHER_CBC_ESSIV    4

/*
 * Parse a LUKS cipher-mode string into a CIPHER_* id.
 * e.g. "xts-plain64" → CIPHER_XTS, "cbc-essiv:sha256" → CIPHER_CBC_ESSIV
 */
int cipher_mode_id(const char *mode_str);

/* ------------------------------------------------------------------ */
/* XTS-AES                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    AES_CTX data_ctx;   /* encrypt (enc) or decrypt (dec) */
    AES_CTX tweak_ctx;  /* always encrypt */
} XTS_CTX;

/*
 * key points to two concatenated keys (total 32 or 64 bytes for AES-128 or AES-256).
 * For decryption, init with decrypt=1.
 */
void xts_init(XTS_CTX *ctx, const u8 *key, int key_len, int decrypt);

/*
 * Encrypt/decrypt one sector.
 * sector_num is the logical sector (used as the tweak).
 * len must be a multiple of 16, >= 16.
 */
void xts_crypt(const XTS_CTX *ctx, u64 sector_num,
               const u8 *in, u8 *out, u32 len, int decrypt);

/* ------------------------------------------------------------------ */
/* CBC-AES                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    AES_CTX enc_ctx;
    AES_CTX dec_ctx;
    int mode;            /* CIPHER_CBC_* */
    /* ESSIV key (SHA-256 of master key → AES key for IV generation) */
    AES_CTX essiv_ctx;
} CBC_CTX;

void cbc_init(CBC_CTX *ctx, const u8 *key, int key_len,
              int mode,
              const u8 *essiv_hash, int essiv_hash_len); /* NULL if not ESSIV */

void cbc_decrypt_sector(const CBC_CTX *ctx, u64 sector_num,
                        const u8 *in, u8 *out, u32 len);
