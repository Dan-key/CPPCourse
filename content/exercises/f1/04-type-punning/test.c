/*
 * Тест-харнес для F1-04: Type Punning.
 * Не изменяй этот файл.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -fstrict-aliasing \
 *       -fsanitize=address,undefined -O1 -g starter.c test.c -o prog && ./prog
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>   /* memcmp */

/* Объявления из решения студента */
uint32_t float_bits_union(float f);
uint32_t float_bits_memcpy(float f);
uint32_t float_bits_cast(float f);
float    bits_to_float(uint32_t bits);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    printf("=== float_bits_union (union type punning — легально в C) ===\n");
    CHECK(float_bits_union(0.0f)  == 0x00000000u, "union: 0.0f  = 0x00000000");
    CHECK(float_bits_union(1.0f)  == 0x3F800000u, "union: 1.0f  = 0x3F800000");
    CHECK(float_bits_union(-0.0f) == 0x80000000u, "union: -0.0f = 0x80000000");
    CHECK(float_bits_union(2.0f)  == 0x40000000u, "union: 2.0f  = 0x40000000");

    printf("=== float_bits_memcpy (переносимо, нет UB) ===\n");
    CHECK(float_bits_memcpy(0.0f)  == 0x00000000u, "memcpy: 0.0f  = 0x00000000");
    CHECK(float_bits_memcpy(1.0f)  == 0x3F800000u, "memcpy: 1.0f  = 0x3F800000");
    CHECK(float_bits_memcpy(-0.0f) == 0x80000000u, "memcpy: -0.0f = 0x80000000");
    CHECK(float_bits_memcpy(2.0f)  == 0x40000000u, "memcpy: 2.0f  = 0x40000000");

    printf("=== float_bits_cast (демонстрация UB — результат совпадает на x86) ===\n");
    /*
     * На x86 с gcc/clang результат совпадает даже при -fstrict-aliasing,
     * потому что оптимизатор не всегда убирает такое чтение.
     * НО: это UB, и поведение impl-defined. В реальном коде: только memcpy.
     */
    CHECK(float_bits_cast(1.0f) == 0x3F800000u,
          "cast (UB!): 1.0f = 0x3F800000 — на x86 совпадает, но полагаться нельзя");

    printf("=== bits_to_float (обратное преобразование) ===\n");
    CHECK(bits_to_float(0x00000000u) == 0.0f,  "bits→float: 0x00000000 = 0.0f");
    CHECK(bits_to_float(0x3F800000u) == 1.0f,  "bits→float: 0x3F800000 = 1.0f");
    CHECK(bits_to_float(0x40000000u) == 2.0f,  "bits→float: 0x40000000 = 2.0f");

    printf("=== round-trip: float → bits → float ===\n");
    {
        float orig = 3.14f;
        uint32_t bits = float_bits_memcpy(orig);
        float recovered = bits_to_float(bits);
        /* memcmp: битовое сравнение, не ==  (== для float имеет свои нюансы) */
        CHECK(memcmp(&orig, &recovered, sizeof orig) == 0,
              "round-trip: 3.14f через memcpy → bits → float восстанавливается точно");
    }

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
