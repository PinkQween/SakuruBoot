#include "hmac.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* HMAC generic (block_size, hash function via SHA ctx)               */
/* ------------------------------------------------------------------ */

void hmac_sha1(const u8 *key, u32 klen,
               const u8 *msg, u32 mlen, u8 out[20]) {
    u8 k[64], ipad[64], opad[64], inner[20];
    /* Hash key if longer than block size */
    if (klen > 64) { sha1(key, klen, k); klen = 20; }
    else { for (u32 i=0;i<klen;i++) k[i]=key[i]; }
    for (u32 i=klen;i<64;i++) k[i]=0;
    for (int i=0;i<64;i++) { ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5c; }
    /* inner = SHA1(ipad || msg) */
    SHA1_CTX ctx;
    sha1_init(&ctx); sha1_update(&ctx,ipad,64); sha1_update(&ctx,msg,mlen);
    sha1_final(&ctx,inner);
    /* out = SHA1(opad || inner) */
    sha1_init(&ctx); sha1_update(&ctx,opad,64); sha1_update(&ctx,inner,20);
    sha1_final(&ctx,out);
}

void hmac_sha256(const u8 *key, u32 klen,
                 const u8 *msg, u32 mlen, u8 out[32]) {
    u8 k[64], ipad[64], opad[64], inner[32];
    if (klen > 64) { sha256(key, klen, k); klen = 32; }
    else { for (u32 i=0;i<klen;i++) k[i]=key[i]; }
    for (u32 i=klen;i<64;i++) k[i]=0;
    for (int i=0;i<64;i++) { ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5c; }
    SHA256_CTX ctx;
    sha256_init(&ctx); sha256_update(&ctx,ipad,64); sha256_update(&ctx,msg,mlen);
    sha256_final(&ctx,inner);
    sha256_init(&ctx); sha256_update(&ctx,opad,64); sha256_update(&ctx,inner,32);
    sha256_final(&ctx,out);
}

void hmac_sha512(const u8 *key, u32 klen,
                 const u8 *msg, u32 mlen, u8 out[64]) {
    u8 k[128], ipad[128], opad[128], inner[64];
    if (klen > 128) { sha512(key, klen, k); klen = 64; }
    else { for (u32 i=0;i<klen;i++) k[i]=key[i]; }
    for (u32 i=klen;i<128;i++) k[i]=0;
    for (int i=0;i<128;i++) { ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5c; }
    SHA512_CTX ctx;
    sha512_init(&ctx); sha512_update(&ctx,ipad,128); sha512_update(&ctx,msg,mlen);
    sha512_final(&ctx,inner);
    sha512_init(&ctx); sha512_update(&ctx,opad,128); sha512_update(&ctx,inner,64);
    sha512_final(&ctx,out);
}

void hmac(int id, const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 *out) {
    if (id == HASH_SHA1)   { hmac_sha1  (key,klen,msg,mlen,out); return; }
    if (id == HASH_SHA256) { hmac_sha256(key,klen,msg,mlen,out); return; }
    if (id == HASH_SHA512) { hmac_sha512(key,klen,msg,mlen,out); return; }
}
