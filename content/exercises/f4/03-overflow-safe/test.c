#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

int64_t  array_sum(const int32_t *a, size_t n);
int32_t  array_max(const int32_t *a, size_t n);
uint32_t low_bits_mask(unsigned k);

static int run = 0, pass = 0;
#define CHECK(cond, msg) do { run++; if (cond) { pass++; printf("  [OK]   %s\n", msg); } \
                              else printf("  [FAIL] %s\n", msg); } while (0)

int main(void)
{
    printf("=== 03-overflow-safe: тесты ===\n\n");

    printf("-- array_sum --\n");
    int32_t big[]   = { 2000000000, 2000000000, 2000000000 };
    CHECK(array_sum(big, 3) == 6000000000LL, "array_sum не переполняется (6e9)");
    int32_t small[] = { 1, 2, 3, 4 };
    CHECK(array_sum(small, 4) == 10,          "array_sum базовый случай");
    CHECK(array_sum(NULL, 0) == 0,            "array_sum пустого = 0");

    printf("-- array_max --\n");
    int32_t neg[] = { -5, -3, -9, -1, -7 };
    CHECK(array_max(neg, 5) == -1, "array_max из одних отрицательных");
    int32_t mix[] = { 3, -1, 7, 2 };
    CHECK(array_max(mix, 4) == 7,  "array_max смешанного массива");

    printf("-- low_bits_mask --\n");
    CHECK(low_bits_mask(0)  == 0u,          "low_bits_mask(0) == 0");
    CHECK(low_bits_mask(1)  == 1u,          "low_bits_mask(1) == 1");
    CHECK(low_bits_mask(8)  == 0xFFu,       "low_bits_mask(8) == 0xFF");
    CHECK(low_bits_mask(31) == 0x7FFFFFFFu, "low_bits_mask(31)");
    CHECK(low_bits_mask(32) == 0xFFFFFFFFu, "low_bits_mask(32) без UB");

    printf("\n%d/%d пройдено\n", pass, run);
    return (pass == run) ? 0 : 1;
}
