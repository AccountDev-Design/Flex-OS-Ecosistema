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
