#pragma once

#include <stdint.h>
#include <stdio.h>

static int _test_count = 0;
static int _test_pass = 0;
static int _test_fail = 0;

typedef bool (*test_fn)();

static inline void run_test(const char *name, test_fn fn) {
    _test_count++;
    printf("  %s ... ", name);
    if (fn()) {
        _test_pass++;
        printf("ok\n");
    } else {
        _test_fail++;
    }
}

static inline int test_summary() {
    printf("\n%d tests: %d passed, %d failed\n", _test_count, _test_pass,
           _test_fail);
    return _test_fail > 0 ? 1 : 0;
}

#define RUN(fn) run_test(#fn, fn)

#define ASSERT(cond)                                                       \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            return false;                                                  \
        }                                                                  \
    } while (0)

#define ASSERT_EQ(a, b)                                                    \
    do {                                                                   \
        auto _a = (a);                                                     \
        auto _b = (b);                                                     \
        if (_a != _b) {                                                    \
            printf("FAIL\n    %s:%d: %s == %s (got %ld vs %ld)\n",         \
                   __FILE__, __LINE__, #a, #b, (long)_a, (long)_b);        \
            return false;                                                  \
        }                                                                  \
    } while (0)
