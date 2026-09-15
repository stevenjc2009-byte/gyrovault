#ifndef GV_TEST_H
#define GV_TEST_H

#include <stdio.h>

static int t_pass;
static int t_fail;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) {                                                            \
            t_pass++;                                                          \
        } else {                                                               \
            t_fail++;                                                          \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                      \
    } while (0)

/* Prints the per-suite line; returns non-zero on any failure or if nothing was checked. */
static int test_summary(const char *suite)
{
    printf("[%s] %d passed, %d failed\n", suite, t_pass, t_fail);
    return (t_fail != 0 || t_pass == 0) ? 1 : 0;
}

#endif
