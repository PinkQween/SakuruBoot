/*
 * SakuruBoot config file parser
 *
 * Format:
 *   timeout = 5
 *   default = 0
 *
 *   [entry:ViOS]
 *   type    = elf64
 *   kernel  = /boot/vios/kernel.elf
 *   cmdline = quiet
 *
 *   [entry:Linux]
 *   type    = linux
 *   kernel  = /boot/vmlinuz
 *   initrd  = /boot/initrd.img
 *   cmdline = root=/dev/sda2 quiet splash
 */

#include "config.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static bool is_space(char c) { return c == ' ' || c == '\t'; }
static bool is_newline(char c) { return c == '\n' || c == '\r'; }

static const char *skip_spaces(const char *p) {
    while (*p && is_space(*p)) p++;
    return p;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int str_starts(const char *haystack, const char *prefix) {
    while (*prefix) {
        if (*haystack != *prefix) return 0;
        haystack++; prefix++;
    }
    return 1;
}

static void str_copy(char *dst, const char *src, u32 max) {
    u32 i = 0;
    while (i + 1 < max && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Parse a line into key and value. Returns 1 if valid "key = value". */
static int parse_kv(const char *line, char *key, u32 kmax,
                    char *val, u32 vmax) {
    const char *p = skip_spaces(line);
    u32 ki = 0;

    while (*p && !is_space(*p) && *p != '=' && !is_newline(*p)) {
        if (ki + 1 < kmax) key[ki++] = *p;
        p++;
    }
    key[ki] = 0;
    if (!ki) return 0;

    p = skip_spaces(p);
    if (*p != '=') return 0;
    p++;
    p = skip_spaces(p);

    u32 vi = 0;
    while (*p && !is_newline(*p)) {
        if (vi + 1 < vmax) val[vi++] = *p;
        p++;
    }
    /* Trim trailing spaces */
    while (vi > 0 && is_space(val[vi - 1])) vi--;
    val[vi] = 0;

    return 1;
}

/* ------------------------------------------------------------------ */
/* Kernel discovery helpers                                            */
/* ------------------------------------------------------------------ */

static u32 cfg_str_len(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static int cfg_str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

/* Case-insensitive suffix check */
static int ends_with_ci(const char *s, const char *suf) {
    u32 slen = cfg_str_len(s), suflen = cfg_str_len(suf);
    if (suflen > slen) return 0;
    const char *p = s + slen - suflen;
    while (*suf) {
        char sc = *p >= 'A' && *p <= 'Z' ? (char)(*p + 32) : *p;
        char dc = *suf >= 'A' && *suf <= 'Z' ? (char)(*suf + 32) : *suf;
        if (sc != dc) return 0;
        p++; suf++;
    }
    return 1;
}

OSType config_guess_type(const char *filename) {
    if (ends_with_ci(filename, ".elf"))          return OS_TYPE_ELF64;
    if (str_starts(filename, "vmlinuz"))         return OS_TYPE_LINUX;
    if (str_starts(filename, "bzImage"))         return OS_TYPE_LINUX;
    if (cfg_str_eq_ci(filename, "bootmgfw.efi")) return OS_TYPE_WINDOWS;
    if (cfg_str_eq_ci(filename, "bootmgr.efi"))  return OS_TYPE_WINDOWS;
    return OS_TYPE_UNKNOWN;
}

void config_make_name(const char *dir, const char *filename, OSType type,
                      char *out, u32 max) {
    if (type == OS_TYPE_ELF64) {
        /* If the file is named "kernel.elf", use the parent directory name */
        if (cfg_str_eq_ci(filename, "kernel.elf") || cfg_str_eq_ci(filename, "kernel")) {
            /* Extract last path component of dir */
            const char *p = dir;
            const char *last = dir;
            while (*p) { if (*p == '/') last = p + 1; p++; }
            if (*last == 0) last = dir; /* fallback */
            /* Capitalize first char */
            u32 i = 0;
            if (*last && i + 1 < max) {
                out[i++] = (*last >= 'a' && *last <= 'z') ? (char)(*last - 32) : *last;
                last++;
            }
            while (*last && i + 1 < max) out[i++] = *last++;
            out[i] = 0;
            return;
        }
        /* Otherwise strip the .elf extension */
        u32 i = 0;
        while (filename[i] && !(filename[i] == '.' && ends_with_ci(filename + i, ".elf"))
               && i + 1 < max) {
            out[i] = filename[i]; i++;
        }
        out[i] = 0;
        return;
    }

    if (type == OS_TYPE_WINDOWS) {
        str_copy(out, "Windows", max);
        return;
    }

    if (type == OS_TYPE_LINUX) {
        /* vmlinuz → "Linux", vmlinuz-X.Y.Z → "Linux X.Y.Z" */
        u32 skip = str_starts(filename, "vmlinuz") ? 7 :
                   str_starts(filename, "bzImage")  ? 7 : 0;
        str_copy(out, "Linux", max);
        const char *ver = filename + skip;
        if (*ver == '-' || *ver == '_') ver++;
        if (*ver) {
            u32 ol = cfg_str_len(out);
            if (ol + 1 < max) { out[ol++] = ' '; out[ol] = 0; }
            str_copy(out + ol, ver, max - ol);
        }
        return;
    }

    str_copy(out, filename, max);
}

int config_add_kernel(BootConfig *cfg, const char *name, OSType type,
                      const char *kernel_path, const char *initrd_path) {
    /* Skip if this kernel path is already in the table */
    for (u32 i = 0; i < cfg->num_entries; i++) {
        if (str_eq(cfg->entries[i].kernel, kernel_path)) return 0;
    }
    if (cfg->num_entries >= MAX_ENTRIES) return -1;

    BootEntry *e = &cfg->entries[cfg->num_entries++];
    for (u32 i = 0; i < sizeof(BootEntry); i++) ((u8 *)e)[i] = 0;
    e->type = type;
    str_copy(e->name,   name,        MAX_STR_LEN);
    str_copy(e->kernel, kernel_path, MAX_STR_LEN);
    if (initrd_path && initrd_path[0])
        str_copy(e->initrd, initrd_path, MAX_STR_LEN);
    return 0;
}


OSType config_parse_type(const char *str) {
    if (str_eq(str, "elf64"))      return OS_TYPE_ELF64;
    if (str_eq(str, "linux"))      return OS_TYPE_LINUX;
    if (str_eq(str, "multiboot2")) return OS_TYPE_MULTIBOOT2;
    if (str_eq(str, "windows"))    return OS_TYPE_WINDOWS;
    return OS_TYPE_UNKNOWN;
}

/* Shared parser — if merge=false (full parse), resets config first.
 * If merge=true, appends entries to existing config without touching
 * timeout/default/theme/accent already set by an earlier config file. */
static int do_parse(const char *buf, u32 len, BootConfig *cfg, bool merge) {
    (void)len;

    if (!merge) {
        cfg->timeout       = DEFAULT_TIMEOUT;
        cfg->default_entry = 0;
        cfg->num_entries   = 0;
        cfg->theme_color   = 5;   /* magenta — default border/highlight bg */
        cfg->accent_color  = 14;  /* yellow  — default title/arrow/hint fg */
    }

    BootEntry *cur = NULL;
    const char *p  = buf;

    while (*p) {
        /* Skip leading whitespace/blank lines */
        while (*p && (is_space(*p) || is_newline(*p))) p++;
        if (!*p) break;

        /* Comment */
        if (*p == '#' || *p == ';') {
            while (*p && !is_newline(*p)) p++;
            continue;
        }

        /* Section header: [entry:Name] */
        if (*p == '[') {
            p++;
            if (str_starts(p, "entry:")) {
                p += 6; /* skip "entry:" */
                if (cfg->num_entries < MAX_ENTRIES) {
                    cur = &cfg->entries[cfg->num_entries++];
                    /* Zero the entry */
                    for (u32 i = 0; i < sizeof(BootEntry); i++)
                        ((u8*)cur)[i] = 0;
                    cur->type      = OS_TYPE_ELF64; /* default */
                    cur->luks_tries = 3;            /* default passphrase attempts */

                    u32 ni = 0;
                    while (*p && *p != ']' && !is_newline(*p)) {
                        if (ni + 1 < MAX_STR_LEN)
                            cur->name[ni++] = *p;
                        p++;
                    }
                    cur->name[ni] = 0;
                }
            }
            while (*p && *p != ']') p++;
            if (*p == ']') p++;
            continue;
        }

        /* Collect the full line */
        char line[MAX_STR_LEN * 2];
        u32 li = 0;
        while (*p && !is_newline(*p) && li + 1 < sizeof(line))
            line[li++] = *p++;
        line[li] = 0;

        char key[64], val[MAX_STR_LEN];
        if (!parse_kv(line, key, sizeof(key), val, sizeof(val)))
            continue;

        if (!cur) {
            /* Global settings — only applied on full parse (not merge) */
            if (!merge) {
                if (str_eq(key, "timeout")) {
                    u32 t = 0;
                    for (const char *d = val; *d >= '0' && *d <= '9'; d++)
                        t = t * 10 + (*d - '0');
                    cfg->timeout = t;
                } else if (str_eq(key, "default")) {
                    u32 d = 0;
                    for (const char *c = val; *c >= '0' && *c <= '9'; c++)
                        d = d * 10 + (*c - '0');
                    cfg->default_entry = d;
                } else if (str_eq(key, "accent")) {
                    int c = config_parse_color(val);
                    if (c >= 0) cfg->accent_color = (u8)c;
                } else if (str_eq(key, "theme")) {
                    int c = config_parse_color(val);
                    if (c >= 0 && c <= 7) cfg->theme_color = (u8)c;
                }
            }
        } else {
            /* Entry-specific settings */
            if      (str_eq(key, "type"))   cur->type = config_parse_type(val);
            else if (str_eq(key, "kernel")) str_copy(cur->kernel,  val, MAX_STR_LEN);
            else if (str_eq(key, "initrd"))      str_copy(cur->initrd,       val, MAX_STR_LEN);
            else if (str_eq(key, "cmdline"))     str_copy(cur->cmdline,      val, MAX_STR_LEN);
            else if (str_eq(key, "encrypted"))   cur->encrypted    = (val[0]=='1'||val[0]=='y'||val[0]=='t') ? 1 : 0;
            else if (str_eq(key, "luks_keyfile"))str_copy(cur->luks_keyfile, val, MAX_STR_LEN);
            else if (str_eq(key, "luks_tries")) {
                int t=0; for(int i=0;val[i]>='0'&&val[i]<='9';i++) t=t*10+(val[i]-'0');
                cur->luks_tries = (t>0)?t:3;
            }
        }
    }

    /* Clamp default index */
    if (cfg->default_entry >= cfg->num_entries && cfg->num_entries > 0)
        cfg->default_entry = 0;

    return cfg->num_entries > 0 ? 0 : -1;
}

int config_parse(const char *buf, u32 len, BootConfig *cfg) {
    return do_parse(buf, len, cfg, false);
}

/* Like config_parse but appends entries to an already-populated config.
 * Global settings (timeout, default, theme, accent) are preserved from
 * the first config_parse call — only new boot entries are added. */
int config_parse_merge(const char *buf, u32 len, BootConfig *cfg) {
    return do_parse(buf, len, cfg, true);
}

/* ------------------------------------------------------------------ */
/* Color name → EFI color index                                        */
/* ------------------------------------------------------------------ */

int config_parse_color(const char *name) {
    if (str_eq(name, "black"))        return 0;
    if (str_eq(name, "blue"))         return 1;
    if (str_eq(name, "green"))        return 2;
    if (str_eq(name, "cyan"))         return 3;
    if (str_eq(name, "red"))          return 4;
    if (str_eq(name, "magenta"))      return 5;
    if (str_eq(name, "pink"))         return 5;
    if (str_eq(name, "brown"))        return 6;
    if (str_eq(name, "orange"))       return 6;
    if (str_eq(name, "lightgray"))    return 7;
    if (str_eq(name, "darkgray"))     return 8;
    if (str_eq(name, "lightblue"))    return 9;
    if (str_eq(name, "lightgreen"))   return 10;
    if (str_eq(name, "lightcyan"))    return 11;
    if (str_eq(name, "lightred"))     return 12;
    if (str_eq(name, "lightmagenta")) return 13;
    if (str_eq(name, "yellow"))       return 14;
    if (str_eq(name, "white"))        return 15;
    return -1;
}
