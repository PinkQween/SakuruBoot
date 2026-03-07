#pragma once
#include "../common/types.h"

/*
 * Blake2b — variable-length hash, 1-64 byte output.
 * Used internally by Argon2.
 */
typedef struct {
    u64 h[8];
    u64 t[2];
    u64 f[2];
    u8  buf[128];
    u32 buf_len;
    u32 out_len;
} Blake2b_CTX;

void blake2b_init  (Blake2b_CTX *ctx, u32 out_len,
                    const u8 *key, u32 key_len);  /* key_len=0 for no key */
void blake2b_update(Blake2b_CTX *ctx, const u8 *in, u32 len);
void blake2b_final (Blake2b_CTX *ctx, u8 *out);
void blake2b       (const u8 *in, u32 len,
                    const u8 *key, u32 key_len,
                    u8 *out, u32 out_len);
