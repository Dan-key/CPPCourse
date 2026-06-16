#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

bool safe_less_si(int a, size_t b);
bool safe_less_iu(int a, unsigned b);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    printf("=== safe_less_si (int < size_t) ===\n");
    CHECK(safe_less_si(-1, 0),              "-1 < 0u → true");
    CHECK(safe_less_si(-1, 1),              "-1 < 1u → true");
    CHECK(!safe_less_si(0, 0),              "0 < 0u → false");
    CHECK(safe_less_si(0, 1),               "0 < 1u → true");
    CHECK(!safe_less_si(1, 0),              "1 < 0u → false");
    CHECK(safe_less_si(1, 2),               "1 < 2u → true");
    CHECK(!safe_less_si(2, 1),              "2 < 1u → false");
    CHECK(safe_less_si(INT_MIN, 0),         "INT_MIN < 0u → true");
    CHECK(safe_less_si(INT_MAX, SIZE_MAX),  "INT_MAX < SIZE_MAX → true");
    CHECK(!safe_less_si(INT_MAX, (size_t)INT_MAX), "INT_MAX < INT_MAX(size_t) → false");

    printf("=== safe_less_iu (int < unsigned) ===\n");
    CHECK(safe_less_iu(-1, 0),              "-1 < 0u → true");
    CHECK(!safe_less_iu(0, 0),              "0 < 0u → false");
    CHECK(safe_less_iu(0, 1),               "0 < 1u → true");
    CHECK(safe_less_iu(INT_MIN, 0),         "INT_MIN < 0u → true");
    CHECK(!safe_less_iu(INT_MAX, (unsigned)INT_MAX), "INT_MAX < INT_MAX(u) → false");
    CHECK(safe_less_iu(INT_MAX, UINT_MAX),  "INT_MAX < UINT_MAX → true");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
