#pragma once
#include <cmath>
#include <cstdio>

namespace test {
inline int g_failures = 0;
inline int g_checks   = 0;

inline void check(bool cond, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL %s:%d   %s\n", file, line, expr);
    }
}
inline bool approx(double a, double b, double eps = 1e-4) {
    return std::fabs(a - b) <= eps;
}
} // namespace test

#define CHECK(x)          ::test::check((x), #x, __FILE__, __LINE__)
#define CHECK_APPROX(a,b) ::test::check(::test::approx((a),(b)), #a " ~= " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a,b,e) ::test::check(::test::approx((a),(b),(e)), #a " ~= " #b, __FILE__, __LINE__)
