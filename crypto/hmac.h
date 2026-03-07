#pragma once
#include "sha.h"

/*
 * HMAC-SHA{1,256,512}
 * key_len may be any length; keys >block_size are hashed first (RFC 2104).
 */
void hmac_sha1  (const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 out[20]);
void hmac_sha256(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 out[32]);
void hmac_sha512(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 out[64]);

/* Generic dispatch — hash_id is HASH_SHA{1,256,512} */
void hmac(int hash_id,
          const u8 *key, u32 klen,
          const u8 *msg, u32 mlen,
          u8 *out);
