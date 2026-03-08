/*
 * sakuru_test.h — Minimal, zero-dependency C11 unit test framework
 *
 * Usage:
 *   #include "framework/sakuru_test.h"
 *
 *   TEST(my_suite, my_test) {
 *       EXPECT_EQ(1 + 1, 2);
 *       ASSERT_NE(ptr, NULL);
 *   }
 *
 *   int main(void) { return sakuru_run_all(); }
 *
 * Build with -DSAKURU_HOST_TEST so common/ headers skip freestanding deps.
 */
#pragma once

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ── ANSI colours ─────────────────────────────────────────────────────── */
#define _ST_RED     "\x1b[31m"
#define _ST_GREEN   "\x1b[32m"
#define _ST_YELLOW  "\x1b[33m"
#define _ST_CYAN    "\x1b[36m"
#define _ST_BOLD    "\x1b[1m"
#define _ST_DIM     "\x1b[2m"
#define _ST_RESET   "\x1b[0m"

/* ── Internal state ───────────────────────────────────────────────────── */
typedef struct {
    const char *suite;
    const char *name;
    void (*fn)(void);
} _SakuruTest;

#define _ST_MAX_TESTS 512

static _SakuruTest _st_tests[_ST_MAX_TESTS];
static int         _st_count        = 0;
static int         _st_passed       = 0;
static int         _st_failed       = 0;
static int         _st_cur_failures = 0; /* failures in current test */
static const char *_st_cur_suite    = NULL;
static const char *_st_cur_name     = NULL;

/* ── Registration ─────────────────────────────────────────────────────── */
static inline void _st_register(const char *suite, const char *name,
                                 void (*fn)(void)) {
    if (_st_count < _ST_MAX_TESTS)
        _st_tests[_st_count++] = (_SakuruTest){ suite, name, fn };
}

/* TEST(suite, name) { ... } */
#define TEST(suite, name)                                                     \
    static void _st_fn_##suite##_##name(void);                               \
    static void __attribute__((constructor)) _st_reg_##suite##_##name(void) {\
        _st_register(#suite, #name, _st_fn_##suite##_##name);                \
    }                                                                         \
    static void _st_fn_##suite##_##name(void)

/* ── Assertion helpers ────────────────────────────────────────────────── */
#define _ST_FAIL(fmt, ...)                                                    \
    do {                                                                      \
        fprintf(stderr, "    " _ST_RED "FAIL" _ST_RESET " %s:%d  " fmt "\n",\
                __FILE__, __LINE__, ##__VA_ARGS__);                           \
        _st_cur_failures++;                                                   \
    } while (0)

/* EXPECT — records failure but continues test */
#define EXPECT_TRUE(expr)                                                     \
    do { if (!(expr)) _ST_FAIL("Expected TRUE: %s", #expr); } while(0)

#define EXPECT_FALSE(expr)                                                    \
    do { if (expr) _ST_FAIL("Expected FALSE: %s", #expr); } while(0)

#define EXPECT_EQ(a, b)                                                       \
    do { if (!((a) == (b)))                                                   \
        _ST_FAIL("%s == %s  (got %lld vs %lld)",                             \
                 #a, #b, (long long)(a), (long long)(b)); } while(0)

#define EXPECT_NE(a, b)                                                       \
    do { if ((a) == (b))                                                      \
        _ST_FAIL("%s != %s  (both %lld)", #a, #b, (long long)(a)); } while(0)

#define EXPECT_LT(a, b)                                                       \
    do { if (!((a) < (b)))                                                    \
        _ST_FAIL("%s < %s  (%lld vs %lld)",                                  \
                 #a, #b, (long long)(a), (long long)(b)); } while(0)

#define EXPECT_LE(a, b)                                                       \
    do { if (!((a) <= (b)))                                                   \
        _ST_FAIL("%s <= %s  (%lld vs %lld)",                                 \
                 #a, #b, (long long)(a), (long long)(b)); } while(0)

#define EXPECT_GT(a, b)                                                       \
    do { if (!((a) > (b)))                                                    \
        _ST_FAIL("%s > %s  (%lld vs %lld)",                                  \
                 #a, #b, (long long)(a), (long long)(b)); } while(0)

#define EXPECT_STREQ(a, b)                                                    \
    do { if (strcmp((a),(b)) != 0)                                            \
        _ST_FAIL("STREQ: \"%s\" vs \"%s\"", (a), (b)); } while(0)

#define EXPECT_STRNE(a, b)                                                    \
    do { if (strcmp((a),(b)) == 0)                                            \
        _ST_FAIL("STRNE: both \"%s\"", (a)); } while(0)

#define EXPECT_NULL(p)                                                        \
    do { if ((p) != NULL)                                                     \
        _ST_FAIL("Expected NULL: %s", #p); } while(0)

#define EXPECT_NOT_NULL(p)                                                    \
    do { if ((p) == NULL)                                                     \
        _ST_FAIL("Expected non-NULL: %s", #p); } while(0)

/* Byte-buffer comparison — prints first differing byte */
#define EXPECT_MEM_EQ(a, b, len)                                             \
    do {                                                                      \
        const uint8_t *_a = (const uint8_t *)(a);                           \
        const uint8_t *_b = (const uint8_t *)(b);                           \
        for (size_t _i = 0; _i < (size_t)(len); _i++) {                     \
            if (_a[_i] != _b[_i]) {                                          \
                _ST_FAIL("MEM_EQ mismatch at byte %zu: 0x%02x vs 0x%02x",   \
                         _i, _a[_i], _b[_i]);                                \
                break;                                                        \
            }                                                                 \
        }                                                                     \
    } while(0)

/* ASSERT — records failure AND aborts current test immediately */
#define ASSERT_TRUE(expr)                                                     \
    do { if (!(expr)) { _ST_FAIL("ASSERT TRUE: %s", #expr); return; } } while(0)

#define ASSERT_EQ(a, b)                                                       \
    do { if (!((a) == (b))) {                                                 \
        _ST_FAIL("ASSERT EQ: %s == %s  (%lld vs %lld)",                      \
                 #a, #b, (long long)(a), (long long)(b)); return; } } while(0)

#define ASSERT_NOT_NULL(p)                                                    \
    do { if ((p) == NULL) {                                                   \
        _ST_FAIL("ASSERT NOT_NULL: %s", #p); return; } } while(0)

/* ── Runner ───────────────────────────────────────────────────────────── */
static inline int sakuru_run_all(void) {
    printf(_ST_BOLD "\n  SakuruBoot Test Suite\n" _ST_RESET);
    printf(_ST_DIM  "  ─────────────────────────────────────────\n" _ST_RESET);

    const char *last_suite = NULL;

    for (int i = 0; i < _st_count; i++) {
        _SakuruTest *t = &_st_tests[i];
        _st_cur_suite    = t->suite;
        _st_cur_name     = t->name;
        _st_cur_failures = 0;

        if (!last_suite || strcmp(last_suite, t->suite) != 0) {
            printf("\n  " _ST_CYAN _ST_BOLD "%s" _ST_RESET "\n", t->suite);
            last_suite = t->suite;
        }

        printf("    " _ST_DIM "%-42s" _ST_RESET, t->name);
        fflush(stdout);

        t->fn();

        if (_st_cur_failures == 0) {
            printf(_ST_GREEN " PASS\n" _ST_RESET);
            _st_passed++;
        } else {
            printf(_ST_RED   " FAIL (%d assertion%s)\n" _ST_RESET,
                   _st_cur_failures, _st_cur_failures == 1 ? "" : "s");
            _st_failed++;
        }
    }

    printf("\n  " _ST_DIM "─────────────────────────────────────────\n" _ST_RESET);
    if (_st_failed == 0) {
        printf("  " _ST_GREEN _ST_BOLD "All %d tests passed." _ST_RESET "\n\n",
               _st_passed);
    } else {
        printf("  " _ST_RED _ST_BOLD "%d/%d tests FAILED." _ST_RESET "\n\n",
               _st_failed, _st_passed + _st_failed);
    }

    return _st_failed == 0 ? 0 : 1;
}
