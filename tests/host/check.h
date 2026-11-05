// Minimal test helper, so the host tests need nothing but a C compiler.

#pragma once

#include <stdio.h>

static int check_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond); \
            check_failures++;                                      \
        }                                                          \
    } while (0)

static inline int check_result(const char *name) {
    if (check_failures == 0) {
        printf("%s: all tests passed\n", name);
    }
    return check_failures != 0;
}
