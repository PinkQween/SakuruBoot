#pragma once
#include "../common/types.h"

/* ---- SHA-1 ---- */
typedef struct { u32 h[5]; u64 len; u8 buf[64]; u32 buf_len; } SHA1_CTX;
void sha1_init(SHA1_CTX *ctx);
void sha1_update(SHA1_CTX *ctx, const u8 *data, u32 len);
void sha1_final(SHA1_CTX *ctx, u8 out[20]);
void sha1(const u8 *data, u32 len, u8 out[20]);

/* ---- SHA-256 ---- */
typedef struct { u32 h[8]; u64 len; u8 buf[64]; u32 buf_len; } SHA256_CTX;
void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const u8 *data, u32 len);
void sha256_final(SHA256_CTX *ctx, u8 out[32]);
void sha256(const u8 *data, u32 len, u8 out[32]);

/* ---- SHA-512 ---- */
typedef struct { u64 h[8]; u64 len[2]; u8 buf[128]; u32 buf_len; } SHA512_CTX;
void sha512_init(SHA512_CTX *ctx);
void sha512_update(SHA512_CTX *ctx, const u8 *data, u32 len);
void sha512_final(SHA512_CTX *ctx, u8 out[64]);
void sha512(const u8 *data, u32 len, u8 out[64]);

/* Hash IDs */
#define HASH_SHA1   1
#define HASH_SHA256 2
#define HASH_SHA512 3
int hash_id(const char *name);     /* "sha1"/"sha256"/"sha512" → ID */
int hash_digest_size(int id);      /* returns 20/32/64 */
void hash_compute(int id, const u8 *data, u32 len, u8 *out);
