/*
 * Упражнение EL1-03: ABI структур — sizeof, offsetof, wire format
 *
 * Темы: padding компилятора, __attribute__((packed)), binary compatibility,
 *       сериализация без UB для wire format (протоколы, флеш-заголовки).
 *
 * Проблема: структура C содержит padding для выравнивания членов.
 * Если записать struct proto_header в файл или по сети «как есть»,
 * на другой платформе (или другом компиляторе) padding может быть другим.
 *
 * Правильный подход для wire format:
 *   - Явная сериализация: записывать каждое поле по одному через memcpy
 *   - Явная конвертация byte order (big-endian для сетевых протоколов)
 *   - НЕ fwrite(&header, sizeof header, 1, f) — padding зависит от компилятора/ABI
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
 *       -fsanitize=address,undefined -O1 -g starter.c test.c -o prog && ./prog
 *
 * Для кросс-компиляции (проверить что sizeof совпадают на ARM):
 *   aarch64-linux-gnu-gcc -std=c17 -Wall -O1 starter.c test.c -o prog_arm
 *   qemu-aarch64-static -L /usr/aarch64-linux-gnu ./prog_arm
 */

#include <stddef.h>   /* offsetof */
#include <stdint.h>
#include <string.h>   /* memcpy */

/* -----------------------------------------------------------------------
 * Структура A — «наивная» (с padding).
 *
 * Компилятор вставит padding перед uint32_t length (требует выравнивания по 4),
 * и, возможно, после последнего поля для выравнивания всей структуры.
 *
 * Ожидаемый layout на AArch64/x86_64 с GCC (стандартный ABI):
 *   offset 0: magic    (1 байт)
 *   offset 1-3: padding (3 байта — чтобы length был на offset 4)
 *   offset 4: length   (4 байта)
 *   offset 8: version  (1 байт)
 *   offset 9: padding  (1 байт — чтобы checksum был на offset 10)
 *   offset 10: checksum (2 байта)
 *   offset 12: type    (1 байт)
 *   offset 13-15: trailing padding (до кратного 4)
 *   sizeof == 16
 * ----------------------------------------------------------------------- */
struct proto_header {
    uint8_t  magic;      /* 0xAB */
    uint32_t length;     /* длина payload (big-endian в wire format) */
    uint8_t  version;    /* версия протокола */
    uint16_t checksum;   /* контрольная сумма (big-endian в wire format) */
    uint8_t  type;       /* тип пакета */
};

/* -----------------------------------------------------------------------
 * Структура B — упакованная (__packed) для wire format.
 *
 * __attribute__((packed)) убирает весь padding.
 * sizeof == 1 + 4 + 1 + 2 + 1 = 9 байт.
 *
 * ВНИМАНИЕ: packed структуры могут вызывать unaligned access на строгих
 * платформах (ARMv6, некоторые MIPS). На ARMv7+ и AArch64 — аппаратная
 * поддержка unaligned access, но медленнее.
 *
 * Для сериализации в wire format лучше: явный memcpy в uint8_t буфер.
 * Packed полезна когда нужно описать заголовок для overlay на буфер.
 * ----------------------------------------------------------------------- */
struct __attribute__((packed)) proto_header_packed {
    uint8_t  magic;
    uint32_t length;
    uint8_t  version;
    uint16_t checksum;
    uint8_t  type;
};

/* -----------------------------------------------------------------------
 * Вспомогательные функции byte order (без зависимости от arpa/inet.h).
 * В реальном коде используй htonl/ntohl или свои cpu_to_be32/be32_to_cpu.
 * ----------------------------------------------------------------------- */
static inline uint32_t u32_to_be(uint32_t x)
{
    uint8_t b[4] = {
        (uint8_t)(x >> 24),
        (uint8_t)(x >> 16),
        (uint8_t)(x >> 8),
        (uint8_t)(x)
    };
    uint32_t r;
    memcpy(&r, b, 4);
    return r;
}

static inline uint16_t u16_to_be(uint16_t x)
{
    uint8_t b[2] = {
        (uint8_t)(x >> 8),
        (uint8_t)(x)
    };
    uint16_t r;
    memcpy(&r, b, 2);
    return r;
}

/* -----------------------------------------------------------------------
 * header_size() — вернуть sizeof(struct proto_header)
 *
 * Ожидается: > 9 (есть padding).
 * ----------------------------------------------------------------------- */
size_t header_size(void)
{
    return sizeof(struct proto_header);
}

/* -----------------------------------------------------------------------
 * header_packed_size() — вернуть sizeof(struct proto_header_packed)
 *
 * Ожидается: == 9 (1 + 4 + 1 + 2 + 1, без padding).
 * ----------------------------------------------------------------------- */
size_t header_packed_size(void)
{
    return sizeof(struct proto_header_packed);
}

/* -----------------------------------------------------------------------
 * length_offset() — вернуть offsetof(struct proto_header, length)
 *
 * Ожидается: > 1 (есть padding после magic).
 * ----------------------------------------------------------------------- */
size_t length_offset(void)
{
    return offsetof(struct proto_header, length);
}

/* -----------------------------------------------------------------------
 * length_offset_packed() — вернуть offsetof(struct proto_header_packed, length)
 *
 * Ожидается: == 1 (immediately after magic, no padding).
 * ----------------------------------------------------------------------- */
size_t length_offset_packed(void)
{
    return offsetof(struct proto_header_packed, length);
}

/* -----------------------------------------------------------------------
 * serialize_header()
 *
 * Сериализовать struct proto_header в wire format:
 *   - 9 байт без padding
 *   - length и checksum — в big-endian
 *   - остальные поля (однобайтные) — без изменений
 *
 * Wire layout:
 *   buf[0]     = magic
 *   buf[1..4]  = length в big-endian (старший байт первый)
 *   buf[5]     = version
 *   buf[6..7]  = checksum в big-endian
 *   buf[8]     = type
 *
 * buf должен быть не менее 9 байт.
 * Возвращает количество записанных байт (всегда 9).
 *
 * Использовать memcpy + u32_to_be / u16_to_be для корректной записи
 * без UB и без зависимости от endianness хоста.
 *
 * НЕ делать: memcpy(buf, h, sizeof *h) — запишет padding!
 * ----------------------------------------------------------------------- */
int serialize_header(const struct proto_header *h, uint8_t *buf)
{
    (void)h;
    (void)buf;
    return 0; /* TODO: записать 9 байт в правильном порядке */
}

/* -----------------------------------------------------------------------
 * deserialize_header()
 *
 * Обратная операция: прочитать 9-байтный wire format в struct proto_header.
 * Возвращает 0 при успехе.
 * ----------------------------------------------------------------------- */
int deserialize_header(const uint8_t *buf, struct proto_header *h)
{
    (void)buf;
    (void)h;
    return -1; /* TODO */
}
