/*
 * Упражнение EL1-01: Определение endianness без UB
 *
 * Тема: byte order, type punning, memcpy vs pointer cast.
 *
 * На x86/x86_64 и ARM (в обычном режиме) — little-endian:
 *   uint32_t x = 0x01020304;
 *   В памяти по адресу &x: [04] [03] [02] [01]  ← младший байт первый
 *
 * Сетевые протоколы и большинство регистров SoC — big-endian (network byte order):
 *   uint32_t x = 0x01020304;
 *   В памяти: [01] [02] [03] [04]  ← старший байт первый
 *
 * Функции htonl/ntohl делают то же самое что cpu_to_be32/be32_to_cpu,
 * но через BSD sockets API. Реализуем собственные для понимания.
 *
 * ВАЖНО: нельзя определять endianness через *(uint8_t*)&x — это нарушает
 * strict aliasing (§6.5p7, UB). Нужно использовать memcpy или union.
 *
 * Компиляция (тест):
 *   gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
 *       -fsanitize=address,undefined -O1 -g starter.c test.c -o prog && ./prog
 */
#include <stdint.h>
#include <string.h>   /* memcpy */

/* -----------------------------------------------------------------------
 * is_little_endian()
 *
 * Определяет byte order текущей платформы.
 * Возвращает 1 если little-endian (x86, ARM в normal mode).
 * Возвращает 0 если big-endian (некоторые MIPS, SPARC, PowerPC).
 *
 * Реализуй через union или memcpy — НЕ через указатель-каст:
 *   uint32_t x = 1; return *(uint8_t*)&x;  // UB! strict aliasing violation
 *
 * Правильно через union (§6.5.2.3 C11, footnote 95 — в C это defined behavior):
 *   union { uint32_t u; uint8_t b[4]; } pun;
 *   pun.u = 1;
 *   return pun.b[0];   // little-endian: младший байт первый
 *
 * Правильно через memcpy:
 *   uint32_t v = 1;
 *   uint8_t b[4];
 *   memcpy(b, &v, 4);
 *   return b[0];
 * ----------------------------------------------------------------------- */
int is_little_endian(void) {
    return -1; /* TODO */
}

/* -----------------------------------------------------------------------
 * bswap32(x)
 *
 * Меняет порядок байт в 32-битном числе.
 * Пример: 0x01020304 → 0x04030201
 *         байты [01][02][03][04] → [04][03][02][01]
 *
 * НЕ используй *(uint32_t*)(uint8_t_ptr) — UB.
 * Можно: побайтовые операции (>> << |), или memcpy с uint8_t массивом.
 *
 * Подсказка (побайтовые операции):
 *   ((x & 0xFF000000u) >> 24) | ...
 *
 * Или через GCC built-in (быстро, но не стандарт):
 *   return __builtin_bswap32(x);
 * ----------------------------------------------------------------------- */
uint32_t bswap32(uint32_t x) {
    return 0; /* TODO */
}

/* -----------------------------------------------------------------------
 * cpu_to_be32(x)
 *
 * Конвертирует значение из CPU byte order в big-endian (network order).
 *
 * На little-endian системе (x86, ARM) = bswap32(x).
 * На big-endian системе = x (ничего делать не нужно, уже в правильном порядке).
 *
 * Используется при записи числа в сетевой пакет, флеш-заголовок,
 * или в регистр SoC с big-endian доступом.
 * ----------------------------------------------------------------------- */
uint32_t cpu_to_be32(uint32_t x) {
    return 0; /* TODO */
}

/* -----------------------------------------------------------------------
 * be32_to_cpu(x)
 *
 * Конвертирует значение из big-endian в CPU byte order.
 * Обратная операция к cpu_to_be32.
 *
 * Используется при чтении числа из сетевого пакета или флеш-заголовка.
 * ----------------------------------------------------------------------- */
uint32_t be32_to_cpu(uint32_t x) {
    return 0; /* TODO */
}
