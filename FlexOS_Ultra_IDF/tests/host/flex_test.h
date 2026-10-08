// Mini marco de pruebas de host (sin dependencias).
#pragma once
#include <stdio.h>
#include <stdlib.h>

extern int g_fails, g_checks;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_fails++;                                                                 \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                              \
    } while (0)

#define CHECK_EQ_I(a, b)                                                               \
    do {                                                                               \
        long long _a = (long long)(a), _b = (long long)(b);                            \
        g_checks++;                                                                    \
        if (_a != _b) {                                                                \
            g_fails++;                                                                 \
            fprintf(stderr, "FALLO %s:%d: %s == %lld, esperado %lld\n", __FILE__, __LINE__, #a, _a, _b); \
        }                                                                              \
    } while (0)

void test_flex_kv(void);
void test_flex_kv_threads(void);
void test_fs_path(void);
void test_flip_rows(void);
void test_wallpaper(void);
void test_glass(void);
void test_home(void);
void test_home_load(void);
void test_touch_arb(void);
#define FLEX_APP_N_TEST 19
