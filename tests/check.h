// Minimal test helpers: CHECK(cond) and CHECK_EQ(a, b) record failures and
// keep going; finish() prints a summary and returns the exit code.
#pragma once
#include <cstdio>

static int g_checks = 0, g_failures = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) {                                                                \
            ++g_failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                \
    do {                                                                              \
        ++g_checks;                                                                   \
        long long va_ = (long long)(a), vb_ = (long long)(b);                         \
        if (va_ != vb_) {                                                             \
            ++g_failures;                                                             \
            std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, \
                        #a, #b, va_, vb_);                                            \
        }                                                                             \
    } while (0)

static int finish(const char* name)
{
    std::printf("%s: %d checks, %d failed\n", name, g_checks, g_failures);
    return g_failures ? 1 : 0;
}
