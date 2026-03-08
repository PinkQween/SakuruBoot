/*
 * Minimal ext4 reader for SakuruBoot UEFI.
 *
 * Reads ext4 partitions via EFI_DISK_IO_PROTOCOL (byte-granular access).
 * Supports: extent tree depth 0-1, linear directory scan, path traversal.
 * Does NOT support: htree directories, inline data, extent depth > 1,
 *                   indirect block addressing beyond 12 direct blocks.
 */

#include "ext4.h"
#include "../common/types.h"
#include <stddef.h>

/* gBS is defined in uefi_loader.c and linked together */
extern EFI_BOOT_SERVICES *gBS;

/* ------------------------------------------------------------------ */
/* ext4 on-disk constants                                              */
/* ------------------------------------------------------------------ */

#define EXT4_SUPER_MAGIC            0xEF53u
#define EXT4_SUPERBLOCK_OFFSET      1024ull  /* bytes from partition start */
#define EXT4_ROOT_INO               2u
#define EXT4_EXTENTS_FL             0x00080000u
#define EXT4_EXTENT_HEADER_MAGIC    0xF30Au
#define EXT4_FT_DIR                 2u
#define EXT4_FEATURE_INCOMPAT_64BIT 0x0080u

/* Superblock field byte offsets (within the 1024-byte superblock) */
#define SB_FIRST_DATA_BLOCK  20u   /* u32 — usually 0 (4K) or 1 (1K blocks) */
#define SB_LOG_BLOCK_SIZE    24u   /* u32 — block_size = 1024 << this */
#define SB_INODES_PER_GROUP  40u   /* u32 */
#define SB_MAGIC             56u   /* u16 — must be 0xEF53 */
#define SB_INODE_SIZE        88u   /* u16 — bytes per inode (≥128) */
#define SB_FEATURE_INCOMPAT  96u   /* u32 */
#define SB_DESC_SIZE        254u   /* u16 — block group descriptor size (32 or 64) */

/* Block group descriptor field offsets */
#define BGD_INODE_TABLE_LO   8u    /* u32 — low 32 bits of inode table block */
#define BGD_INODE_TABLE_HI  40u    /* u32 — high 32 bits (only for 64-bit BGD) */

/* Inode field offsets */
#define IN_SIZE_LO    4u           /* u32 — low 32 bits of file size */
#define IN_FLAGS     32u           /* u32 — inode flags (EXT4_EXTENTS_FL etc.) */
#define IN_BLOCK     40u           /* u32[15] — extent tree root or block map */
#define IN_SIZE_HIGH 108u          /* u32 — high 32 bits of size (regular files) */

/* Extent header field offsets (12-byte header before each extent array) */
#define EH_MAGIC    0u             /* u16 — must be 0xF30A */
#define EH_ENTRIES  2u             /* u16 — number of valid entries */
#define EH_DEPTH    6u             /* u16 — 0 = leaf, >0 = index */

/* Extent leaf entry offsets (12 bytes each) */
#define EX_BLOCK     0u            /* u32 — first logical block */
#define EX_LEN       4u            /* u16 — number of blocks */
#define EX_START_HI  6u            /* u16 — high 16 bits of physical block */
#define EX_START_LO  8u            /* u32 — low 32 bits of physical block */

/* Extent index entry offsets (12 bytes each) */
#define EI_BLOCK    0u             /* u32 — first logical block covered */
#define EI_LEAF_LO  4u             /* u32 — low 32 bits of child block */
#define EI_LEAF_HI  8u             /* u16 — high 16 bits of child block */

/* Directory entry field offsets */
#define DE_INODE     0u            /* u32 */
#define DE_REC_LEN   4u            /* u16 */
#define DE_NAME_LEN  6u            /* u8 */
#define DE_FILE_TYPE 7u            /* u8 */
#define DE_NAME      8u            /* char[name_len], not null-terminated */

/* ------------------------------------------------------------------ */
/* Little-endian field accessors (no alignment requirements)           */
/* ------------------------------------------------------------------ */

static inline uint16_t r16(const void *p, unsigned off) {
    const uint8_t *b = (const uint8_t *)p + off;
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}

static inline uint32_t r32(const void *p, unsigned off) {
    const uint8_t *b = (const uint8_t *)p + off;
    return (uint32_t)( b[0]
                     | ((uint32_t)b[1] << 8)
                     | ((uint32_t)b[2] << 16)
                     | ((uint32_t)b[3] << 24));
}

/* ------------------------------------------------------------------ */
/* Volume handle                                                       */
/* ------------------------------------------------------------------ */

struct Ext4Vol {
    EFI_DISK_IO_PROTOCOL  *dio;       /* preferred: byte-granular (may be NULL) */
    EFI_BLOCK_IO_PROTOCOL *bio;       /* fallback: always present */
    UINT32  media_id;
    UINT32  phys_blk_sz;              /* device physical sector size (for bio fallback) */
    UINT32  block_size;         /* ext4 block size in bytes (1024/2048/4096) */
    UINT32  inodes_per_group;
    UINT32  inode_size;         /* bytes per inode (always >= 128) */
    UINT32  first_data_block;   /* 0 for 4K blocks, 1 for 1K blocks */
    UINT64  bgdt_off;           /* byte offset of block group descriptor table */
    UINT32  desc_size;          /* bytes per block group descriptor (32 or 64) */
    EFI_HANDLE part_handle;     /* partition handle (NULL for LUKS-backed volumes) */
    /* LUKS-backed volume — when non-NULL, read_bytes goes through luks_vol_read */
    struct LuksVol *luks_vol;
};

/*
 * Read byte_count bytes from byte_off into buf.
 * Uses DiskIO if available, otherwise falls back to block-aligned BlockIO reads.
 * If vol->luks_vol is set, reads through the LUKS decryption layer instead.
 */
static EFI_STATUS read_bytes(Ext4Vol *v, UINT64 byte_off, UINTN byte_count, void *buf) {
    if (v->luks_vol) {
        /* LUKS-backed: read via 512-byte sector interface */
        UINT64 first_sec = byte_off / 512;
        UINT64 last_sec  = (byte_off + byte_count - 1) / 512;
        UINTN  nsecs     = (UINTN)(last_sec - first_sec + 1);
        UINTN  total     = nsecs * 512;

        void *tmp = NULL;
        EFI_STATUS s = gBS->AllocatePool(EfiLoaderData, total, (void **)&tmp);
        if (EFI_ERROR(s)) return s;

        /* luks_vol_read is declared in luks_vol.h */
        extern int luks_vol_read(struct LuksVol *, UINT64, uint8_t *, UINT32);
        int r = luks_vol_read(v->luks_vol, first_sec, (uint8_t*)tmp, (UINT32)nsecs);
        if (r == 0) {
            UINTN src_off = (UINTN)(byte_off - first_sec * 512);
            for (UINTN i = 0; i < byte_count; i++)
                ((uint8_t *)buf)[i] = ((uint8_t *)tmp)[src_off + i];
        }
        gBS->FreePool(tmp);
        return (r == 0) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
    }

    if (v->dio)
        return v->dio->ReadDisk(v->dio, v->media_id, byte_off, byte_count, buf);

    /* BlockIO fallback: must read whole sectors, then copy the requested range */
    UINT64 first_lba = byte_off / v->phys_blk_sz;
    UINT64 last_lba  = (byte_off + byte_count - 1) / v->phys_blk_sz;
    UINTN  lba_count = (UINTN)(last_lba - first_lba + 1);
    UINTN  total     = lba_count * v->phys_blk_sz;

    void *tmp = NULL;
    EFI_STATUS s = gBS->AllocatePool(EfiLoaderData, total, (void **)&tmp);
    if (EFI_ERROR(s)) return s;

    s = v->bio->ReadBlocks(v->bio, v->media_id, first_lba, total, tmp);
    if (!EFI_ERROR(s)) {
        UINTN src_off = (UINTN)(byte_off - first_lba * v->phys_blk_sz);
        for (UINTN i = 0; i < byte_count; i++)
            ((uint8_t *)buf)[i] = ((uint8_t *)tmp)[src_off + i];
    }
    gBS->FreePool(tmp);
    return s;
}

/* Read one ext4 block (v->block_size bytes) at logical block number blk_no */
static EFI_STATUS blk_read(Ext4Vol *v, uint64_t blk_no, void *buf) {
    return read_bytes(v, blk_no * v->block_size, v->block_size, buf);
}

/* ------------------------------------------------------------------ */
/* Mount                                                               */
/* ------------------------------------------------------------------ */

Ext4Vol *ext4_mount(EFI_HANDLE h) {
    EFI_GUID bio_g = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_GUID dio_g = EFI_DISK_IO_PROTOCOL_GUID;
    EFI_BLOCK_IO_PROTOCOL *bio = NULL;
    EFI_DISK_IO_PROTOCOL  *dio = NULL;

    gBS->HandleProtocol(h, &bio_g, (void **)&bio);
    gBS->HandleProtocol(h, &dio_g, (void **)&dio);
    if (!bio || !bio->Media) return NULL;
    /* DiskIO is preferred but optional — BlockIO fallback used if absent */
    if (!bio->Media->MediaPresent || bio->Media->BlockSize == 0) return NULL;
    /* Only scan logical partitions (not raw whole-disk or optical handles).
     * Whole-disk and CD/DVD handles often hang or return garbage on reads. */
    if (!bio->Media->LogicalPartition) return NULL;
    /* Need at least 3 sectors to reach the superblock at byte offset 1024 */
    if (bio->Media->LastBlock < 2) return NULL;
    /* Skip optical drives (2048-byte blocks) — they can't hold ext4 */
    if (bio->Media->BlockSize == 2048) return NULL;

    /* Build a temporary vol on the stack just to read the superblock */
    Ext4Vol tmp;
    for (UINTN i = 0; i < sizeof(Ext4Vol); i++) ((uint8_t *)&tmp)[i] = 0;
    tmp.dio         = dio;
    tmp.bio         = bio;
    tmp.media_id    = bio->Media->MediaId;
    tmp.phys_blk_sz = bio->Media->BlockSize ? bio->Media->BlockSize : 512;
    tmp.block_size  = tmp.phys_blk_sz; /* dummy — only used for BlockIO padding calc */

    uint8_t sb[1024];
    if (EFI_ERROR(read_bytes(&tmp, EXT4_SUPERBLOCK_OFFSET, 1024, sb)))
        return NULL;
    if (r16(sb, SB_MAGIC) != EXT4_SUPER_MAGIC) return NULL;

    Ext4Vol *v = NULL;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, sizeof(Ext4Vol), (void **)&v)) || !v)
        return NULL;
    for (UINTN i = 0; i < sizeof(Ext4Vol); i++) ((uint8_t *)v)[i] = 0;

    v->dio              = dio;
    v->bio              = bio;
    v->media_id         = bio->Media->MediaId;
    v->phys_blk_sz      = tmp.phys_blk_sz;
    v->part_handle      = h;
    v->luks_vol         = NULL;
    v->block_size       = 1024u << r32(sb, SB_LOG_BLOCK_SIZE);
    v->inodes_per_group = r32(sb, SB_INODES_PER_GROUP);
    v->first_data_block = r32(sb, SB_FIRST_DATA_BLOCK);
    v->inode_size       = r16(sb, SB_INODE_SIZE);
    if (v->inode_size < 128) v->inode_size = 128;

    v->bgdt_off  = (UINT64)(v->first_data_block + 1) * v->block_size;

    uint32_t f_incompat = r32(sb, SB_FEATURE_INCOMPAT);
    uint16_t ds         = r16(sb, SB_DESC_SIZE);
    v->desc_size = ((f_incompat & EXT4_FEATURE_INCOMPAT_64BIT) && ds >= 64) ? 64u : 32u;

    return v;
}

void ext4_unmount(Ext4Vol *v) {
    if (v) gBS->FreePool(v);
}

EFI_HANDLE ext4_get_part_handle(Ext4Vol *vol) {
    return vol ? vol->part_handle : NULL;
}

Ext4Vol *ext4_mount_luks(struct LuksVol *lv) {
    if (!lv) return NULL;

    /* Read superblock via LUKS layer to verify ext4 */
    extern int luks_vol_read(struct LuksVol *, UINT64, uint8_t *, UINT32);

    /* Superblock is at byte offset 1024; that's sectors 2 and 3 */
    uint8_t sb_buf[512*3];
    if (luks_vol_read(lv, 0, sb_buf, 3) != 0) return NULL;
    uint8_t *sb = sb_buf + 1024;
    if (r16(sb, SB_MAGIC) != EXT4_SUPER_MAGIC) return NULL;

    Ext4Vol *v = NULL;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, sizeof(Ext4Vol), (void **)&v)) || !v)
        return NULL;
    for (UINTN i = 0; i < sizeof(Ext4Vol); i++) ((uint8_t *)v)[i] = 0;

    v->luks_vol         = lv;
    v->dio              = NULL;
    v->bio              = NULL;
    v->media_id         = 0;
    v->phys_blk_sz      = 512;
    v->part_handle      = NULL;
    v->block_size       = 1024u << r32(sb, SB_LOG_BLOCK_SIZE);
    v->inodes_per_group = r32(sb, SB_INODES_PER_GROUP);
    v->first_data_block = r32(sb, SB_FIRST_DATA_BLOCK);
    v->inode_size       = r16(sb, SB_INODE_SIZE);
    if (v->inode_size < 128) v->inode_size = 128;
    v->bgdt_off  = (UINT64)(v->first_data_block + 1) * v->block_size;

    uint32_t f_incompat = r32(sb, SB_FEATURE_INCOMPAT);
    uint16_t ds         = r16(sb, SB_DESC_SIZE);
    v->desc_size = ((f_incompat & EXT4_FEATURE_INCOMPAT_64BIT) && ds >= 64) ? 64u : 32u;

    return v;
}

/* ------------------------------------------------------------------ */
/* Inode read                                                          */
/* ------------------------------------------------------------------ */

/* Fill buf (v->inode_size bytes) with inode data for inode number ino */
static bool inode_read(Ext4Vol *v, uint32_t ino, uint8_t *buf) {
    if (ino == 0) return false;
    uint32_t grp   = (ino - 1) / v->inodes_per_group;
    uint32_t local = (ino - 1) % v->inodes_per_group;

    /* Read block group descriptor */
    uint8_t bgd[64];
    if (EFI_ERROR(read_bytes(v, v->bgdt_off + (UINT64)grp * v->desc_size,
                             v->desc_size, bgd)))
        return false;

    uint64_t it_lo = r32(bgd, BGD_INODE_TABLE_LO);
    uint64_t it_hi = (v->desc_size >= 64) ? r32(bgd, BGD_INODE_TABLE_HI) : 0;
    uint64_t it    = it_lo | (it_hi << 32);

    UINT64 off = it * v->block_size + (UINT64)local * v->inode_size;
    return !EFI_ERROR(read_bytes(v, off, v->inode_size, buf));
}

/* ------------------------------------------------------------------ */
/* Extent tree: map logical block number → physical block number       */
/*                                                                     */
/* eh      — pointer to extent header (in inode i_block or a block)   */
/* lblock  — logical block to look up                                  */
/* scratch — one block-sized buffer for depth-1 index reads            */
/*                                                                     */
/* Returns 0 for a sparse/hole block or on error.                      */
/* ------------------------------------------------------------------ */

static uint64_t extent_phys(Ext4Vol *v, const uint8_t *eh,
                              uint32_t lblock, uint8_t *scratch) {
    if (r16(eh, EH_MAGIC) != EXT4_EXTENT_HEADER_MAGIC) return 0;

    uint16_t entries = r16(eh, EH_ENTRIES);
    uint16_t depth   = r16(eh, EH_DEPTH);

    if (depth > 0) {
        /* Index node: find the best index entry (largest ei_block ≤ lblock) */
        const uint8_t *best = NULL;
        for (uint16_t i = 0; i < entries; i++) {
            const uint8_t *ei = eh + 12 + (UINTN)i * 12;
            if (r32(ei, EI_BLOCK) > lblock) break;
            best = ei;
        }
        if (!best) return 0;
        uint64_t leaf = r32(best, EI_LEAF_LO)
                      | ((uint64_t)r16(best, EI_LEAF_HI) << 32);
        if (EFI_ERROR(blk_read(v, leaf, scratch))) return 0;
        /* Now scratch holds the leaf extent block; re-read magic and entries */
        eh      = scratch;
        if (r16(eh, EH_MAGIC) != EXT4_EXTENT_HEADER_MAGIC) return 0;
        entries = r16(eh, EH_ENTRIES);
        /* depth is 0 in the leaf — fall through to leaf scan */
    }

    /* Leaf scan: find the extent covering lblock */
    for (uint16_t i = 0; i < entries; i++) {
        const uint8_t *ex = eh + 12 + (UINTN)i * 12;
        uint32_t ee_blk = r32(ex, EX_BLOCK);
        uint16_t ee_len = r16(ex, EX_LEN);
        if (lblock >= ee_blk && lblock < ee_blk + (uint32_t)ee_len) {
            uint64_t phys = r32(ex, EX_START_LO)
                          | ((uint64_t)r16(ex, EX_START_HI) << 32);
            return phys + (lblock - ee_blk);
        }
    }
    return 0; /* sparse/hole */
}

/* ------------------------------------------------------------------ */
/* Read one logical block from a file inode into out_buf.              */
/* scratch: one block-sized buffer used for extent index resolution.   */
/* ------------------------------------------------------------------ */

static bool read_lblock(Ext4Vol *v, const uint8_t *inode,
                         uint32_t lblock, uint8_t *out_buf, uint8_t *scratch) {
    uint32_t flags = r32(inode, IN_FLAGS);
    uint64_t phys  = 0;

    if (flags & EXT4_EXTENTS_FL) {
        phys = extent_phys(v, inode + IN_BLOCK, lblock, scratch);
    } else {
        /* Fall back to direct block addressing (i_block[0..11]) */
        if (lblock < 12)
            phys = r32(inode + IN_BLOCK, lblock * 4u);
    }

    if (phys == 0) {
        /* Sparse block — zero fill */
        for (UINTN i = 0; i < v->block_size; i++) out_buf[i] = 0;
        return true;
    }
    return !EFI_ERROR(blk_read(v, phys, out_buf));
}

/* ------------------------------------------------------------------ */
/* Directory lookup: find one path component in dir_ino                */
/* Returns child inode number, or 0 if not found.                      */
/* ------------------------------------------------------------------ */

static uint32_t dir_lookup_comp(Ext4Vol *v, uint32_t dir_ino,
                                  const char *name, int nlen,
                                  uint8_t *inode_buf, uint8_t *two_blocks) {
    /* two_blocks must be 2 × v->block_size bytes:
     *   [0 .. block_size-1]        → directory block data (out_buf)
     *   [block_size .. 2*block-1]  → extent index scratch */
    uint8_t *dir_buf  = two_blocks;
    uint8_t *ext_scratch = two_blocks + v->block_size;

    if (!inode_read(v, dir_ino, inode_buf)) return 0;
    uint32_t size = r32(inode_buf, IN_SIZE_LO);
    uint32_t nblk = (size + v->block_size - 1) / v->block_size;

    for (uint32_t blk = 0; blk < nblk; blk++) {
        if (!read_lblock(v, inode_buf, blk, dir_buf, ext_scratch)) break;

        UINTN limit = v->block_size;
        /* Last block may be partially filled */
        if (blk + 1 == nblk && (size % v->block_size))
            limit = size % v->block_size;

        UINTN pos = 0;
        while (pos + 8 <= limit) {
            const uint8_t *de = dir_buf + pos;
            uint32_t  ino2   = r32(de, DE_INODE);
            uint16_t  reclen = r16(de, DE_REC_LEN);
            uint8_t   nl     = de[DE_NAME_LEN];
            if (reclen == 0) break;

            if (ino2 && nl == (uint8_t)nlen) {
                bool match = true;
                const char *dname = (const char *)(de + DE_NAME);
                for (int i = 0; i < nlen; i++) {
                    if (dname[i] != name[i]) { match = false; break; }
                }
                if (match) return ino2;
            }
            pos += reclen;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Walk an absolute path to an inode number.                           */
/* Returns 0 if any component is not found.                            */
/* ------------------------------------------------------------------ */

static uint32_t path_lookup(Ext4Vol *v, const char *path,
                              uint8_t *inode_buf, uint8_t *two_blocks) {
    uint32_t    ino = EXT4_ROOT_INO;
    const char *p   = path;
    while (*p == '/') p++;   /* skip leading slashes */
    if (!*p) return ino;     /* bare "/" → root */

    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        int clen = (int)(p - start);
        while (*p == '/') p++;

        ino = dir_lookup_comp(v, ino, start, clen, inode_buf, two_blocks);
        if (!ino) return 0;
    }
    return ino;
}

/* ------------------------------------------------------------------ */
/* Public: stat (existence check)                                      */
/* ------------------------------------------------------------------ */

UINTN ext4_stat(Ext4Vol *v, const char *path) {
    uint8_t *inode_buf = NULL, *two_blocks = NULL;
    UINTN result = 0;

    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, v->inode_size,  (void **)&inode_buf)))
        return 0;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, v->block_size * 2, (void **)&two_blocks)))
        { gBS->FreePool(inode_buf); return 0; }

    uint32_t ino = path_lookup(v, path, inode_buf, two_blocks);
    if (ino && inode_read(v, ino, inode_buf))
        result = (UINTN)r32(inode_buf, IN_SIZE_LO);

    gBS->FreePool(two_blocks);
    gBS->FreePool(inode_buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* Public: read entire file into pool memory                           */
/* ------------------------------------------------------------------ */

void *ext4_read_file(Ext4Vol *v, const char *path, UINTN *out_size) {
    uint8_t *inode_buf  = NULL;
    uint8_t *two_blocks = NULL;  /* two block-sized buffers: [idx scratch | block data] */
    void    *result     = NULL;

    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, v->inode_size,     (void **)&inode_buf)))
        return NULL;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, v->block_size * 2, (void **)&two_blocks)))
        { gBS->FreePool(inode_buf); return NULL; }

    uint8_t *ext_scratch = two_blocks;                 /* first block: extent index */
    uint8_t *blk_buf     = two_blocks + v->block_size; /* second block: file data */

    uint32_t ino = path_lookup(v, path, inode_buf, two_blocks);
    if (!ino || !inode_read(v, ino, inode_buf)) goto done;

    {
        uint64_t fsize = (uint64_t)r32(inode_buf, IN_SIZE_LO)
                       | ((uint64_t)r32(inode_buf, IN_SIZE_HIGH) << 32);
        if (fsize == 0 || fsize > 128ull * 1024 * 1024) goto done; /* sanity: max 128 MB */

        char *buf = NULL;
        if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, (UINTN)fsize + 1, (void **)&buf)))
            goto done;
        ((uint8_t *)buf)[fsize] = 0;

        uint32_t nblk = (uint32_t)((fsize + v->block_size - 1) / v->block_size);
        for (uint32_t blk = 0; blk < nblk; blk++) {
            if (!read_lblock(v, inode_buf, blk, blk_buf, ext_scratch)) {
                gBS->FreePool(buf); goto done;
            }
            UINTN dst = (UINTN)blk * v->block_size;
            UINTN len = (UINTN)(fsize - dst);
            if (len > v->block_size) len = v->block_size;
            for (UINTN i = 0; i < len; i++) ((uint8_t *)buf)[dst + i] = blk_buf[i];
        }
        *out_size = (UINTN)fsize;
        result = buf;
    }

done:
    gBS->FreePool(two_blocks);
    gBS->FreePool(inode_buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* Public: readdir                                                     */
/* ------------------------------------------------------------------ */

void ext4_readdir(Ext4Vol *v, const char *path, Ext4DirCb cb, void *ctx) {
    uint8_t *inode_buf  = NULL;
    uint8_t *two_blocks = NULL;

    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, v->inode_size,     (void **)&inode_buf)))
        return;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, v->block_size * 2, (void **)&two_blocks)))
        { gBS->FreePool(inode_buf); return; }

    uint8_t *ext_scratch = two_blocks;
    uint8_t *blk_buf     = two_blocks + v->block_size;

    uint32_t dir_ino = path_lookup(v, path, inode_buf, two_blocks);
    if (!dir_ino || !inode_read(v, dir_ino, inode_buf)) goto done;

    {
        uint32_t size = r32(inode_buf, IN_SIZE_LO);
        uint32_t nblk = (size + v->block_size - 1) / v->block_size;

        for (uint32_t blk = 0; blk < nblk; blk++) {
            if (!read_lblock(v, inode_buf, blk, blk_buf, ext_scratch)) break;

            UINTN limit = v->block_size;
            if (blk + 1 == nblk && (size % v->block_size))
                limit = size % v->block_size;

            UINTN pos = 0;
            while (pos + 8 <= limit) {
                const uint8_t *de = blk_buf + pos;
                uint32_t  ino2   = r32(de, DE_INODE);
                uint16_t  reclen = r16(de, DE_REC_LEN);
                uint8_t   nl     = de[DE_NAME_LEN];
                uint8_t   ftype  = de[DE_FILE_TYPE];
                if (reclen == 0) break;

                if (ino2 && nl > 0 && nl < 255) {
                    /* Only report regular files (1) and directories (2).
                     * Symlinks (7), devices, etc. are silently ignored.
                     * This prevents /vmlinuz symlinks on Ubuntu/Debian roots
                     * from being mistaken for real kernels. */
                    if (ftype != 1 && ftype != EXT4_FT_DIR) {
                        pos += reclen; continue;
                    }
                    char fname[256];
                    const char *src = (const char *)(de + DE_NAME);
                    for (int i = 0; i < (int)nl; i++) fname[i] = src[i];
                    fname[nl] = 0;
                    /* Skip "." and ".." */
                    if (!(fname[0] == '.' && (fname[1] == 0 ||
                         (fname[1] == '.' && fname[2] == 0))))
                        cb(ctx, fname, ftype == EXT4_FT_DIR);
                }
                pos += reclen;
            }
        }
    }

done:
    gBS->FreePool(two_blocks);
    gBS->FreePool(inode_buf);
}
