#pragma once
#include "../common/types.h"

/*
 * PBKDF2 (RFC 2898 §5.2).
 * hash_id : HASH_SHA1 / HASH_SHA256 / HASH_SHA512
 * Derived key written to dk[0..dklen-1].
 */
void pbkdf2(int hash_id,
            const u8 *password, u32 plen,
            const u8 *salt,     u32 slen,
            u32 iterations,
            u8 *dk, u32 dklen);
