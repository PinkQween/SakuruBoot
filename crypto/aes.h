#pragma once
#include "../common/types.h"

/*
 * AES block cipher — 128-bit and 256-bit key variants.
 * Only ECB single-block encrypt/decrypt exposed here.
 * Higher-level modes (XTS, CBC) live in cipher.h.
 */

#define AES_BLOCK_SIZE 16

typedef struct { u32 rk[60]; int nr; } AES_CTX;

/* key_len: 16 (AES-128) or 32 (AES-256) */
void aes_init_enc(AES_CTX *ctx, const u8 *key, int key_len);
void aes_init_dec(AES_CTX *ctx, const u8 *key, int key_len);

void aes_encrypt_block(const AES_CTX *ctx, const u8 in[16], u8 out[16]);
void aes_decrypt_block(const AES_CTX *ctx, const u8 in[16], u8 out[16]);
