// Tiny assertion helpers for the BL2VR unit tests. Dependency-free on purpose -
// the whole project ships with zero package-manager dependencies, and the tests
// should not add any. One suite = one .cpp with a main() that returns Summary().
#pragma once
#include <cstdio>
#include <cmath>

static int g_checks = 0;
static int g_fails  = 0;

static inline void CheckImpl(bool ok, const char* expr, const char* file, int line) {
    g_checks++;
    if (!ok) {
        g_fails++;
        std::printf("  FAIL %s(%d): %s\n", file, line, expr);
    }
}
static inline void CheckNearImpl(double got, double want, double eps,
                                 const char* expr, const char* file, int line) {
    g_checks++;
    if (!(std::fabs(got - want) <= eps)) {
        g_fails++;
        std::printf("  FAIL %s(%d): %s  got=%.9g want=%.9g (eps %.3g)\n",
                    file, line, expr, got, want, eps);
    }
}

#define CHECK(cond)           CheckImpl((cond) ? true : false, #cond, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) CheckNearImpl((double)(a), (double)(b), (eps), #a " ~= " #b, __FILE__, __LINE__)

static inline void Section(const char* name) { std::printf("-- %s\n", name); }

// Returns the process exit code: 0 = all green.
static inline int Summary(const char* suite) {
    std::printf("%s: %d checks, %d failed -> %s\n",
                suite, g_checks, g_fails, g_fails ? "FAIL" : "PASS");
    return g_fails ? 1 : 0;
}