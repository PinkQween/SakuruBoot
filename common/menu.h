#pragma once

#include "config.h"

/*
 * Platform-independent boot menu data.
 * The actual rendering (print_fn, input_fn) is platform-provided.
 */
typedef void     (*MenuPrintFn)(const char *str);
typedef void     (*MenuPrintIntFn)(int n);
typedef int      (*MenuReadKeyFn)(void);   /* Returns ASCII key or -1 */
typedef void     (*MenuClearFn)(void);
typedef void     (*MenuSetColorFn)(int fg, int bg);
typedef void     (*MenuSetCursorFn)(int row, int col);  /* optional */
typedef void     (*MenuShowCursorFn)(int on);            /* optional */

typedef struct {
    MenuPrintFn      print;
    MenuPrintIntFn   print_int;
    MenuReadKeyFn    read_key;
    MenuClearFn      clear;
    MenuSetColorFn   set_color;    /* optional, may be NULL */
    MenuSetCursorFn  set_cursor;   /* optional: reposition without clear */
    MenuShowCursorFn show_cursor;  /* optional: hide/show cursor */
    int              cols;         /* screen width in chars (0 = auto/80) */
    int              rows;         /* screen height in chars (0 = auto/25) */
} MenuOps;

/* Menu color indices (platform maps these however appropriate) */
#define MENU_COLOR_NORMAL    0
#define MENU_COLOR_HIGHLIGHT 1
#define MENU_COLOR_TITLE     2
#define MENU_COLOR_TIMER     3
#define MENU_COLOR_BORDER    4
#define MENU_COLOR_ACCENT    5  /* accent — yellow on highlight bg (selector arrow) */
#define MENU_COLOR_HINT      6  /* accent fg on black — hint/countdown text */

/*
 * Box-drawing — rendered on MAGENTA (pink) background so font "gaps"
 * at cell edges blend with the pink background, eliminating visible gaps.
 */
#define BOX_SEL "\xc2\xbb"        /* U+00BB » selector      */
#define BOX_DTL "\xe2\x95\x94"    /* U+2554 ╔ top-left      */
#define BOX_DTR "\xe2\x95\x97"    /* U+2557 ╗ top-right     */
#define BOX_DBL "\xe2\x95\x9a"    /* U+255A ╚ bottom-left   */
#define BOX_DBR "\xe2\x95\x9d"    /* U+255D ╝ bottom-right  */
#define BOX_DH  "\xe2\x95\x90"    /* U+2550 ═ horizontal    */
#define BOX_DV  "\xe2\x95\x91"    /* U+2551 ║ vertical      */
#define BOX_LT  "\xe2\x95\xa0"    /* U+2560 ╠ left T-joint  */
#define BOX_RT  "\xe2\x95\xa3"    /* U+2563 ╣ right T-joint */

/*
 * Display the boot menu and wait for user selection.
 * Returns the index of the selected entry (0-based).
 */
int menu_run(const BootConfig *cfg, const MenuOps *ops);
