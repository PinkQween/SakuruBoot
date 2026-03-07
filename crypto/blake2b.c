#include "blake2b.h"
#include <stddef.h>

static const u64 IV[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull,
    0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
    0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull,
};
static const u8 SIGMA[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3},
    {11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4},
    { 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8},
    { 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13},
    { 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9},
    {12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11},
    {13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10},
    { 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0},
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15},
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3},
};

static u64 le64r(const u8 *p) {
    u64 v=0; for(int i=7;i>=0;i--) v=(v<<8)|p[i]; return v;
}
static void put_le64(u8 *p, u64 v) {
    for(int i=0;i<8;i++){p[i]=(u8)v;v>>=8;}
}
static u64 ror64(u64 x,int n){return(x>>n)|(x<<(64-n));}

#define G(r,i,a,b,c,d) do { \
    a=a+b+m[SIGMA[r][2*i+0]]; d=ror64(d^a,32); \
    c=c+d;                    b=ror64(b^c,24); \
    a=a+b+m[SIGMA[r][2*i+1]]; d=ror64(d^a,16); \
    c=c+d;                    b=ror64(b^c,63); \
} while(0)

static void blake2b_compress(Blake2b_CTX *ctx, const u8 blk[128], int last) {
    u64 v[16], m[16];
    for(int i=0;i<8;i++) v[i]=ctx->h[i];
    for(int i=0;i<8;i++) v[8+i]=IV[i];
    v[12]^=ctx->t[0]; v[13]^=ctx->t[1];
    if(last) v[14]=~v[14];
    for(int i=0;i<16;i++) m[i]=le64r(blk+i*8);
    for(int r=0;r<12;r++){
        G(r,0,v[0],v[4],v[ 8],v[12]);
        G(r,1,v[1],v[5],v[ 9],v[13]);
        G(r,2,v[2],v[6],v[10],v[14]);
        G(r,3,v[3],v[7],v[11],v[15]);
        G(r,4,v[0],v[5],v[10],v[15]);
        G(r,5,v[1],v[6],v[11],v[12]);
        G(r,6,v[2],v[7],v[ 8],v[13]);
        G(r,7,v[3],v[4],v[ 9],v[14]);
    }
    for(int i=0;i<8;i++) ctx->h[i]^=v[i]^v[8+i];
}

void blake2b_init(Blake2b_CTX *ctx, u32 out_len,
                  const u8 *key, u32 key_len) {
    for(int i=0;i<8;i++) ctx->h[i]=IV[i];
    ctx->h[0] ^= 0x01010000ull ^ ((u64)key_len<<8) ^ (u64)out_len;
    ctx->t[0]=ctx->t[1]=ctx->f[0]=ctx->f[1]=0;
    ctx->buf_len=0; ctx->out_len=out_len;
    for(int i=0;i<128;i++) ctx->buf[i]=0;
    if(key_len){
        for(u32 i=0;i<key_len;i++) ctx->buf[i]=key[i];
        ctx->buf_len=128;
    }
}

void blake2b_update(Blake2b_CTX *ctx, const u8 *in, u32 len) {
    u32 i=0;
    while(i<len){
        if(ctx->buf_len==128){
            ctx->t[0]+=128; if(ctx->t[0]<128) ctx->t[1]++;
            blake2b_compress(ctx,ctx->buf,0);
            ctx->buf_len=0;
        }
        ctx->buf[ctx->buf_len++]=in[i++];
    }
}

void blake2b_final(Blake2b_CTX *ctx, u8 *out) {
    ctx->t[0]+=ctx->buf_len; if(ctx->t[0]<ctx->buf_len) ctx->t[1]++;
    ctx->f[0]=~0ull;
    for(u32 i=ctx->buf_len;i<128;i++) ctx->buf[i]=0;
    blake2b_compress(ctx,ctx->buf,1);
    u8 tmp[64]; for(int i=0;i<8;i++) put_le64(tmp+i*8,ctx->h[i]);
    for(u32 i=0;i<ctx->out_len;i++) out[i]=tmp[i];
}

void blake2b(const u8 *in, u32 len,
             const u8 *key, u32 klen,
             u8 *out, u32 out_len) {
    Blake2b_CTX ctx;
    blake2b_init(&ctx, out_len, key, klen);
    blake2b_update(&ctx, in, len);
    blake2b_final(&ctx, out);
}
