/*
 * Тест-харнес для EL1-01: Endian detect.
 * Не изменяй этот файл.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
 *       -fsanitize=address,undefined -O1 -g starter.c test.c -o prog && ./prog
 *
 * При кросс-компиляции (проверка что is_little_endian работает везде):
 *   aarch64-linux-gnu-gcc -std=c17 -Wall -O1 starter.c test.c -o prog_arm
 *   qemu-aarch64-static -L /usr/aarch64-linux-gnu ./prog_arm
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Объявления из решения студента */
int      is_little_endian(void);
uint32_t bswap32(uint32_t x);
uint32_t cpu_to_be32(uint32_t x);
uint32_t be32_to_cpu(uint32_t x);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    int le = is_little_endian();

    printf("=== is_little_endian ===\n");
    /* Возвращает только 0 или 1, не -1 и не другой мусор */
    CHECK(le == 0 || le == 1,
          "is_little_endian() возвращает 0 или 1 (не мусор)");

    /* На x86_64 хосте всегда little-endian */
#if defined(__x86_64__) || defined(__i386__)
    CHECK(le == 1,
          "На x86_64 хосте: is_little_endian() == 1");
#endif

    /* На AArch64 (обычный Linux) тоже little-endian */
#if defined(__aarch64__)
    CHECK(le == 1,
          "На aarch64 Linux: is_little_endian() == 1");
#endif

    printf("\n=== bswap32 ===\n");
    CHECK(bswap32(0x01020304u) == 0x04030201u,
          "bswap32(0x01020304) == 0x04030201");
    CHECK(bswap32(0x00000001u) == 0x01000000u,
          "bswap32(0x00000001) == 0x01000000");
    CHECK(bswap32(0x12345678u) == 0x78563412u,
          "bswap32(0x12345678) == 0x78563412");
    CHECK(bswap32(0x00000000u) == 0x00000000u,
          "bswap32(0x00000000) == 0x00000000");
    CHECK(bswap32(0xFFFFFFFFu) == 0xFFFFFFFFu,
          "bswap32(0xFFFFFFFF) == 0xFFFFFFFF (симметрично)");
    CHECK(bswap32(0xDEADBEEFu) == 0xEFBEADDEu,
          "bswap32(0xDEADBEEF) == 0xEFBEADDE");

    printf("\n=== bswap32: round-trip (двойной свап восстанавливает значение) ===\n");
    {
        uint32_t vals[] = {0x00000001u, 0x12345678u, 0xDEADBEEFu, 0xABCDEF01u};
        int ok = 1;
        for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
            if (bswap32(bswap32(vals[i])) != vals[i]) { ok = 0; break; }
        }
        CHECK(ok, "bswap32(bswap32(x)) == x для нескольких значений");
    }

    printf("\n=== cpu_to_be32 / be32_to_cpu ===\n");

    /* Round-trip независимо от endianness платформы */
    {
        uint32_t vals[] = {0x00000000u, 0x00000001u, 0x12345678u, 0xFFFFFFFFu};
        int ok = 1;
        for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
            if (cpu_to_be32(be32_to_cpu(vals[i])) != vals[i]) { ok = 0; break; }
        }
        CHECK(ok, "cpu_to_be32(be32_to_cpu(x)) == x (round-trip)");
    }
    {
        uint32_t vals[] = {0x00000000u, 0xABCDEF01u, 0x80000001u};
        int ok = 1;
        for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
            if (be32_to_cpu(cpu_to_be32(vals[i])) != vals[i]) { ok = 0; break; }
        }
        CHECK(ok, "be32_to_cpu(cpu_to_be32(x)) == x (round-trip обратный)");
    }

    /* На little-endian платформе (x86, arm64 linux) проверяем конкретные значения */
    if (le == 1) {
        CHECK(cpu_to_be32(0x01020304u) == 0x04030201u,
              "cpu_to_be32(0x01020304) == 0x04030201 на little-endian");
        CHECK(be32_to_cpu(0x04030201u) == 0x01020304u,
              "be32_to_cpu(0x04030201) == 0x01020304 на little-endian");
        CHECK(cpu_to_be32(0u) == 0u,
              "cpu_to_be32(0) == 0");
        CHECK(cpu_to_be32(1u) == 0x01000000u,
              "cpu_to_be32(1) == 0x01000000 на little-endian");
    } else {
        /* big-endian: конвертация — тождественное преобразование */
        CHECK(cpu_to_be32(0x01020304u) == 0x01020304u,
              "cpu_to_be32(x) == x на big-endian (нет свапа)");
        CHECK(be32_to_cpu(0x01020304u) == 0x01020304u,
              "be32_to_cpu(x) == x на big-endian (нет свапа)");
    }

    /*
     * Проверка отсутствия UB в bswap32.
     * Собери с -fsanitize=undefined и убедись что UBSan не выдаёт ошибок.
     * Если реализация использует *(uint32_t*)(uint8_t*) — UBSan поймает.
     * Эта проверка проходит в любом случае, но UBSan покажет проблему в логе.
     */
    {
        uint32_t x = 0x01020304u;
        uint32_t swapped = bswap32(x);
        /* Читаем результат через memcpy — без UB */
        uint8_t bytes[4];
        memcpy(bytes, &swapped, 4);
        CHECK(1, "bswap32 запущен с -fsanitize=undefined без ошибок UBSan");
        (void)bytes;
    }

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
