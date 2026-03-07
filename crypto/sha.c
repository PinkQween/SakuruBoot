#include "sha.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Portable helpers                                                     */
/* ------------------------------------------------------------------ */
static u32 ror32(u32 x, int n) { return (x >> n) | (x << (32 - n)); }
static u64 ror64(u64 x, int n) { return (x >> n) | (x << (64 - n)); }

static u32 be32(const u8 *p) {
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|(u32)p[3];
}
static u64 be64(const u8 *p) {
    return ((u64)be32(p)<<32)|(u64)be32(p+4);
}
static void put_be32(u8 *p, u32 v) {
    p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v;
}
static void put_be64(u8 *p, u64 v) {
    put_be32(p,(u32)(v>>32)); put_be32(p+4,(u32)v);
}

/* ------------------------------------------------------------------ */
/* SHA-1                                                               */
/* ------------------------------------------------------------------ */
void sha1_init(SHA1_CTX *ctx) {
    ctx->h[0]=0x67452301u; ctx->h[1]=0xEFCDAB89u;
    ctx->h[2]=0x98BADCFEu; ctx->h[3]=0x10325476u;
    ctx->h[4]=0xC3D2E1F0u;
    ctx->len=0; ctx->buf_len=0;
}

static void sha1_block(SHA1_CTX *ctx, const u8 blk[64]) {
    u32 w[80], a,b,c,d,e,f,k,t;
    for (int i=0;i<16;i++) w[i]=be32(blk+i*4);
    for (int i=16;i<80;i++) { u32 x=w[i-3]^w[i-8]^w[i-14]^w[i-16]; w[i]=(x<<1)|(x>>31); }
    a=ctx->h[0]; b=ctx->h[1]; c=ctx->h[2]; d=ctx->h[3]; e=ctx->h[4];
    for (int i=0;i<80;i++) {
        if      (i<20){f=(b&c)|(~b&d);k=0x5A827999u;}
        else if (i<40){f=b^c^d;       k=0x6ED9EBA1u;}
        else if (i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDCu;}
        else          {f=b^c^d;       k=0xCA62C1D6u;}
        t=((a<<5)|(a>>27))+f+e+k+w[i];
        e=d; d=c; c=(b<<30)|(b>>2); b=a; a=t;
    }
    ctx->h[0]+=a; ctx->h[1]+=b; ctx->h[2]+=c; ctx->h[3]+=d; ctx->h[4]+=e;
}

void sha1_update(SHA1_CTX *ctx, const u8 *data, u32 len) {
    ctx->len += len;
    u32 i = 0;
    if (ctx->buf_len) {
        u32 need = 64 - ctx->buf_len;
        u32 take = (len < need) ? len : need;
        for (u32 j=0;j<take;j++) ctx->buf[ctx->buf_len+j]=data[j];
        ctx->buf_len += take; i += take;
        if (ctx->buf_len == 64) { sha1_block(ctx, ctx->buf); ctx->buf_len=0; }
    }
    for (;i+64<=len;i+=64) sha1_block(ctx, data+i);
    for (;i<len;i++) ctx->buf[ctx->buf_len++]=data[i];
}

void sha1_final(SHA1_CTX *ctx, u8 out[20]) {
    u64 bits = ctx->len * 8;
    u8 pad = 0x80;
    sha1_update(ctx, &pad, 1);
    while (ctx->buf_len != 56) { pad=0; sha1_update(ctx, &pad, 1); }
    u8 len_be[8]; put_be64(len_be, bits);
    sha1_update(ctx, len_be, 8);
    for (int i=0;i<5;i++) put_be32(out+i*4, ctx->h[i]);
}

void sha1(const u8 *data, u32 len, u8 out[20]) {
    SHA1_CTX ctx; sha1_init(&ctx); sha1_update(&ctx,data,len); sha1_final(&ctx,out);
}

/* ------------------------------------------------------------------ */
/* SHA-256                                                             */
/* ------------------------------------------------------------------ */
static const u32 K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
    0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
    0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
    0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
    0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,
    0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
};

void sha256_init(SHA256_CTX *ctx) {
    ctx->h[0]=0x6a09e667u; ctx->h[1]=0xbb67ae85u;
    ctx->h[2]=0x3c6ef372u; ctx->h[3]=0xa54ff53au;
    ctx->h[4]=0x510e527fu; ctx->h[5]=0x9b05688cu;
    ctx->h[6]=0x1f83d9abu; ctx->h[7]=0x5be0cd19u;
    ctx->len=0; ctx->buf_len=0;
}

static void sha256_block(SHA256_CTX *ctx, const u8 blk[64]) {
    u32 w[64], a,b,c,d,e,f,g,h,s0,s1,ch,maj,t1,t2;
    for (int i=0;i<16;i++) w[i]=be32(blk+i*4);
    for (int i=16;i<64;i++) {
        s0=ror32(w[i-15],7)^ror32(w[i-15],18)^(w[i-15]>>3);
        s1=ror32(w[i-2],17)^ror32(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    a=ctx->h[0];b=ctx->h[1];c=ctx->h[2];d=ctx->h[3];
    e=ctx->h[4];f=ctx->h[5];g=ctx->h[6];h=ctx->h[7];
    for (int i=0;i<64;i++) {
        s1=ror32(e,6)^ror32(e,11)^ror32(e,25);
        ch=(e&f)^(~e&g);
        t1=h+s1+ch+K256[i]+w[i];
        s0=ror32(a,2)^ror32(a,13)^ror32(a,22);
        maj=(a&b)^(a&c)^(b&c);
        t2=s0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx->h[0]+=a;ctx->h[1]+=b;ctx->h[2]+=c;ctx->h[3]+=d;
    ctx->h[4]+=e;ctx->h[5]+=f;ctx->h[6]+=g;ctx->h[7]+=h;
}

void sha256_update(SHA256_CTX *ctx, const u8 *data, u32 len) {
    ctx->len += len;
    u32 i=0;
    if (ctx->buf_len) {
        u32 need=64-ctx->buf_len, take=(len<need)?len:need;
        for (u32 j=0;j<take;j++) ctx->buf[ctx->buf_len+j]=data[j];
        ctx->buf_len+=take; i+=take;
        if (ctx->buf_len==64){sha256_block(ctx,ctx->buf);ctx->buf_len=0;}
    }
    for (;i+64<=len;i+=64) sha256_block(ctx,data+i);
    for (;i<len;i++) ctx->buf[ctx->buf_len++]=data[i];
}

void sha256_final(SHA256_CTX *ctx, u8 out[32]) {
    u64 bits=ctx->len*8;
    u8 pad=0x80; sha256_update(ctx,&pad,1);
    while(ctx->buf_len!=56){pad=0;sha256_update(ctx,&pad,1);}
    u8 lb[8]; put_be64(lb,bits); sha256_update(ctx,lb,8);
    for(int i=0;i<8;i++) put_be32(out+i*4,ctx->h[i]);
}

void sha256(const u8 *data, u32 len, u8 out[32]) {
    SHA256_CTX ctx; sha256_init(&ctx); sha256_update(&ctx,data,len); sha256_final(&ctx,out);
}

/* ------------------------------------------------------------------ */
/* SHA-512                                                             */
/* ------------------------------------------------------------------ */
static const u64 K512[80] = {
    0x428a2f98d728ae22ull,0x7137449123ef65cdull,0xb5c0fbcfec4d3b2full,0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull,0x59f111f1b605d019ull,0x923f82a4af194f9bull,0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull,0x12835b0145706fbeull,0x243185be4ee4b28cull,0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full,0x80deb1fe3b1696b1ull,0x9bdc06a725c71235ull,0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull,0xefbe4786384f25e3ull,0x0fc19dc68b8cd5b5ull,0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull,0x4a7484aa6ea6e483ull,0x5cb0a9dcbd41fbd4ull,0x76f988da831153b5ull,
    0x983e5152ee66dfabull,0xa831c66d2db43210ull,0xb00327c898fb213full,0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull,0xd5a79147930aa725ull,0x06ca6351e003826full,0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull,0x2e1b21385c26c926ull,0x4d2c6dfc5ac42aedull,0x53380d139d95b3dfull,
    0x650a73548baf63deull,0x766a0abb3c77b2a8ull,0x81c2c92e47edaee6ull,0x92722c851482353bull,
    0xa2bfe8a14cf10364ull,0xa81a664bbc423001ull,0xc24b8b70d0f89791ull,0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull,0xd69906245565a910ull,0xf40e35855771202aull,0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull,0x1e376c085141ab53ull,0x2748774cdf8eeb99ull,0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull,0x4ed8aa4ae3418acbull,0x5b9cca4f7763e373ull,0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull,0x78a5636f43172f60ull,0x84c87814a1f0ab72ull,0x8cc702081a6439ecull,
    0x90befffa23631e28ull,0xa4506cebde82bde9ull,0xbef9a3f7b2c67915ull,0xc67178f2e372532bull,
    0xca273eceea26619cull,0xd186b8c721c0c207ull,0xeada7dd6cde0eb1eull,0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull,0x0a637dc5a2c898a6ull,0x113f9804bef90daeull,0x1b710b35131c471bull,
    0x28db77f523047d84ull,0x32caab7b40c72493ull,0x3c9ebe0a15c9bebcull,0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull,0x597f299cfc657e2aull,0x5fcb6fab3ad6faecull,0x6c44198c4a475817ull,
};

void sha512_init(SHA512_CTX *ctx) {
    ctx->h[0]=0x6a09e667f3bcc908ull; ctx->h[1]=0xbb67ae8584caa73bull;
    ctx->h[2]=0x3c6ef372fe94f82bull; ctx->h[3]=0xa54ff53a5f1d36f1ull;
    ctx->h[4]=0x510e527fade682d1ull; ctx->h[5]=0x9b05688c2b3e6c1full;
    ctx->h[6]=0x1f83d9abfb41bd6bull; ctx->h[7]=0x5be0cd19137e2179ull;
    ctx->len[0]=ctx->len[1]=0; ctx->buf_len=0;
}

static void sha512_block(SHA512_CTX *ctx, const u8 blk[128]) {
    u64 w[80],a,b,c,d,e,f,g,h,s0,s1,ch,maj,t1,t2;
    for(int i=0;i<16;i++) w[i]=be64(blk+i*8);
    for(int i=16;i<80;i++){
        s0=ror64(w[i-15],1)^ror64(w[i-15],8)^(w[i-15]>>7);
        s1=ror64(w[i-2],19)^ror64(w[i-2],61)^(w[i-2]>>6);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    a=ctx->h[0];b=ctx->h[1];c=ctx->h[2];d=ctx->h[3];
    e=ctx->h[4];f=ctx->h[5];g=ctx->h[6];h=ctx->h[7];
    for(int i=0;i<80;i++){
        s1=ror64(e,14)^ror64(e,18)^ror64(e,41);
        ch=(e&f)^(~e&g);
        t1=h+s1+ch+K512[i]+w[i];
        s0=ror64(a,28)^ror64(a,34)^ror64(a,39);
        maj=(a&b)^(a&c)^(b&c);
        t2=s0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx->h[0]+=a;ctx->h[1]+=b;ctx->h[2]+=c;ctx->h[3]+=d;
    ctx->h[4]+=e;ctx->h[5]+=f;ctx->h[6]+=g;ctx->h[7]+=h;
}

void sha512_update(SHA512_CTX *ctx, const u8 *data, u32 len) {
    u64 old=ctx->len[0]; ctx->len[0]+=len;
    if(ctx->len[0]<old) ctx->len[1]++;
    u32 i=0;
    if(ctx->buf_len){
        u32 need=128-ctx->buf_len,take=(len<need)?len:need;
        for(u32 j=0;j<take;j++) ctx->buf[ctx->buf_len+j]=data[j];
        ctx->buf_len+=take; i+=take;
        if(ctx->buf_len==128){sha512_block(ctx,ctx->buf);ctx->buf_len=0;}
    }
    for(;i+128<=len;i+=128) sha512_block(ctx,data+i);
    for(;i<len;i++) ctx->buf[ctx->buf_len++]=data[i];
}

void sha512_final(SHA512_CTX *ctx, u8 out[64]) {
    /* total bits = len[1]:len[0] * 8 */
    u64 bhi=(ctx->len[1]<<3)|(ctx->len[0]>>61), blo=ctx->len[0]<<3;
    u8 pad=0x80; sha512_update(ctx,&pad,1);
    while(ctx->buf_len!=112){pad=0;sha512_update(ctx,&pad,1);}
    u8 lb[16]; put_be64(lb,bhi); put_be64(lb+8,blo);
    sha512_update(ctx,lb,16);
    for(int i=0;i<8;i++) put_be64(out+i*8,ctx->h[i]);
}

void sha512(const u8 *data, u32 len, u8 out[64]) {
    SHA512_CTX ctx; sha512_init(&ctx); sha512_update(&ctx,data,len); sha512_final(&ctx,out);
}

/* ------------------------------------------------------------------ */
/* Dispatch helpers                                                    */
/* ------------------------------------------------------------------ */
static int streq(const char *a, const char *b) {
    while(*a&&*b&&*a==*b){a++;b++;} return *a==*b;
}
int hash_id(const char *name) {
    if(streq(name,"sha1"))   return HASH_SHA1;
    if(streq(name,"sha256")) return HASH_SHA256;
    if(streq(name,"sha512")) return HASH_SHA512;
    return -1;
}
int hash_digest_size(int id) {
    if(id==HASH_SHA1)   return 20;
    if(id==HASH_SHA256) return 32;
    if(id==HASH_SHA512) return 64;
    return 0;
}
void hash_compute(int id, const u8 *data, u32 len, u8 *out) {
    if(id==HASH_SHA1)   { sha1  (data,len,out); return; }
    if(id==HASH_SHA256) { sha256(data,len,out); return; }
    if(id==HASH_SHA512) { sha512(data,len,out); return; }
}
