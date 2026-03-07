#include "pbkdf2.h"
#include "hmac.h"
#include <stddef.h>

static void xor_buf(u8 *dst, const u8 *src, u32 len) {
    for (u32 i=0;i<len;i++) dst[i]^=src[i];
}

void pbkdf2(int hid,
            const u8 *password, u32 plen,
            const u8 *salt,     u32 slen,
            u32 iters,
            u8 *dk, u32 dklen) {
    int hlen = hash_digest_size(hid);
    /* Scratch: salt + 4-byte block counter */
    u8 salt_blk[512]; /* slen ≤ 508 for LUKS (32 bytes) */
    for (u32 i=0;i<slen;i++) salt_blk[i]=salt[i];

    u8 u[64], t[64];
    u32 out_off = 0;
    for (u32 blk = 1; dklen > 0; blk++) {
        /* U1 = HMAC(password, salt || INT(blk)) */
        salt_blk[slen+0]=(u8)(blk>>24);
        salt_blk[slen+1]=(u8)(blk>>16);
        salt_blk[slen+2]=(u8)(blk>>8);
        salt_blk[slen+3]=(u8)(blk);
        hmac(hid, password, plen, salt_blk, slen+4, u);
        for (int j=0;j<hlen;j++) t[j]=u[j];
        /* U2..Uc */
        for (u32 c=1;c<iters;c++) {
            hmac(hid, password, plen, u, (u32)hlen, u);
            xor_buf(t, u, (u32)hlen);
        }
        u32 take = (dklen < (u32)hlen) ? dklen : (u32)hlen;
        for (u32 i=0;i<take;i++) dk[out_off+i]=t[i];
        out_off += take;
        dklen   -= take;
    }
}
