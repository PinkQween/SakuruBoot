#include "cipher.h"
#include "sha.h"
#include <stddef.h>

static int strpfx(const char *s, const char *pfx){
    while(*pfx) if(*s++!=*pfx++) return 0;
    return 1;
}

int cipher_mode_id(const char *m) {
    if (strpfx(m,"xts"))          return CIPHER_XTS;
    if (strpfx(m,"cbc-essiv"))    return CIPHER_CBC_ESSIV;
    if (strpfx(m,"cbc-plain64"))  return CIPHER_CBC_PLAIN64;
    if (strpfx(m,"cbc-plain"))    return CIPHER_CBC_PLAIN;
    return CIPHER_XTS; /* safe default */
}

/* ------------------------------------------------------------------ */
/* GF(2^128) multiply — for XTS tweak update                          */
/* ------------------------------------------------------------------ */
static void gf128_mul_x_le(u8 *t) {
    u8 carry = 0, next;
    for (int i = 0; i < 16; i++) {
        next = (t[i] >> 7) & 1;
        t[i] = (u8)((t[i] << 1) | carry);
        carry = next;
    }
    if (carry) t[0] ^= 0x87;
}

/* ------------------------------------------------------------------ */
/* XTS                                                                 */
/* ------------------------------------------------------------------ */
void xts_init(XTS_CTX *ctx, const u8 *key, int key_len, int decrypt) {
    int half = key_len / 2;
    if (decrypt) aes_init_dec(&ctx->data_ctx, key, half);
    else         aes_init_enc(&ctx->data_ctx, key, half);
    aes_init_enc(&ctx->tweak_ctx, key + half, half);
}

void xts_crypt(const XTS_CTX *ctx, u64 sector_num,
               const u8 *in, u8 *out, u32 len, int decrypt) {
    /* Encode sector number as 16-byte little-endian IV */
    u8 tweak[16] = {0};
    for (int i = 0; i < 8; i++) { tweak[i] = (u8)(sector_num & 0xff); sector_num >>= 8; }

    /* Encrypt the tweak */
    aes_encrypt_block(&ctx->tweak_ctx, tweak, tweak);

    for (u32 off = 0; off + 16 <= len; off += 16) {
        u8 tmp[16];
        for (int i = 0; i < 16; i++) tmp[i] = in[off+i] ^ tweak[i];
        if (decrypt) aes_decrypt_block(&ctx->data_ctx, tmp, tmp);
        else         aes_encrypt_block(&ctx->data_ctx, tmp, tmp);
        for (int i = 0; i < 16; i++) out[off+i] = tmp[i] ^ tweak[i];
        gf128_mul_x_le(tweak);
    }
}

/* ------------------------------------------------------------------ */
/* CBC                                                                 */
/* ------------------------------------------------------------------ */
void cbc_init(CBC_CTX *ctx, const u8 *key, int key_len,
              int mode, const u8 *essiv_hash, int essiv_hash_len) {
    aes_init_enc(&ctx->enc_ctx, key, key_len);
    aes_init_dec(&ctx->dec_ctx, key, key_len);
    ctx->mode = mode;
    if (mode == CIPHER_CBC_ESSIV && essiv_hash && essiv_hash_len >= 32) {
        /* ESSIV key = SHA-256(key), used as AES key for IV encryption */
        u8 essiv_key[32];
        sha256(key, (u32)key_len, essiv_key);
        aes_init_enc(&ctx->essiv_ctx, essiv_key, 32);
    }
}

static void make_cbc_iv(const CBC_CTX *ctx, u64 sector_num, u8 iv[16]) {
    for (int i = 0; i < 16; i++) iv[i] = 0;
    switch (ctx->mode) {
    case CIPHER_CBC_PLAIN:
        iv[0]=(u8)sector_num; iv[1]=(u8)(sector_num>>8);
        iv[2]=(u8)(sector_num>>16); iv[3]=(u8)(sector_num>>24);
        break;
    case CIPHER_CBC_PLAIN64:
        for(int i=0;i<8;i++){iv[i]=(u8)(sector_num&0xff);sector_num>>=8;}
        break;
    case CIPHER_CBC_ESSIV: {
        u8 tmp[16]={0};
        for(int i=0;i<8;i++){tmp[i]=(u8)(sector_num&0xff);sector_num>>=8;}
        aes_encrypt_block(&ctx->essiv_ctx, tmp, iv);
        break;
    }
    default: break;
    }
}

void cbc_decrypt_sector(const CBC_CTX *ctx, u64 sector_num,
                        const u8 *in, u8 *out, u32 len) {
    u8 iv[16];
    make_cbc_iv(ctx, sector_num, iv);

    for (u32 off = 0; off + 16 <= len; off += 16) {
        u8 tmp[16];
        aes_decrypt_block(&ctx->dec_ctx, in+off, tmp);
        for (int i = 0; i < 16; i++) out[off+i] = tmp[i] ^ iv[i];
        for (int i = 0; i < 16; i++) iv[i] = in[off+i]; /* next IV = ciphertext */
    }
}
