/*
 * tests/unit/test_config.c — Unit tests for the config parser
 *
 * Exercises: config_parse(), config_parse_type(), config_parse_color(),
 *            config_guess_type(), config_make_name(), global keys,
 *            entry keys, boolean fields, edge cases.
 */
#define SAKURU_HOST_TEST
#include "../framework/sakuru_test.h"

/* Stub out freestanding bits that config.c doesn't actually need on host */
#include <stdint.h>
#include <stdbool.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uintptr_t usize;

#include "../../common/config.h"
#include "../../common/config.c"   /* single-TU include for simplicity */

/* ── Global keys ─────────────────────────────────────────────────────── */
TEST(Config, GlobalTimeout) {
    static const char cfg[] = "timeout = 10\n";
    BootConfig bc; bc.num_entries = 0;
    config_parse(cfg, sizeof(cfg)-1, &bc);
    EXPECT_EQ(bc.timeout, 10u);
}

TEST(Config, GlobalDefault) {
    static const char cfg[] =
        "timeout = 3\ndefault = 2\n"
        "[entry:A]\ntype=linux\nkernel=/vmlinuz\n"
        "[entry:B]\ntype=linux\nkernel=/vmlinuz\n"
        "[entry:C]\ntype=linux\nkernel=/vmlinuz\n";
    BootConfig bc; bc.num_entries = 0;
    config_parse(cfg, (u32)strlen(cfg), &bc);
    EXPECT_EQ(bc.default_entry, 2u);
    EXPECT_EQ(bc.num_entries, 3u);
}

TEST(Config, DefaultClamped) {
    /* default index > num_entries must be clamped to 0 */
    static const char cfg[] =
        "default = 99\n[entry:Only]\ntype=linux\nkernel=/vmlinuz\n";
    BootConfig bc; bc.num_entries = 0;
    config_parse(cfg, (u32)strlen(cfg), &bc);
    EXPECT_EQ(bc.default_entry, 0u);
}

TEST(Config, ThemeAndAccentColors) {
    static const char cfg[] = "theme = blue\naccent = yellow\n";
    BootConfig bc; bc.num_entries = 1; /* prevent -1 return */
    /* hack: add a dummy entry so parse doesn't return -1 */
    static const char full[] =
        "theme = blue\naccent = yellow\n"
        "[entry:X]\ntype=linux\nkernel=/vmlinuz\n";
    config_parse(full, (u32)strlen(full), &bc);
    EXPECT_EQ(bc.theme_color, 1u);   /* blue = 1 */
    EXPECT_EQ(bc.accent_color, 14u); /* yellow = 14 */
}

/* ── Entry keys ──────────────────────────────────────────────────────── */
TEST(Config, LinuxEntryParsed) {
    static const char cfg[] =
        "[entry:Arch]\n"
        "type    = linux\n"
        "kernel  = /vmlinuz-linux\n"
        "initrd  = /initramfs-linux.img\n"
        "cmdline = root=/dev/sda2 rw quiet\n";
    BootConfig bc; bc.num_entries = 0;
    int r = config_parse(cfg, (u32)strlen(cfg), &bc);
    EXPECT_EQ(r, 0);
    ASSERT_EQ(bc.num_entries, 1u);
    EXPECT_STREQ(bc.entries[0].name,    "Arch");
    EXPECT_STREQ(bc.entries[0].kernel,  "/vmlinuz-linux");
    EXPECT_STREQ(bc.entries[0].initrd,  "/initramfs-linux.img");
    EXPECT_STREQ(bc.entries[0].cmdline, "root=/dev/sda2 rw quiet");
    EXPECT_EQ(bc.entries[0].type, OS_TYPE_LINUX);
}

TEST(Config, WindowsEntryParsed) {
    static const char cfg[] =
        "[entry:Windows 11]\n"
        "type = windows\n";
    BootConfig bc; bc.num_entries = 0;
    config_parse(cfg, (u32)strlen(cfg), &bc);
    ASSERT_EQ(bc.num_entries, 1u);
    EXPECT_EQ(bc.entries[0].type, OS_TYPE_WINDOWS);
}

TEST(Config, EncryptedBoolVariants) {
    static const char cfg_yes[]  = "[entry:A]\ntype=linux\nkernel=/k\nencrypted=yes\n";
    static const char cfg_one[]  = "[entry:A]\ntype=linux\nkernel=/k\nencrypted=1\n";
    static const char cfg_true[] = "[entry:A]\ntype=linux\nkernel=/k\nencrypted=true\n";
    static const char cfg_no[]   = "[entry:A]\ntype=linux\nkernel=/k\nencrypted=no\n";

    BootConfig bc;
    bc.num_entries = 0; config_parse(cfg_yes,  (u32)strlen(cfg_yes),  &bc);
    EXPECT_EQ(bc.entries[0].encrypted, 1);
    bc.num_entries = 0; config_parse(cfg_one,  (u32)strlen(cfg_one),  &bc);
    EXPECT_EQ(bc.entries[0].encrypted, 1);
    bc.num_entries = 0; config_parse(cfg_true, (u32)strlen(cfg_true), &bc);
    EXPECT_EQ(bc.entries[0].encrypted, 1);
    bc.num_entries = 0; config_parse(cfg_no,   (u32)strlen(cfg_no),   &bc);
    EXPECT_EQ(bc.entries[0].encrypted, 0);
}

TEST(Config, LuksTries) {
    static const char cfg[] =
        "[entry:A]\ntype=linux\nkernel=/k\nencrypted=yes\nluks_tries=7\n";
    BootConfig bc; bc.num_entries = 0;
    config_parse(cfg, (u32)strlen(cfg), &bc);
    EXPECT_EQ(bc.entries[0].luks_tries, 7);
}

TEST(Config, LuksTriesDefaultIsThree) {
    static const char cfg[] = "[entry:A]\ntype=linux\nkernel=/k\n";
    BootConfig bc; bc.num_entries = 0;
    config_parse(cfg, (u32)strlen(cfg), &bc);
    EXPECT_EQ(bc.entries[0].luks_tries, 3);
}

/* ── Type parsing ────────────────────────────────────────────────────── */
TEST(Config, ParseTypeStrings) {
    EXPECT_EQ(config_parse_type("linux"),      OS_TYPE_LINUX);
    EXPECT_EQ(config_parse_type("elf64"),      OS_TYPE_ELF64);
    EXPECT_EQ(config_parse_type("multiboot2"), OS_TYPE_MULTIBOOT2);
    EXPECT_EQ(config_parse_type("windows"),    OS_TYPE_WINDOWS);
    EXPECT_EQ(config_parse_type("bogus"),      OS_TYPE_UNKNOWN);
    EXPECT_EQ(config_parse_type(""),           OS_TYPE_UNKNOWN);
}

/* ── Color parsing ───────────────────────────────────────────────────── */
TEST(Config, ColorNames) {
    EXPECT_EQ(config_parse_color("black"),         0);
    EXPECT_EQ(config_parse_color("blue"),          1);
    EXPECT_EQ(config_parse_color("cyan"),          3);
    EXPECT_EQ(config_parse_color("magenta"),       5);
    EXPECT_EQ(config_parse_color("yellow"),       14);
    EXPECT_EQ(config_parse_color("white"),        15);
    EXPECT_EQ(config_parse_color("notacolor"),    -1);
}

/* ── Filename type guessing ──────────────────────────────────────────── */
TEST(Config, GuessTypeFromFilename) {
    EXPECT_EQ(config_guess_type("vmlinuz"),          OS_TYPE_LINUX);
    EXPECT_EQ(config_guess_type("vmlinuz-6.12"),     OS_TYPE_LINUX);
    EXPECT_EQ(config_guess_type("bzImage"),          OS_TYPE_LINUX);
    EXPECT_EQ(config_guess_type("kernel.elf"),       OS_TYPE_ELF64);
    EXPECT_EQ(config_guess_type("bootmgfw.efi"),     OS_TYPE_WINDOWS);
    EXPECT_EQ(config_guess_type("bootmgr.efi"),      OS_TYPE_WINDOWS);
    EXPECT_EQ(config_guess_type("random.bin"),       OS_TYPE_UNKNOWN);
}

/* ── Name generation ─────────────────────────────────────────────────── */
TEST(Config, MakeNameLinux) {
    char out[64];
    config_make_name("/boot", "vmlinuz", OS_TYPE_LINUX, out, 64);
    EXPECT_STREQ(out, "Linux");
}

TEST(Config, MakeNameLinuxVersion) {
    char out[64];
    config_make_name("/boot", "vmlinuz-6.12.1", OS_TYPE_LINUX, out, 64);
    EXPECT_STREQ(out, "Linux 6.12.1");
}

TEST(Config, MakeNameWindows) {
    char out[64];
    config_make_name("/EFI/Microsoft/Boot", "bootmgfw.efi",
                     OS_TYPE_WINDOWS, out, 64);
    EXPECT_STREQ(out, "Windows");
}

/* ── Edge cases ──────────────────────────────────────────────────────── */
TEST(Config, EmptyConfigReturnsError) {
    BootConfig bc; bc.num_entries = 0;
    int r = config_parse("", 0, &bc);
    EXPECT_EQ(r, -1);
}

TEST(Config, CommentsIgnored) {
    static const char cfg[] =
        "; this is a comment\n"
        "# also a comment\n"
        "[entry:Test]\n"
        "; kernel = /wrong\n"
        "type=linux\nkernel=/correct\n";
    BootConfig bc; bc.num_entries = 0;
    config_parse(cfg, (u32)strlen(cfg), &bc);
    ASSERT_EQ(bc.num_entries, 1u);
    EXPECT_STREQ(bc.entries[0].kernel, "/correct");
}

TEST(Config, MaxEntries) {
    char buf[MAX_ENTRIES * 60 + 64];
    int pos = 0;
    for (int i = 0; i < MAX_ENTRIES + 2; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "[entry:E%d]\ntype=linux\nkernel=/k%d\n", i, i);
    }
    BootConfig bc; bc.num_entries = 0;
    config_parse(buf, (u32)pos, &bc);
    EXPECT_EQ(bc.num_entries, (u32)MAX_ENTRIES);
}

TEST(Config, MergePreservesGlobals) {
    static const char base[] =
        "timeout = 8\n[entry:A]\ntype=linux\nkernel=/a\n";
    static const char extra[] =
        "timeout = 99\n[entry:B]\ntype=linux\nkernel=/b\n";
    BootConfig bc; bc.num_entries = 0;
    config_parse(base,  (u32)strlen(base),  &bc);
    config_parse_merge(extra, (u32)strlen(extra), &bc);
    /* timeout should NOT be overwritten by merge */
    EXPECT_EQ(bc.timeout, 8u);
    EXPECT_EQ(bc.num_entries, 2u);
}

int main(void) { return sakuru_run_all(); }
