/*
 * Тест-харнес для overflow-safe arithmetic.
 * Этот файл компилируется вместе с решением студента.
 * Не изменяй этот файл.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

/* Объявления из решения студента */
bool add_safe_int(int a, int b, int *out);
bool mul_safe_int(int a, int b, int *out);
bool add_safe_size(size_t a, size_t b, size_t *out);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    int r = 0;
    size_t sr = 0;

    printf("=== add_safe_int ===\n");
    CHECK(!add_safe_int(1, 2, &r) && r == 3,         "add(1,2) = 3, no overflow");
    CHECK(!add_safe_int(0, 0, &r) && r == 0,         "add(0,0) = 0, no overflow");
    CHECK(!add_safe_int(-1, -2, &r) && r == -3,      "add(-1,-2) = -3, no overflow");
    CHECK(!add_safe_int(INT_MIN, 0, &r) && r==INT_MIN, "add(INT_MIN,0) no overflow");
    CHECK(add_safe_int(INT_MAX, 1, &r),              "add(INT_MAX,1) overflows");
    CHECK(add_safe_int(INT_MIN, -1, &r),             "add(INT_MIN,-1) overflows");
    CHECK(add_safe_int(INT_MAX, INT_MAX, &r),        "add(INT_MAX,INT_MAX) overflows");

    printf("=== mul_safe_int ===\n");
    CHECK(!mul_safe_int(2, 3, &r) && r == 6,         "mul(2,3) = 6, no overflow");
    CHECK(!mul_safe_int(0, INT_MAX, &r) && r == 0,   "mul(0,INT_MAX) = 0, no overflow");
    CHECK(!mul_safe_int(-1, 1, &r) && r == -1,       "mul(-1,1) = -1, no overflow");
    CHECK(mul_safe_int(INT_MAX, 2, &r),              "mul(INT_MAX,2) overflows");
    CHECK(mul_safe_int(INT_MIN, -1, &r),             "mul(INT_MIN,-1) overflows");
    CHECK(mul_safe_int(INT_MAX, INT_MAX, &r),        "mul(INT_MAX,INT_MAX) overflows");

    printf("=== add_safe_size ===\n");
    CHECK(!add_safe_size(1, 2, &sr) && sr == 3,      "size_add(1,2) = 3");
    CHECK(!add_safe_size(0, 0, &sr) && sr == 0,      "size_add(0,0) = 0");
    CHECK(add_safe_size(SIZE_MAX, 1, &sr),           "size_add(SIZE_MAX,1) overflows");
    CHECK(add_safe_size(SIZE_MAX, SIZE_MAX, &sr),    "size_add(SIZE_MAX,SIZE_MAX) overflows");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
