/*
 * Тест-харнес для F1-03: Integer Promotions.
 * Компилируй вместе с решением студента (starter.c или solution.c).
 * Не изменяй этот файл.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
 *       -fsanitize=address,undefined -O1 -g starter.c test.c -o prog && ./prog
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

/* Объявления функций из решения студента */
int      uint8_sum_overflows(uint8_t a, uint8_t b);
uint8_t  bitwise_not_u8(uint8_t x);
int      signed_less_than_unsigned(void);
int      safe_lt(int a, unsigned b);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    printf("=== uint8_sum_overflows (продвижение до int предотвращает overflow) ===\n");
    /*
     * 255 + 255 = 510 в int — никакого переполнения.
     * uint8_t продвигается до int ДО сложения (§6.3.1.1).
     * Правильный ответ всегда 0.
     */
    CHECK(uint8_sum_overflows(255, 255) == 0,
          "uint8(255)+uint8(255) в int = 510, переполнения нет → 0");
    CHECK(uint8_sum_overflows(0, 0) == 0,
          "uint8(0)+uint8(0) = 0, переполнения нет → 0");
    CHECK(uint8_sum_overflows(128, 127) == 0,
          "uint8(128)+uint8(127) = 255 в int, нет overflow → 0");

    printf("=== bitwise_not_u8 (маскировка после продвижения) ===\n");
    /*
     * ~(uint8_t)x продвигается до int: ~(int)0 == (int)(-1).
     * Каст к uint8_t отрезает лишние биты: (uint8_t)(-1) == 255.
     */
    CHECK(bitwise_not_u8(0)    == 255,  "~uint8(0)    = 255 (0xFF)");
    CHECK(bitwise_not_u8(0xFF) == 0,    "~uint8(0xFF) = 0");
    CHECK(bitwise_not_u8(1)    == 254,  "~uint8(1)    = 254 (0xFE)");
    CHECK(bitwise_not_u8(0x0F) == 0xF0, "~uint8(0x0F) = 0xF0");

    printf("=== signed_less_than_unsigned (usual arithmetic conversions) ===\n");
    /*
     * (-1) < 0u:  int(-1) → unsigned UINT_MAX = 4294967295.
     * UINT_MAX > 0u  →  результат ЛОЖНЫЙ = 0.
     * Это «классический сюрприз» §6.3.1.8.
     */
    CHECK(signed_less_than_unsigned() == 0,
          "(-1) < 0u == 0  ((-1) приводится к UINT_MAX)");

    printf("=== safe_lt (честное математическое сравнение) ===\n");
    /*
     * safe_lt реализует: математически a < b ?
     * Правильный алгоритм: (a < 0) ? 1 : ((unsigned)a < b)
     */
    CHECK(safe_lt(-1,  0u) == 1, "safe_lt(-1, 0)    == 1  (-1 меньше 0)");
    CHECK(safe_lt(-1,  1u) == 1, "safe_lt(-1, 1)    == 1");
    CHECK(safe_lt( 0,  0u) == 0, "safe_lt(0,  0)    == 0  (равны)");
    CHECK(safe_lt( 0,  1u) == 1, "safe_lt(0,  1)    == 1");
    CHECK(safe_lt(100, 100u) == 0, "safe_lt(100, 100) == 0");
    CHECK(safe_lt(INT_MAX, 0u) == 0,
          "safe_lt(INT_MAX, 0)   == 0  (INT_MAX > 0)");
    CHECK(safe_lt(5, 10u) == 1, "safe_lt(5, 10)    == 1");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
