/*
 * common/loader_entry.c — systemd-boot Type 1 Loader Entry spec parser
 */

#ifndef SAKURU_HOST_TEST

#include "loader_entry.h"
#include "config.h"
#include "types.h"
#include "../uefi/efi.h"

/* EFI file protocol function pointer types (void* in efi.h) */
typedef EFI_STATUS (EFIAPI *FileOpenFn)    (void *, void **, CHAR16 *, u64, u64);
typedef EFI_STATUS (EFIAPI *FileReadFn)    (void *, UINTN *, void *);
typedef EFI_STATUS (EFIAPI *FileCloseFn)   (void *);
typedef EFI_STATUS (EFIAPI *FileSetPosFn)  (void *, u64);
typedef EFI_STATUS (EFIAPI *FileGetInfoFn) (void *, EFI_GUID *, UINTN *, void *);

/* EFI_FILE_INFO GUID */
#define EFI_FILE_INFO_GUID \
    EFI_GUID_INIT(0x09576e92,0x6d3f,0x11d2,0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b)

/* EFI attribute flags */
#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_DIRECTORY   0x0000000000000010ULL

/* Minimal EFI_FILE_INFO layout (variable-length; FileName starts at offset 80) */
typedef struct {
    u64     Size;
    u64     FileSize;
    u64     PhysicalSize;
    /* three EFI_TIME structs (each 16 bytes) */
    u8      Times[48];
    u64     Attribute;
    /* CHAR16 FileName[] follows — handled via offsetof */
} FileInfoHdr;

/* ── UCS-2 string helpers ──────────────────────────────────────────── */
static int ucs2_ends_with_conf(const CHAR16 *s) {
    UINTN len = 0;
    while (s[len]) len++;
    if (len < 5) return 0;
    return (s[len-5] == '.') &&
           ((s[len-4]|0x20) == 'c') &&
           ((s[len-3]|0x20) == 'o') &&
           ((s[len-2]|0x20) == 'n') &&
           ((s[len-1]|0x20) == 'f');
}

static void ucs2_to_ascii_n(char *dst, const CHAR16 *src, int max) {
    int i = 0;
    while (src[i] && i + 1 < max) { dst[i] = (char)(src[i] & 0x7F); i++; }
    dst[i] = 0;
}

/* ── Single-entry .conf parser ─────────────────────────────────────── */
static void parse_entry_conf(const char *buf, UINTN len, BootEntry *e) {
    const char *p   = buf;
    const char *end = buf + len;

    while (p < end) {
        /* Skip whitespace */
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        /* Comment or blank line */
        if (p >= end || *p == '#' || *p == '\n' || *p == '\r') {
            while (p < end && *p != '\n') p++;
            if (p < end) p++;
            continue;
        }
        /* Find key */
        const char *key_start = p;
        while (p < end && *p != ' ' && *p != '\t' && *p != '\n') p++;
        int key_len = (int)(p - key_start);

        /* Skip whitespace between key and value */
        while (p < end && (*p == ' ' || *p == '\t')) p++;

        /* Value: rest of line */
        const char *val_start = p;
        while (p < end && *p != '\n' && *p != '\r') p++;
        int val_len = (int)(p - val_start);
        /* Strip trailing whitespace */
        while (val_len > 0 && (val_start[val_len-1] == ' ' ||
                                val_start[val_len-1] == '\t')) val_len--;

        /* Skip rest of line */
        while (p < end && *p != '\n') p++;
        if (p < end) p++;

        /* Match key */
#define KEY_IS(k) (key_len == (int)sizeof(k)-1 && \
                   __builtin_memcmp(key_start, k, key_len) == 0)
        if (KEY_IS("title")) {
            int n = val_len < MAX_STR_LEN-1 ? val_len : MAX_STR_LEN-1;
            __builtin_memcpy(e->name, val_start, n);
            e->name[n] = '\0';
        } else if (KEY_IS("linux")) {
            int n = val_len < MAX_STR_LEN-1 ? val_len : MAX_STR_LEN-1;
            __builtin_memcpy(e->kernel, val_start, n);
            e->kernel[n] = '\0';
        } else if (KEY_IS("initrd")) {
            int n = val_len < MAX_STR_LEN-1 ? val_len : MAX_STR_LEN-1;
            __builtin_memcpy(e->initrd, val_start, n);
            e->initrd[n] = '\0';
        } else if (KEY_IS("options")) {
            int n = val_len < MAX_STR_LEN-1 ? val_len : MAX_STR_LEN-1;
            __builtin_memcpy(e->cmdline, val_start, n);
            e->cmdline[n] = '\0';
        }
#undef KEY_IS
    }
    e->type = OS_TYPE_LINUX;
    if (e->luks_tries == 0) e->luks_tries = 3;
}

/* ── Directory scan ─────────────────────────────────────────────────── */
void loader_entry_scan(void *root_dir, BootConfig *cfg) {
    if (!root_dir || !cfg) return;

    /* Open /loader/entries/ */
    void *entries_dir = NULL;
    CHAR16 entries_path[] = {'\\','l','o','a','d','e','r','\\',
                              'e','n','t','r','i','e','s','\\', 0};
    FileOpenFn file_open = (FileOpenFn)(((void **)root_dir)[0]);
    EFI_STATUS s = file_open(root_dir, &entries_dir, entries_path,
                             EFI_FILE_MODE_READ, 0);
    if (s != 0 || !entries_dir) return;

    FileCloseFn   file_close   = (FileCloseFn)  (((void **)entries_dir)[2]);
    FileReadFn    file_read    = (FileReadFn)   (((void **)entries_dir)[3]);
    FileSetPosFn  file_set_pos = (FileSetPosFn) (((void **)entries_dir)[5]);
    FileGetInfoFn file_get_info= (FileGetInfoFn)(((void **)entries_dir)[8]);

    /* Rewind */
    file_set_pos(entries_dir, 0);

    /* Buffer for EFI_FILE_INFO (name up to 256 chars) */
    u8 info_buf[sizeof(FileInfoHdr) + 256 * sizeof(CHAR16)];

    while (cfg->num_entries < MAX_ENTRIES) {
        UINTN info_size = sizeof(info_buf);
        EFI_GUID fi_guid = EFI_FILE_INFO_GUID;
        s = file_get_info(entries_dir, &fi_guid, &info_size, info_buf);
        if (s != 0 || info_size == 0) break;  /* End of directory */

        FileInfoHdr *fi = (FileInfoHdr *)info_buf;
        if (fi->Attribute & EFI_FILE_DIRECTORY) continue;

        /* FileName starts right after the fixed part */
        CHAR16 *fname = (CHAR16 *)(info_buf + sizeof(FileInfoHdr));
        if (!ucs2_ends_with_conf(fname)) continue;

        /* Open the .conf file */
        void *conf_file = NULL;
        s = file_open(entries_dir, &conf_file, fname,
                      EFI_FILE_MODE_READ, 0);
        if (s != 0 || !conf_file) continue;

        /* Read entire file (limit to 4 KiB) */
        char fbuf[4096];
        UINTN flen = sizeof(fbuf) - 1;
        FileReadFn conf_read = (FileReadFn)(((void **)conf_file)[3]);
        conf_read(conf_file, &flen, fbuf);
        fbuf[flen] = '\0';

        FileCloseFn conf_close = (FileCloseFn)(((void **)conf_file)[2]);
        conf_close(conf_file);

        /* Parse into next available entry */
        BootEntry *e = &cfg->entries[cfg->num_entries];
        __builtin_memset(e, 0, sizeof(*e));

        /* Default name from filename (strip .conf suffix, UCS-2→ASCII) */
        char ascii_name[MAX_STR_LEN];
        ucs2_to_ascii_n(ascii_name, fname, MAX_STR_LEN);
        int nlen = 0;
        while (ascii_name[nlen] && ascii_name[nlen] != '.') nlen++;
        __builtin_memcpy(e->name, ascii_name, nlen);
        e->name[nlen] = '\0';

        parse_entry_conf(fbuf, flen, e);

        /* Only add if a kernel path was found */
        if (e->kernel[0] != '\0') cfg->num_entries++;
    }

    file_close(entries_dir);
}

#endif /* !SAKURU_HOST_TEST */
