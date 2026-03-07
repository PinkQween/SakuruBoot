/*
 * SakuruBoot boot menu — platform-independent logic
 * Rendering is done via the MenuOps vtable.
 */

#include "menu.h"

#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_ENTER '\r'

static void print_str(const MenuOps *ops, const char *s) { ops->print(s); }

/*
 * Print string s exactly n times.
 * Single-byte strings are batched into ≤160-char chunks.
 * Multi-byte UTF-8 strings (box chars) are batched into ≤40-copy chunks
 * to avoid issuing one ops->print per glyph.
 */
static void print_n(const MenuOps *ops, const char *s, int n) {
    if (n <= 0 || !s || !s[0]) return;
    if (!s[1]) {
        /* Single-byte: fill a buffer and flush in ≤160-char chunks */
        char buf[161];
        while (n > 0) {
            int chunk = (n > 160) ? 160 : n;
            for (int i = 0; i < chunk; i++) buf[i] = s[0];
            buf[chunk] = 0;
            ops->print(buf);
            n -= chunk;
        }
    } else {
        /* Multi-byte UTF-8: batch up to 40 copies per ops->print call */
        int slen = 0; while (s[slen]) slen++;
        char buf[241]; /* 40 × 6-byte UTF-8 + NUL */
        while (n > 0) {
            int chunk = (n > 40) ? 40 : n;
            int pos = 0;
            for (int i = 0; i < chunk; i++)
                for (int j = 0; j < slen; j++) buf[pos++] = s[j];
            buf[pos] = 0;
            ops->print(buf);
            n -= chunk;
        }
    }
}

static void print_int(const MenuOps *ops, int n) {
    char buf[16]; int i = 15; buf[i--] = 0;
    if (n == 0) { buf[i--] = '0'; }
    while (n > 0) { buf[i--] = '0' + (n % 10); n /= 10; }
    ops->print(&buf[i + 1]);
}

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

/*
 * Layout — Unicode box chars on MAGENTA (cherry-pink) background.
 * Font "gap" areas in each cell are filled with pink bg, so they blend
 * with adjacent pink chars — no visible gaps between wall/border chars.
 *
 *   ╔════════════════════════════╗  ← pink bg box chars
 *   ║        SakuruBoot          ║  ← title yellow on pink
 *   ╠════════════════════════════╣  ← divider on pink
 *   ║   Entry 0                  ║  ← black bg content, pink walls
 *   ║ » Entry 1                  ║  ← highlight band (pink), pink walls
 *   ╚════════════════════════════╝
 *        UP/DOWN  ENTER Boot         ← hint outside box
 *
 * Row count: N + 4 (top/title/divider/bottom) + 1 hint.
 * inner = bw-2; rpad = inner-5-nlen (same for selected and non-selected).
 */
static void render_menu(const BootConfig *cfg, int selected,
                        int countdown, const MenuOps *ops, int start_row) {
    int cols  = (ops->cols > 0) ? ops->cols : 80;

    int bw    = cols - 10;
    if (bw < 40) bw = 40;
    if (bw > 78) bw = 78;
    int lpad    = (cols - bw) / 2;
    if (lpad < 0) lpad = 0;
    int rmargin = cols - lpad - bw;
    if (rmargin < 0) rmargin = 0;
    int inner   = bw - 2;

    if (start_row >= 0 && ops->set_cursor) {
        ops->set_cursor(start_row, 0);
    } else if (ops->clear) {
        ops->clear();
    }

/* lpad black spaces (screen outside box) */
#define LMARGIN() do { \
    if (ops->set_color) ops->set_color(MENU_COLOR_NORMAL, 0); \
    print_n(ops, " ", lpad); } while(0)

/* End a row: just newline — no padding.  Padding to `cols` chars causes
 * UEFI auto-wrap + explicit \n = blank row.  Let firmware clear the rest. */
#define ENDLINE() do { print_str(ops, "\n"); } while(0)

/* Vertical wall: ║ rendered with LIGHTMAGENTA fg on MAGENTA bg.
 * The bg fills the font's top/bottom cell gaps with pink, matching the
 * ║ stroke colour — the wall appears fully continuous across rows. */
#define WALL() do { \
    if (ops->set_color) ops->set_color(MENU_COLOR_BORDER, 0); \
    print_str(ops, BOX_DV); } while(0)

    /* ── Top border ─────────────────────────────────────────────── */
    LMARGIN();
    if (ops->set_color) ops->set_color(MENU_COLOR_BORDER, 0);
    print_str(ops, BOX_DTL); print_n(ops, BOX_DH, inner); print_str(ops, BOX_DTR);
    ENDLINE();

    /* ── Title ──────────────────────────────────────────────────── */
    {
        const char *title = "SakuruBoot  v" SAKURUBOOT_VERSION;
        int tlen  = str_len(title);
        int tlpad = (inner - tlen) / 2;
        int trpad = inner - tlen - tlpad;
        LMARGIN();
        WALL();
        if (ops->set_color) ops->set_color(MENU_COLOR_NORMAL, 0);
        print_n(ops, " ", tlpad);
        if (ops->set_color) ops->set_color(MENU_COLOR_TITLE, 0);
        print_str(ops, title);
        if (ops->set_color) ops->set_color(MENU_COLOR_NORMAL, 0);
        print_n(ops, " ", trpad);
        WALL();
        ENDLINE();
    }

    /* ── Divider ─────────────────────────────────────────────────── */
    LMARGIN();
    if (ops->set_color) ops->set_color(MENU_COLOR_BORDER, 0);
    print_str(ops, BOX_LT); print_n(ops, BOX_DH, inner); print_str(ops, BOX_RT);
    ENDLINE();

    /* ── Entries ─────────────────────────────────────────────────── */
    for (u32 i = 0; i < cfg->num_entries; i++) {
        bool sel  = (i == (u32)selected);
        int  nlen = str_len(cfg->entries[i].name);
        /* Both layouts: ║ + 1gap + (2sp + » + 1sp) + name + rpad + 1gap + ║ = bw
         * inner = 1+1+1+1+nlen+rpad+1 = 5+nlen+rpad → rpad = inner-5-nlen        */
        int  rpad = inner - 5 - nlen;
        if (rpad < 0) rpad = 0;

        LMARGIN();
        WALL();           /* left wall: magenta space, full-cell, no gaps */

        if (sel) {
            /* black gap | pink highlight | black gap — never touches walls */
            if (ops->set_color) ops->set_color(MENU_COLOR_NORMAL, 0);
            print_str(ops, " ");
            if (ops->set_color) ops->set_color(MENU_COLOR_HIGHLIGHT, 0);
            print_str(ops, " ");
            if (ops->set_color) ops->set_color(MENU_COLOR_ACCENT, 0);
            print_str(ops, BOX_SEL);
            if (ops->set_color) ops->set_color(MENU_COLOR_HIGHLIGHT, 0);
            print_str(ops, " ");
            print_str(ops, cfg->entries[i].name);
            print_n(ops, " ", rpad);
            if (ops->set_color) ops->set_color(MENU_COLOR_NORMAL, 0);
            print_str(ops, " ");
        } else {
            if (ops->set_color) ops->set_color(MENU_COLOR_NORMAL, 0);
            print_str(ops, "    ");
            print_str(ops, cfg->entries[i].name);
            print_n(ops, " ", rpad);
            print_str(ops, " ");
        }

        WALL();           /* right wall */
        ENDLINE();
    }

    /* ── Bottom border ───────────────────────────────────────────── */
    LMARGIN();
    if (ops->set_color) ops->set_color(MENU_COLOR_BORDER, 0);
    print_str(ops, BOX_DBL); print_n(ops, BOX_DH, inner); print_str(ops, BOX_DBR);
    ENDLINE();

    /* ── Hint / countdown (outside box, centred) ─────────────────── */
    {
        char line[81];
        int  llen;
        if (ops->set_color) ops->set_color(MENU_COLOR_HINT, 0);
        if (countdown > 0) {
            /* Keep same width as hint: "Booting in Ns..." = 16 chars max */
            const char *pre = "Booting in ";
            const char *suf = "s...";
            int plen = str_len(pre), slen2 = str_len(suf);
            int p = 0;
            for (int j = 0; j < plen; j++) line[p++] = pre[j];
            if (countdown >= 10) line[p++] = '0' + countdown / 10;
            line[p++] = '0' + countdown % 10;
            for (int j = 0; j < slen2; j++) line[p++] = suf[j];
            line[p] = 0; llen = p;
        } else {
            const char *hint = "UP/DOWN  Move    ENTER  Boot";
            llen = str_len(hint);
            for (int j = 0; j <= llen; j++) line[j] = hint[j];
        }
        int hpad = (cols - llen) / 2; if (hpad < 0) hpad = 0;
        print_n(ops, " ", hpad);
        print_str(ops, line);
        print_str(ops, "\n");
    }

#undef LMARGIN
#undef ENDLINE
#undef WALL
}

int menu_run(const BootConfig *cfg, const MenuOps *ops) {
    if (cfg->num_entries == 0) return 0;

    int selected  = (int)cfg->default_entry;
    int countdown = (int)cfg->timeout;
    bool counting = (countdown > 0);

    /* Keep cursor hidden for the entire menu session */
    if (ops->show_cursor) ops->show_cursor(0);

    render_menu(cfg, selected, counting ? countdown : 0, ops, -1);
    int start_row = 0;  /* ClearScreen leaves cursor at row 0 */

    while (1) {
        int key = ops->read_key();  /* -1 on ~1s timeout */

        if (key >= 0) {
            counting = false;
            if (key == KEY_UP) {
                selected = (selected > 0) ? selected - 1
                                          : (int)cfg->num_entries - 1;
            } else if (key == KEY_DOWN) {
                selected = ((u32)selected + 1 < cfg->num_entries)
                           ? selected + 1 : 0;
            } else if (key == KEY_ENTER || key == '\n') {
                if (ops->show_cursor) ops->show_cursor(1);
                return selected;
            }
            render_menu(cfg, selected, 0, ops, start_row);
        }

        if (counting) {
            countdown--;
            if (countdown <= 0) {
                if (ops->show_cursor) ops->show_cursor(1);
                return selected;
            }
            render_menu(cfg, selected, countdown, ops, start_row);
        }
    }
}


