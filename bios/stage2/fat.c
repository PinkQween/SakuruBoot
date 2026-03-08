/*
 * Minimal FAT32 read-only driver for SakuruBoot Stage 2.
 *
 * Supports:
 *   - FAT32 volumes (BPB detection)
 *   - Short (8.3) and long (LFN) filenames
 *   - Subdirectory traversal
 *   - Contiguous and fragmented files (cluster chain)
 */

#include "fat.h"
#include "disk.h"

/* ------------------------------------------------------------------ */
/* FAT32 on-disk structures                                            */
/* ------------------------------------------------------------------ */

typedef struct __attribute__((packed)) {
    u8  jmp[3];
    u8  oem[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  num_fats;
    u16 root_entry_count;       /* 0 for FAT32 */
    u16 total_sectors16;
    u8  media_type;
    u16 fat_size16;             /* 0 for FAT32 */
    u16 sectors_per_track;
    u16 num_heads;
    u32 hidden_sectors;
    u32 total_sectors32;
    /* FAT32 extended BPB */
    u32 fat_size32;
    u16 ext_flags;
    u16 fs_version;
    u32 root_cluster;
    u16 fs_info;
    u16 backup_boot;
    u8  reserved[12];
    u8  drive_num;
    u8  reserved1;
    u8  boot_sig;
    u32 vol_id;
    u8  vol_label[11];
    u8  fs_type[8];             /* "FAT32   " */
} FAT32_BPB;

typedef struct __attribute__((packed)) {
    u8  name[8];
    u8  ext[3];
    u8  attr;
    u8  nt_res;
    u8  crt_time_tenth;
    u16 crt_time;
    u16 crt_date;
    u16 acc_date;
    u16 first_cluster_hi;
    u16 wrt_time;
    u16 wrt_date;
    u16 first_cluster_lo;
    u32 file_size;
} FAT32_DirEntry;

typedef struct __attribute__((packed)) {
    u8  ord;
    u16 name1[5];
    u8  attr;       /* 0x0F */
    u8  type;
    u8  chksum;
    u16 name2[6];
    u16 first_cluster;
    u16 name3[2];
} FAT32_LFN;

#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LFN        0x0F

#define FAT32_EOC   0x0FFFFFF8U
#define FAT32_BAD   0x0FFFFFF7U
#define FAT_MASK    0x0FFFFFFFU

/* ------------------------------------------------------------------ */
/* FatFile handle                                                      */
/* ------------------------------------------------------------------ */
struct FatFile {
    u32 first_cluster;
    u32 file_size;
    u32 cur_cluster;
    u32 offset;          /* bytes read so far */
    bool is_dir;
};

/* ------------------------------------------------------------------ */
/* Driver state                                                        */
/* ------------------------------------------------------------------ */
#define SECTOR_SIZE     512
#define MAX_OPEN_FILES  8
#define CLUSTER_CACHE   4   /* sectors cached per read */

static FAT32_BPB  g_bpb;
static u64        g_part_lba;
static u32        g_fat_start;   /* LBA of FAT region */
static u32        g_data_start;  /* LBA of data region */
static u32        g_root_cluster;
static u32        g_spc;         /* sectors per cluster */

static FatFile    g_file_pool[MAX_OPEN_FILES];
static bool       g_pool_used[MAX_OPEN_FILES];

static u8 g_sector_buf[SECTOR_SIZE * CLUSTER_CACHE];

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static u64 cluster_to_lba(u32 cluster) {
    return g_data_start + (u64)(cluster - 2) * g_spc;
}

static u32 next_cluster(u32 cluster) {
    /* Each FAT32 entry is 4 bytes; calculate sector + offset */
    u32 fat_offset = cluster * 4;
    u32 fat_sector = g_fat_start + (fat_offset / SECTOR_SIZE);
    u32 ent_offset = fat_offset % SECTOR_SIZE;

    disk_read(fat_sector, 1, g_sector_buf);
    u32 val = *(u32 *)(g_sector_buf + ent_offset);
    return val & FAT_MASK;
}

static void read_cluster(u32 cluster, void *buf) {
    u64 lba = cluster_to_lba(cluster);
    disk_read(lba, g_spc, buf);
}

static __attribute__((unused)) bool is_space(char c) { return c == ' '; }

/* Convert FAT 8.3 name to a null-terminated string (e.g. "SAKURUCFG" → no) */
static void fat83_to_str(const u8 *name, const u8 *ext, char *out) {
    int i = 7;
    while (i >= 0 && name[i] == ' ') i--;
    int j = 0;
    for (int k = 0; k <= i; k++) out[j++] = (char)name[k];
    int e = 2;
    while (e >= 0 && ext[e] == ' ') e--;
    if (e >= 0) {
        out[j++] = '.';
        for (int k = 0; k <= e; k++) out[j++] = (char)ext[k];
    }
    out[j] = 0;
}

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

static bool name_match(const char *entry_name, const char *target) {
    while (*entry_name && *target) {
        if (to_upper(*entry_name) != to_upper(*target)) return false;
        entry_name++; target++;
    }
    return *entry_name == 0 && *target == 0;
}

/* ------------------------------------------------------------------ */
/* Directory search                                                    */
/* ------------------------------------------------------------------ */

/* Find a single path component in the directory starting at cluster.
 * Returns the DirEntry on success (copied to *out), or false. */
static bool find_in_dir(u32 dir_cluster, const char *name,
                        FAT32_DirEntry *out, bool *is_last_path) {
    static u8 cluster_buf[512 * 64]; /* up to 64 sectors per cluster — more than enough */
    char lfn_buf[256];
    int lfn_len = 0;
    (void)is_last_path;

    u32 cluster = dir_cluster;
    u32 cluster_limit = g_spc ? (g_bpb.total_sectors32 / g_spc) + 2 : 0x100000U;
    u32 visited = 0;
    while (cluster >= 2 && cluster < FAT32_EOC) {
        if (++visited > cluster_limit) return false; /* corrupted FAT chain */
        u32 spc = g_spc;
        disk_read(cluster_to_lba(cluster), spc, cluster_buf);

        u32 entries = (spc * SECTOR_SIZE) / sizeof(FAT32_DirEntry);
        FAT32_DirEntry *dir = (FAT32_DirEntry *)cluster_buf;

        for (u32 i = 0; i < entries; i++) {
            if (dir[i].name[0] == 0x00) return false;  /* End of directory */
            if (dir[i].name[0] == 0xE5) { lfn_len = 0; continue; } /* Deleted */

            if (dir[i].attr == ATTR_LFN) {
                /* LFN entry: extract UCS-2 name fragment into ASCII */
                FAT32_LFN *lfn = (FAT32_LFN *)&dir[i];
                int ord = (lfn->ord & 0x3F) - 1;
                char tmp[14];
                int ti = 0;
                for (int k = 0; k < 5; k++)
                    tmp[ti++] = lfn->name1[k] ? (char)lfn->name1[k] : 0;
                for (int k = 0; k < 6; k++)
                    tmp[ti++] = lfn->name2[k] ? (char)lfn->name2[k] : 0;
                for (int k = 0; k < 2; k++)
                    tmp[ti++] = lfn->name3[k] ? (char)lfn->name3[k] : 0;
                tmp[13] = 0;
                /* Place into lfn_buf at right position */
                int pos = ord * 13;
                for (int k = 0; k < 13 && tmp[k]; k++)
                    if (pos + k < 255) lfn_buf[pos + k] = tmp[k];
                lfn_len = 1;
                continue;
            }

            /* Regular or directory entry */
            if (lfn_len) {
                /* Try LFN match */
                lfn_buf[255] = 0;
                if (name_match(lfn_buf, name)) {
                    *out = dir[i];
                    return true;
                }
                lfn_len = 0;
            }

            /* Try 8.3 match */
            char sname[13];
            fat83_to_str(dir[i].name, dir[i].ext, sname);
            if (name_match(sname, name)) {
                *out = dir[i];
                return true;
            }
        }

        cluster = next_cluster(cluster);
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int fat_init(u64 part_lba) {
    g_part_lba = part_lba;

    /* Read boot sector */
    disk_read(part_lba, 1, &g_bpb);

    /* Validate FAT32 signature */
    if (g_bpb.bytes_per_sector != SECTOR_SIZE) return -1;
    if (g_bpb.fat_size32 == 0)                return -1;

    u32 reserved = g_bpb.reserved_sectors;
    u32 fat_size = g_bpb.fat_size32;
    u32 num_fats = g_bpb.num_fats;

    g_fat_start   = (u32)part_lba + reserved;
    g_data_start  = (u32)part_lba + reserved + num_fats * fat_size;
    g_root_cluster= g_bpb.root_cluster;
    g_spc         = g_bpb.sectors_per_cluster;

    return 0;
}

#define MAX_PATH_COMPONENT 256

FatFile *fat_open(const char *path) {
    /* Walk path components separated by '/' */
    u32 cluster = g_root_cluster;

    const char *p = path;
    if (*p == '/') p++;

    while (*p) {
        /* Extract next component */
        char component[MAX_PATH_COMPONENT];
        int ci = 0;
        while (*p && *p != '/' && ci < (int)sizeof(component) - 1)
            component[ci++] = *p++;
        component[ci] = 0;
        if (*p == '/') p++;
        bool is_last = (*p == 0);

        FAT32_DirEntry entry;
        if (!find_in_dir(cluster, component, &entry, NULL)) return NULL;

        if (!is_last) {
            /* Must be a directory */
            if (!(entry.attr & ATTR_DIRECTORY)) return NULL;
            cluster = ((u32)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
        } else {
            /* Found the file */
            for (int i = 0; i < MAX_OPEN_FILES; i++) {
                if (!g_pool_used[i]) {
                    g_pool_used[i]     = true;
                    g_file_pool[i].first_cluster =
                        ((u32)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
                    g_file_pool[i].file_size   = entry.file_size;
                    g_file_pool[i].cur_cluster = g_file_pool[i].first_cluster;
                    g_file_pool[i].offset      = 0;
                    g_file_pool[i].is_dir      = !!(entry.attr & ATTR_DIRECTORY);
                    return &g_file_pool[i];
                }
            }
            return NULL; /* No free handles */
        }
    }
    return NULL;
}

u32 fat_read(FatFile *f, void *buf, u32 len) {
    if (!f || f->offset >= f->file_size) return 0;

    u32 remaining = f->file_size - f->offset;
    if (len > remaining) len = remaining;

    u8  *dst       = (u8 *)buf;
    u32  read_tot  = 0;
    u32  clust_sz  = g_spc * SECTOR_SIZE;

    static u8 clust_buf[512 * 64];

    while (read_tot < len && f->cur_cluster >= 2 && f->cur_cluster < FAT32_EOC) {
        u32 clust_off = f->offset % clust_sz;
        u32 in_clust  = clust_sz - clust_off;
        if (in_clust > len - read_tot) in_clust = len - read_tot;

        read_cluster(f->cur_cluster, clust_buf);
        for (u32 i = 0; i < in_clust; i++)
            dst[read_tot + i] = clust_buf[clust_off + i];

        read_tot   += in_clust;
        f->offset  += in_clust;

        if ((f->offset % clust_sz) == 0)
            f->cur_cluster = next_cluster(f->cur_cluster);
    }
    return read_tot;
}

void fat_close(FatFile *f) {
    if (!f) return;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (&g_file_pool[i] == f) { g_pool_used[i] = false; return; }
    }
}

u32 fat_file_size(FatFile *f) {
    return f ? f->file_size : 0;
}

/* ------------------------------------------------------------------ */
/* Directory enumeration                                               */
/* ------------------------------------------------------------------ */

bool fat_readdir(const char *dir_path, FatDirCb cb, void *ctx) {
    u32 cluster = g_root_cluster;

    /* Walk path components to reach the target directory */
    const char *p = dir_path;
    if (*p == '/') p++;
    while (*p) {
        char component[256];
        int ci = 0;
        while (*p && *p != '/' && ci < 255) component[ci++] = *p++;
        component[ci] = 0;
        if (*p == '/') p++;
        if (ci == 0) continue;

        FAT32_DirEntry de;
        if (!find_in_dir(cluster, component, &de, NULL)) return false;
        if (!(de.attr & ATTR_DIRECTORY)) return false;
        cluster = ((u32)de.first_cluster_hi << 16) | de.first_cluster_lo;
    }

    /* Enumerate all entries in the directory at 'cluster' */
    static u8 enum_buf[512 * 64]; /* separate from find_in_dir's buffer */
    char lfn_buf[256];
    int  lfn_len = 0;

    u32 cur = cluster;
    u32 cur_limit = g_spc ? (g_bpb.total_sectors32 / g_spc) + 2 : 0x100000U;
    u32 cur_visited = 0;
    while (cur >= 2 && cur < FAT32_EOC) {
        if (++cur_visited > cur_limit) break; /* corrupted FAT chain */
        disk_read(cluster_to_lba(cur), g_spc, enum_buf);
        u32 cnt = (g_spc * SECTOR_SIZE) / sizeof(FAT32_DirEntry);
        FAT32_DirEntry *dir = (FAT32_DirEntry *)enum_buf;

        for (u32 i = 0; i < cnt; i++) {
            u8 first = dir[i].name[0];
            if (first == 0x00) return true;       /* end of directory */
            if (first == 0xE5) { lfn_len = 0; continue; } /* deleted */
            if (first == '.')  { lfn_len = 0; continue; } /* . and .. */
            if (dir[i].attr & ATTR_VOLUME_ID && dir[i].attr != ATTR_LFN)
                { lfn_len = 0; continue; }

            if (dir[i].attr == ATTR_LFN) {
                FAT32_LFN *lfn = (FAT32_LFN *)&dir[i];
                int ord = (lfn->ord & 0x3F) - 1;
                if (ord < 0 || ord >= 20) { lfn_len = 0; continue; }
                int pos = ord * 13;
                for (int k = 0; k < 5 && pos+k < 255; k++)
                    lfn_buf[pos+k] = lfn->name1[k] ? (char)lfn->name1[k] : 0;
                for (int k = 0; k < 6 && pos+5+k < 255; k++)
                    lfn_buf[pos+5+k] = lfn->name2[k] ? (char)lfn->name2[k] : 0;
                for (int k = 0; k < 2 && pos+11+k < 255; k++)
                    lfn_buf[pos+11+k] = lfn->name3[k] ? (char)lfn->name3[k] : 0;
                lfn_len = 1;
                continue;
            }

            /* Regular entry */
            char name[256];
            if (lfn_len) {
                int e = 0;
                while (e < 255 && lfn_buf[e]) e++;
                lfn_buf[e] = 0;
                for (int k = 0; k <= e; k++) name[k] = lfn_buf[k];
                lfn_len = 0;
            } else {
                fat83_to_str(dir[i].name, dir[i].ext, name);
                if (!name[0]) continue;
            }

            bool is_dir = !!(dir[i].attr & ATTR_DIRECTORY);
            cb(name, is_dir, ctx);
        }
        cur = next_cluster(cur);
    }
    return true;
}

