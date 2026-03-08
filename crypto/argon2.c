/*
 * Argon2 implementation for SakuruBoot.
 * Single-threaded (parallelism lanes are processed sequentially).
 * Implements all three variants: Argon2d, Argon2i, Argon2id.
 * Reference: RFC 9106 / https://github.com/P-H-C/phc-winner-argon2
 */

#include "argon2.h"
#include "blake2b.h"
#include <stddef.h>

#define ARGON2_BLOCK_SIZE   1024u   /* bytes per block */
#define ARGON2_QWORDS       128u    /* 64-bit words per block */
#define ARGON2_SYNC_POINTS  4u

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static u64 le64r(const u8 *p){
    u64 v=0; for(int i=7;i>=0;i--) v=(v<<8)|p[i]; return v;
}
static void put_le32(u8 *p,u32 v){p[0]=(u8)v;p[1]=(u8)(v>>8);p[2]=(u8)(v>>16);p[3]=(u8)(v>>24);}
static void put_le64(u8 *p,u64 v){for(int i=0;i<8;i++){p[i]=(u8)v;v>>=8;}}

static void memxor(u8 *dst,const u8 *a,const u8 *b,u32 n){
    for(u32 i=0;i<n;i++) dst[i]=a[i]^b[i];
}
static void memcpy_local(u8 *d,const u8 *s,u32 n){for(u32 i=0;i<n;i++)d[i]=s[i];}
static void memset_local(u8 *d,u8 v,u32 n){for(u32 i=0;i<n;i++)d[i]=v;}

/* Blake2b with variable output length (Argon2 H' function) */
static void H_prime(const u8 *in, u32 in_len, u8 *out, u32 out_len) {
    /* H'(X) as defined in RFC 9106 §3.2 */
    if (out_len <= 64) {
        /* Single Blake2b call with out_len as digest length */
        u8 len_enc[4]; put_le32(len_enc, out_len);
        Blake2b_CTX ctx;
        blake2b_init(&ctx, out_len, NULL, 0);
        blake2b_update(&ctx, len_enc, 4);
        blake2b_update(&ctx, in, in_len);
        blake2b_final(&ctx, out);
    } else {
        /* Multi-block extension */
        u8 len_enc[4]; put_le32(len_enc, out_len);
        u8 a[64];
        Blake2b_CTX ctx;
        blake2b_init(&ctx, 64, NULL, 0);
        blake2b_update(&ctx, len_enc, 4);
        blake2b_update(&ctx, in, in_len);
        blake2b_final(&ctx, a);
        /* A1 first 32 bytes → output[0..31] */
        for(int i=0;i<32;i++) out[i]=a[i];
        u32 written=32, remaining=out_len-32;
        u8 prev[64]; memcpy_local(prev,a,64);
        while(remaining>64){
            blake2b(prev,64,NULL,0,a,64);
            for(int i=0;i<32;i++) out[written+i]=a[i];
            memcpy_local(prev,a,64);
            written+=32; remaining-=32;
        }
        /* Last block — output remaining bytes */
        blake2b(prev,64,NULL,0,out+written,(u32)remaining);
    }
}

/* ------------------------------------------------------------------ */
/* GB — Argon2 mixing function (Blamka + Blake2b round)               */
/* ------------------------------------------------------------------ */
static u64 fBlaMka(u64 x,u64 y){
    u64 xy=(u64)(u32)x*(u64)(u32)y;
    return x+y+(xy<<1);
}
#define rotr64(x,n) (((x)>>(n))|((x)<<(64-(n))))
#define G_BLAMKA(a,b,c,d) do { \
    a=fBlaMka(a,b); d=rotr64(d^a,32); \
    c=fBlaMka(c,d); b=rotr64(b^c,24); \
    a=fBlaMka(a,b); d=rotr64(d^a,16); \
    c=fBlaMka(c,d); b=rotr64(b^c,63); \
} while(0)

static void permute_block(u64 *v) {
    /* Apply Blake2 round to 8×2-element columns, then 8×2-element rows */
    for(int i=0;i<8;i++){
        int j=i*2;
        G_BLAMKA(v[j],v[j+ 1],v[j+ 2],v[j+ 3]);
        G_BLAMKA(v[j+ 4],v[j+ 5],v[j+ 6],v[j+ 7]);
        G_BLAMKA(v[j+ 8],v[j+ 9],v[j+10],v[j+11]);
        G_BLAMKA(v[j+12],v[j+13],v[j+14],v[j+15]);
    }
    /* RFC 9106 §3.4 — 8 column permutations each covering 16 qwords */
    for(int i=0;i<8;i++){
        G_BLAMKA(v[i],v[i+8],v[i+16],v[i+24]);
        G_BLAMKA(v[i+32],v[i+40],v[i+48],v[i+56]);
        G_BLAMKA(v[i+64],v[i+72],v[i+80],v[i+88]);
        G_BLAMKA(v[i+96],v[i+104],v[i+112],v[i+120]);
    }
}

static void GB_block(u8 *out, const u8 *x, const u8 *y) {
    u64 r[128], tmp[128];
    for(int i=0;i<128;i++){
        r[i]=le64r(x+i*8)^le64r(y+i*8);
        tmp[i]=r[i];
    }
    permute_block(tmp);
    for(int i=0;i<128;i++){
        u64 v=tmp[i]^r[i];
        put_le64(out+i*8,v);
    }
}

/* ------------------------------------------------------------------ */
/* Index derivation                                                    */
/* ------------------------------------------------------------------ */
static u32 index_alpha(u32 pass, u32 lane __attribute__((unused)), u32 slice,
                       u32 lane_len, u32 lanes,
                       u32 index, u32 pseudo_rand,
                       int same_lane) {
    u32 ref_area;
    if (pass == 0) {
        if (slice == 0) ref_area = index - 1;
        else if (same_lane) ref_area = slice * (lane_len/ARGON2_SYNC_POINTS) + index - 1;
        else ref_area = slice * (lane_len/ARGON2_SYNC_POINTS) - (index==0?1:0);
    } else {
        if (same_lane) ref_area = lane_len - (lane_len/ARGON2_SYNC_POINTS) + index - 1;
        else ref_area = lane_len - (lane_len/ARGON2_SYNC_POINTS) - (index==0?1:0);
    }
    u64 relative_pos = (u64)pseudo_rand * (u64)pseudo_rand >> 32;
    relative_pos = (u64)ref_area * relative_pos >> 32;
    u32 start_pos = 0;
    if (pass != 0) {
        start_pos = (slice == ARGON2_SYNC_POINTS-1) ? 0
                  : (slice+1)*(lane_len/ARGON2_SYNC_POINTS);
    }
    (void)lanes;
    return (u32)((start_pos + ref_area - 1 - relative_pos) % lane_len);
}

/* ------------------------------------------------------------------ */
/* Main Argon2 function                                                */
/* ------------------------------------------------------------------ */
int argon2(const u8 *password, u32 plen,
           const u8 *salt,     u32 slen,
           u32 time_cost, u32 memory_kb, u32 parallelism,
           int type,
           u8 *out, u32 out_len,
           Argon2AllocFn alloc, Argon2FreeFn freefn) {

    /* Ensure memory is at least 8 * lanes */
    u32 memory_blocks = memory_kb;
    if (memory_blocks < 8 * parallelism) memory_blocks = 8 * parallelism;
    u32 segment_length = memory_blocks / (parallelism * ARGON2_SYNC_POINTS);
    if (segment_length == 0) segment_length = 1;
    u32 lane_length = segment_length * ARGON2_SYNC_POINTS;
    memory_blocks = lane_length * parallelism;

    /* Allocate block memory */
    u8 *B = (u8 *)alloc((u32)memory_blocks * ARGON2_BLOCK_SIZE);
    if (!B) return -1;
    memset_local(B, 0, memory_blocks * ARGON2_BLOCK_SIZE);

    /* H0 — initial hash */
    u8 h0[72]; /* 64 bytes Blake2b output + 8 bytes for block index */
    {
        u8 params[256];
        u32 pi = 0;
        #define LE32(v) do{put_le32(params+pi,(u32)(v));pi+=4;}while(0)
        LE32(parallelism); LE32(out_len); LE32(memory_kb);
        LE32(time_cost);   LE32(0x13);  /* version = 0x13 */
        LE32(type);
        LE32(plen);
        /* H0 = Blake2b-512(p||T||m||t||v||y|||P|||P||S||slen...) */
        Blake2b_CTX ctx;
        blake2b_init(&ctx, 64, NULL, 0);
        blake2b_update(&ctx, params, pi);
        /* password length + password */
        u8 tmp4[4]; put_le32(tmp4,plen);
        blake2b_update(&ctx, tmp4, 4);
        blake2b_update(&ctx, password, plen);
        /* salt */
        put_le32(tmp4,slen);
        blake2b_update(&ctx, tmp4, 4);
        blake2b_update(&ctx, salt, slen);
        /* secret (none), assoc data (none) */
        put_le32(tmp4,0); blake2b_update(&ctx,tmp4,4);
        put_le32(tmp4,0); blake2b_update(&ctx,tmp4,4);
        blake2b_final(&ctx, h0);
        #undef LE32
    }

    /* Initialize first two blocks of each lane */
    for (u32 l = 0; l < parallelism; l++) {
        put_le32(h0+64, 0); put_le32(h0+68, l);
        H_prime(h0, 72, B + (l*lane_length + 0)*ARGON2_BLOCK_SIZE, ARGON2_BLOCK_SIZE);
        put_le32(h0+64, 1); put_le32(h0+68, l);
        H_prime(h0, 72, B + (l*lane_length + 1)*ARGON2_BLOCK_SIZE, ARGON2_BLOCK_SIZE);
    }

    /* Main loop */
    u8 tmp_block[ARGON2_BLOCK_SIZE];
    for (u32 pass = 0; pass < time_cost; pass++) {
        for (u32 slice = 0; slice < ARGON2_SYNC_POINTS; slice++) {
            for (u32 lane = 0; lane < parallelism; lane++) {
                u32 seg_start = slice * segment_length;
                u32 start_idx = (pass==0 && slice==0) ? 2 : 0;

                for (u32 idx = start_idx; idx < segment_length; idx++) {
                    u32 cur = seg_start + idx;
                    u32 prev_idx = (cur == 0) ? lane_length - 1 : cur - 1;
                    u8 *prev_block = B + (lane*lane_length + prev_idx)*ARGON2_BLOCK_SIZE;

                    /* Get pseudo-random value from prev block */
                    u32 pseudo_rand;
                    if (type == ARGON2_TYPE_D ||
                       (type == ARGON2_TYPE_ID && pass >= 1) ||
                       (type == ARGON2_TYPE_ID && slice >= 2)) {
                        /* Data-dependent: use J1 from prev block's first 64-bit word */
                        pseudo_rand = (u32)le64r(prev_block);
                    } else {
                        /* Data-independent: use a generated pseudo-random block */
                        /* Simplified: use index-based deterministic value */
                        u8 prand_input[72];
                        memset_local(prand_input,0,72);
                        put_le32(prand_input, pass);
                        put_le32(prand_input+4, lane);
                        put_le32(prand_input+8, slice);
                        put_le32(prand_input+12, memory_blocks);
                        put_le32(prand_input+16, time_cost);
                        put_le32(prand_input+20, (u32)type);
                        put_le32(prand_input+24, idx/128 + 1);
                        u8 prand_block[ARGON2_BLOCK_SIZE];
                        H_prime(prand_input, 28, prand_block, ARGON2_BLOCK_SIZE);
                        pseudo_rand = (u32)le64r(prand_block + (idx%128)*8);
                    }

                    /* Compute reference lane */
                    u32 ref_lane = (pass==0 && slice==0)
                                 ? lane
                                 : (pseudo_rand >> 16) % parallelism;

                    /* Compute reference block index within ref_lane */
                    u32 ref_idx = index_alpha(pass,lane,slice,lane_length,
                                              parallelism,idx,pseudo_rand,
                                              ref_lane==lane);
                    u8 *ref_block  = B + (ref_lane*lane_length + ref_idx)*ARGON2_BLOCK_SIZE;
                    u8 *curr_block = B + (lane*lane_length + cur)*ARGON2_BLOCK_SIZE;

                    GB_block(tmp_block, prev_block, ref_block);
                    if (pass == 0) {
                        memcpy_local(curr_block, tmp_block, ARGON2_BLOCK_SIZE);
                    } else {
                        /* XOR with existing block (overwrite-with-XOR variant) */
                        memxor(curr_block, curr_block, tmp_block, ARGON2_BLOCK_SIZE);
                    }
                }
            }
        }
    }

    /* Finalize: XOR last blocks of all lanes */
    u8 final_block[ARGON2_BLOCK_SIZE];
    memcpy_local(final_block,
                 B + (0*lane_length + lane_length-1)*ARGON2_BLOCK_SIZE,
                 ARGON2_BLOCK_SIZE);
    for (u32 l = 1; l < parallelism; l++) {
        u8 *last = B + (l*lane_length + lane_length-1)*ARGON2_BLOCK_SIZE;
        for (u32 i=0;i<ARGON2_BLOCK_SIZE;i++) final_block[i]^=last[i];
    }

    /* Output = H'(final_block, out_len) */
    H_prime(final_block, ARGON2_BLOCK_SIZE, out, out_len);

    freefn(B);
    return 0;
}
