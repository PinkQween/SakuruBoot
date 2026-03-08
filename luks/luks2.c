/*
 * LUKS2 unlock.
 *
 * LUKS2 stores two 4096-byte binary headers at offset 0 and 16384 (or 1 MiB),
 * followed by a variable-length JSON metadata area.
 *
 * We implement a minimal JSON parser sufficient to extract:
 *   keyslots[N].type        "luks2"
 *   keyslots[N].kdf.type    "argon2id" | "argon2i" | "pbkdf2"
 *   keyslots[N].kdf.salt    base64-encoded 32-byte salt
 *   keyslots[N].kdf.time    Argon2 time cost
 *   keyslots[N].kdf.memory  Argon2 memory (KB)
 *   keyslots[N].kdf.cpus    Argon2 parallelism
 *   keyslots[N].kdf.hash    PBKDF2 hash spec
 *   keyslots[N].kdf.iterations PBKDF2 iterations
 *   keyslots[N].af.stripes  AF stripes (default 4000)
 *   keyslots[N].area.offset key material offset (bytes)
 *   keyslots[N].area.size   key material size (bytes)
 *   keyslots[N].key_size    derived key size (bytes)
 *   digests[N].type         "pbkdf2"
 *   digests[N].keyslots     list of keyslot IDs covered
 *   digests[N].salt         base64
 *   digests[N].digest       base64
 *   digests[N].hash         hash spec
 *   digests[N].iterations   iterations
 *   segments[0].offset      payload offset in bytes
 *   segments[0].encryption  cipher spec e.g. "aes-xts-plain64"
 *   segments[0].sector_size sector size in bytes
 */

#include "luks2.h"
#include "../crypto/pbkdf2.h"
#include "../crypto/argon2.h"
#include "../crypto/sha.h"
#include "../crypto/cipher.h"
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static u64 le64r_l(const u8 *p){
    u64 v=0;for(int i=7;i>=0;i--)v=(v<<8)|p[i];return v;
}
static __attribute__((unused)) u64 be64r_l(const u8 *p){
    u64 v=0;for(int i=0;i<8;i++)v=(v<<8)|p[i];return v;
}

static int streqn(const char *a,const char *b,int n){
    for(int i=0;i<n;i++) if(a[i]!=b[i]) return 0;
    return 1;
}
static int strfind(const char *hay,int hlen,const char *needle,int nlen){
    for(int i=0;i<=hlen-nlen;i++){
        if(streqn(hay+i,needle,nlen)) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Base64 decode (RFC 4648 standard alphabet, no padding required)    */
/* ------------------------------------------------------------------ */
static int b64_val(char c){
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='a'&&c<='z') return c-'a'+26;
    if(c>='0'&&c<='9') return c-'0'+52;
    if(c=='+') return 62;
    if(c=='/') return 63;
    return -1;
}
static u32 base64_decode(const char *src,u32 slen,u8 *dst){
    u32 out=0; int buf=0,bits=0;
    for(u32 i=0;i<slen;i++){
        int v=b64_val(src[i]); if(v<0) continue;
        buf=(buf<<6)|v; bits+=6;
        if(bits>=8){bits-=8;dst[out++]=(u8)(buf>>bits);}
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Minimal JSON value extractor                                        */
/*   json_get_str(json, jlen, key, out, out_max) → length of value    */
/*   json_get_int(json, jlen, key) → integer value or 0               */
/* Only handles flat key lookup (no recursion needed here).           */
/* ------------------------------------------------------------------ */

/* Find "key": and return pointer to the value start and its length */
static const char *json_find_key(const char *j, int jlen, const char *key,
                                  int *val_len) {
    int klen = 0; while(key[klen]) klen++;
    char needle[64]; int nlen=0;
    needle[nlen++]='"';
    for(int i=0;i<klen;i++) needle[nlen++]=key[i];
    needle[nlen++]='"';
    needle[nlen++]=':';
    int pos=strfind(j,jlen,needle,nlen);
    if(pos<0) return NULL;
    const char *v=j+pos+nlen;
    int rem=jlen-(pos+nlen);
    /* skip whitespace */
    while(rem>0&&(*v==' '||*v=='\t'||*v=='\n'||*v=='\r')){v++;rem--;}
    if(rem<=0) return NULL;
    if(*v=='"'){
        /* string value */
        v++; rem--;
        const char *start=v;
        int len=0;
        while(rem>0&&*v!='"'){v++;rem--;len++;}
        *val_len=len;
        return start;
    } else if(*v>='0'&&*v<='9'){
        const char *start=v;
        int len=0;
        while(rem>0&&*v>='0'&&*v<='9'){v++;rem--;len++;}
        *val_len=len;
        return start;
    }
    return NULL;
}

static int json_get_int(const char *j,int jlen,const char *key){
    int vlen=0;
    const char *v=json_find_key(j,jlen,key,&vlen);
    if(!v) return 0;
    int n=0; for(int i=0;i<vlen;i++) n=n*10+(v[i]-'0');
    return n;
}

static int json_get_str(const char *j,int jlen,const char *key,
                         char *out,int out_max){
    int vlen=0;
    const char *v=json_find_key(j,jlen,key,&vlen);
    if(!v) return 0;
    int take=vlen<out_max-1?vlen:out_max-1;
    for(int i=0;i<take;i++) out[i]=v[i];
    out[take]=0;
    return take;
}

/* ------------------------------------------------------------------ */
/* LUKS2 binary header                                                 */
/* ------------------------------------------------------------------ */
#define LUKS2_HDR_SIZE_FIELD_OFFSET  8   /* u64 LE: total metadata size */
#define LUKS2_HDR_JSON_OFF_OFFSET   24   /* u64 LE: offset of JSON area */
#define LUKS2_HDR_JSON_SIZE_OFFSET  32   /* u64 LE: size of JSON area */
#define LUKS2_HDR_SEQID_OFFSET       0   /* actually starts at 0 after magic+version */

int luks2_probe(const u8 *buf){
    return (buf[0]=='L'&&buf[1]=='U'&&buf[2]=='K'&&buf[3]=='S'&&
            buf[4]==0xba&&buf[5]==0xbe) ||
           (buf[0]=='S'&&buf[1]=='K'&&buf[2]=='U'&&buf[3]=='L'&&
            buf[4]==0xba&&buf[5]==0xbe);
}

/* ------------------------------------------------------------------ */
/* UEFI memory helpers (provided via argon2 alloc/free interface)      */
/* ------------------------------------------------------------------ */
extern void *gBS_alloc_pool(u32 size);
extern void  gBS_free_pool(void *p);

/* ------------------------------------------------------------------ */
/* AF merge (same as LUKS1)                                            */
/* ------------------------------------------------------------------ */
static void af2_hash_sector(int hid,u32 sector,u32 key_bytes,
                             const u8 *data,u8 *out){
    u8 tmp[256+4];
    for(u32 i=0;i<key_bytes;i++) tmp[i]=data[i];
    tmp[key_bytes+0]=(u8)(sector>>24); tmp[key_bytes+1]=(u8)(sector>>16);
    tmp[key_bytes+2]=(u8)(sector>>8);  tmp[key_bytes+3]=(u8)(sector);
    hash_compute(hid,tmp,key_bytes+4,out);
}
static void af2_merge(int hid,const u8 *split,u32 stripes,
                      u32 key_bytes,u8 *mk){
    u8 d[64]={0};
    for(u32 s=0;s<stripes-1;s++){
        for(u32 i=0;i<key_bytes;i++) d[i]^=split[s*key_bytes+i];
        af2_hash_sector(hid,s,key_bytes,d,d);
    }
    for(u32 i=0;i<key_bytes;i++) mk[i]=d[i]^split[(stripes-1)*key_bytes+i];
}

/* ------------------------------------------------------------------ */
/* Static scratch                                                      */
/* ------------------------------------------------------------------ */
#define LUKS2_KM_BUF  (256*512)
static u8 s2_enc[LUKS2_KM_BUF];
static u8 s2_dec[LUKS2_KM_BUF];
static u8 s2_dk [64];
static char s2_json[65536]; /* up to 64 KiB JSON */

/* ------------------------------------------------------------------ */
/* Main LUKS2 unlock                                                   */
/* ------------------------------------------------------------------ */
int luks2_unlock(LuksReadFn read_fn, void *read_ctx,
                 const u8 *passphrase, u32 plen,
                 u8 *master_key_out,
                 u32 *key_bytes_out,
                 u64 *payload_offset_out,
                 int *cipher_mode_out) {

    /* Read first 8 sectors (4 KiB header + start of JSON area) */
    u8 hdr[512*8];
    if (read_fn(read_ctx, 0, hdr, 8) != 0) return -1;
    if (!luks2_probe(hdr)) return -1;

    /* Binary header is 4096 bytes; JSON offset and size are in the header */
    u64 json_off  = le64r_l(hdr + 24); /* bytes */
    u64 json_size = le64r_l(hdr + 32); /* bytes */
    if (json_size > sizeof(s2_json)-1) json_size = sizeof(s2_json)-1;

    /* Read JSON area */
    u64 json_lba = json_off / 512;
    u32 json_secs = (u32)((json_size + 511) / 512);
    if (json_secs > 128) json_secs = 128;
    if (read_fn(read_ctx, json_lba, (u8*)s2_json, json_secs) != 0) return -1;
    s2_json[json_size] = 0;

    /* Parse segment 0 to get payload offset and cipher */
    char seg_enc[64]={0};
    json_get_str(s2_json, (int)json_size, "encryption", seg_enc, sizeof(seg_enc));
    int payload_sector_size = json_get_int(s2_json, (int)json_size, "sector_size");
    if (payload_sector_size == 0) payload_sector_size = 512;

    /* segment offset — find "segments" block */
    int seg_off_val = 0;
    {
        int vl=0;
        const char *v=json_find_key(s2_json,(int)json_size,"offset",&vl);
        if(v) { for(int i=0;i<vl;i++) seg_off_val=seg_off_val*10+(v[i]-'0'); }
    }

    /* Parse cipher mode from seg_enc e.g. "aes-xts-plain64" */
    int cmode = CIPHER_XTS;
    {
        /* Find mode portion after second '-' */
        int dashes=0, mode_start=-1;
        for(int i=0;seg_enc[i];i++){
            if(seg_enc[i]=='-'){dashes++;if(dashes==2){mode_start=i+1;break;}}
        }
        if(mode_start>=0) cmode=cipher_mode_id(seg_enc+mode_start);
    }

    /* Try each keyslot (0..31) */
    for (int slot = 0; slot < 32; slot++) {
        int vl=0;
        /* Quick check: does this keyslot exist? */
        if(!json_find_key(s2_json,(int)json_size,"key_size",&vl)) {
            /* No key_size field at all — no keyslots */
            break;
        }

        /* Extract kdf type */
        char kdf_type[32]={0};
        json_get_str(s2_json,(int)json_size,"type",kdf_type,sizeof(kdf_type));

        /* KDF salt */
        char salt_b64[128]={0};
        json_get_str(s2_json,(int)json_size,"salt",salt_b64,sizeof(salt_b64));
        u8 kdf_salt[64]={0};
        u32 salt_len=base64_decode(salt_b64,(u32)(sizeof(salt_b64)),kdf_salt);
        if(salt_len==0) { salt_len=32; }

        int key_size=json_get_int(s2_json,(int)json_size,"key_size");
        if(key_size<=0||key_size>64) key_size=32;

        /* Derive key */
        if(kdf_type[0]=='a'){ /* argon2i or argon2id */
            int a2_type = (kdf_type[6]=='d') ? ARGON2_TYPE_ID : ARGON2_TYPE_I;
            u32 t_cost  = (u32)json_get_int(s2_json,(int)json_size,"time");
            u32 m_cost  = (u32)json_get_int(s2_json,(int)json_size,"memory");
            u32 cpus    = (u32)json_get_int(s2_json,(int)json_size,"cpus");
            if(t_cost==0) t_cost=3;
            if(m_cost==0) m_cost=65536;
            if(cpus==0)   cpus=4;
            if(argon2(passphrase,plen,kdf_salt,salt_len,
                      t_cost,m_cost,cpus,a2_type,
                      s2_dk,(u32)key_size,
                      gBS_alloc_pool,gBS_free_pool)!=0) continue;
        } else {
            /* PBKDF2 */
            char hash_spec[32]={0};
            json_get_str(s2_json,(int)json_size,"hash",hash_spec,sizeof(hash_spec));
            int hid=hash_id(hash_spec); if(hid<0) hid=HASH_SHA256;
            u32 iters=(u32)json_get_int(s2_json,(int)json_size,"iterations");
            if(iters==0) iters=100000;
            pbkdf2(hid,passphrase,plen,kdf_salt,salt_len,iters,s2_dk,(u32)key_size);
        }

        /* Read and decrypt key material */
        int km_offset_int=json_get_int(s2_json,(int)json_size,"offset");
        int km_size_int  =json_get_int(s2_json,(int)json_size,"size");
        if(km_offset_int<=0||km_size_int<=0) continue;

        u64 km_lba=(u64)km_offset_int/512;
        u32 km_secs=((u32)km_size_int+511)/512;
        if(km_secs*512>LUKS2_KM_BUF) continue;
        if(read_fn(read_ctx,km_lba,s2_enc,km_secs)!=0) continue;

        /* Decrypt key material with XTS using derived key */
        XTS_CTX xts;
        xts_init(&xts,s2_dk,(int)key_size*2,1);
        for(u32 off=0;off<(u32)km_size_int;off+=512){
            u32 chunk=((u32)km_size_int-off<512)?(u32)km_size_int-off:512;
            u32 aligned=(chunk+15)&~15u;
            xts_crypt(&xts,(u64)(off/512),s2_enc+off,s2_dec+off,aligned,1);
        }

        /* AF merge */
        int stripes=json_get_int(s2_json,(int)json_size,"stripes");
        if(stripes<=0) stripes=4000;
        u8 candidate_mk[64]={0};
        af2_merge(HASH_SHA256,s2_dec,(u32)stripes,(u32)key_size,candidate_mk);

        /* Verify digest */
        char dig_salt_b64[128]={0},dig_b64[128]={0},dig_hash[32]={0};
        json_get_str(s2_json,(int)json_size,"salt",dig_salt_b64,sizeof(dig_salt_b64));
        json_get_str(s2_json,(int)json_size,"digest",dig_b64,sizeof(dig_b64));
        json_get_str(s2_json,(int)json_size,"hash",dig_hash,sizeof(dig_hash));
        u8 dig_salt[64]={0}; u32 dslen=base64_decode(dig_salt_b64,sizeof(dig_salt_b64),dig_salt);
        u8 expected_dig[64]={0}; base64_decode(dig_b64,sizeof(dig_b64),expected_dig);
        int dhid=hash_id(dig_hash); if(dhid<0) dhid=HASH_SHA256;
        int dig_iters=json_get_int(s2_json,(int)json_size,"iterations");
        if(dig_iters<=0) dig_iters=100000;
        int dsize=hash_digest_size(dhid);
        u8 computed_dig[64]={0};
        pbkdf2(dhid,candidate_mk,(u32)key_size,dig_salt,dslen,(u32)dig_iters,computed_dig,(u32)dsize);
        int match=1;
        for(int i=0;i<dsize;i++) if(computed_dig[i]!=expected_dig[i]){match=0;break;}
        if(!match) continue;

        /* Success */
        for(int i=0;i<key_size;i++) master_key_out[i]=candidate_mk[i];
        *key_bytes_out=(u32)key_size;
        *payload_offset_out=(u64)seg_off_val/512;
        *cipher_mode_out=cmode;
        return 0;
    }
    return -1;
}
