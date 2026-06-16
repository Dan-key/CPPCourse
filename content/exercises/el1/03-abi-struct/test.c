/*
 * Тест-харнес для EL1-03: ABI структур.
 * Не изменяй этот файл.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
 *       -fsanitize=address,undefined -O1 -g starter.c test.c -o prog && ./prog
 *
 * На ARM (кросс):
 *   aarch64-linux-gnu-gcc -std=c17 -Wall -O1 starter.c test.c -o prog_arm
 *   qemu-aarch64-static -L /usr/aarch64-linux-gnu ./prog_arm
 */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Объявления из starter.c */
size_t header_size(void);
size_t header_packed_size(void);
size_t length_offset(void);
size_t length_offset_packed(void);
int    serialize_header(const void *h, uint8_t *buf);
int    deserialize_header(const uint8_t *buf, void *h);

/*
 * Переопределения для тестов (дублируем структуру чтобы не зависеть
 * от внутреннего определения в starter.c).
 */
struct test_header {
    uint8_t  magic;
    uint32_t length;
    uint8_t  version;
    uint16_t checksum;
    uint8_t  type;
};

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s (got: %s)\n", msg, #cond); } \
} while(0)

#define CHECK_EQ(a, b, msg) do { \
    g_run++; \
    if ((a) == (b)) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else { printf("  [FAIL] %s: expected %zu, got %zu\n", msg, (size_t)(b), (size_t)(a)); } \
} while(0)

int main(void)
{
    printf("=== sizeof: padding ===\n");

    size_t sz       = header_size();
    size_t sz_pack  = header_packed_size();

    CHECK(sz > sz_pack,
          "sizeof(proto_header) > sizeof(proto_header_packed) — padding существует");
    CHECK_EQ(sz_pack, 9u,
             "sizeof(proto_header_packed) == 9 (1+4+1+2+1, без padding)");

    printf("  INFO: sizeof(proto_header)        = %zu\n", sz);
    printf("  INFO: sizeof(proto_header_packed) = %zu\n", sz_pack);

    printf("\n=== offsetof: позиции полей ===\n");

    size_t off       = length_offset();
    size_t off_pack  = length_offset_packed();

    CHECK(off > 1u,
          "offsetof(proto_header, length) > 1 — есть padding после magic");
    CHECK_EQ(off_pack, 1u,
             "offsetof(proto_header_packed, length) == 1 — сразу после magic");

    printf("  INFO: offsetof(proto_header, length)        = %zu\n", off);
    printf("  INFO: offsetof(proto_header_packed, length) = %zu\n", off_pack);

    printf("\n=== serialize_header: wire format ===\n");
    {
        struct test_header h;
        h.magic    = 0xABu;
        h.length   = 0x00000100u;  /* 256 в host byte order */
        h.version  = 0x01u;
        h.checksum = 0x1234u;
        h.type     = 0x05u;

        uint8_t buf[16];
        memset(buf, 0xFF, sizeof buf);  /* мусор чтобы обнаружить запись за пределы */

        int ret = serialize_header(&h, buf);
        CHECK(ret == 9, "serialize_header возвращает 9 (количество байт)");

        /* Проверка byte-by-byte */
        CHECK(buf[0] == 0xABu,
              "buf[0] == 0xAB (magic)");
        /* length = 0x00000100 в big-endian: [00][00][01][00] */
        CHECK(buf[1] == 0x00u && buf[2] == 0x00u && buf[3] == 0x01u && buf[4] == 0x00u,
              "buf[1..4] = 0x00000100 в big-endian");
        CHECK(buf[5] == 0x01u,
              "buf[5] == 0x01 (version)");
        /* checksum = 0x1234 в big-endian: [12][34] */
        CHECK(buf[6] == 0x12u && buf[7] == 0x34u,
              "buf[6..7] = 0x1234 в big-endian");
        CHECK(buf[8] == 0x05u,
              "buf[8] == 0x05 (type)");

        /* За пределами 9 байт — не трогаем */
        CHECK(buf[9] == 0xFFu,
              "buf[9] не изменён (serialize пишет ровно 9 байт)");
    }

    printf("\n=== round-trip: serialize → deserialize ===\n");
    {
        struct test_header orig, restored;
        orig.magic    = 0xABu;
        orig.length   = 0x12345678u;
        orig.version  = 0x02u;
        orig.checksum = 0xBEEFu;
        orig.type     = 0x07u;

        uint8_t buf[9];
        int sret = serialize_header(&orig, buf);
        int dret = deserialize_header(buf, &restored);

        CHECK(sret == 9 && dret == 0,
              "serialize и deserialize оба успешны");
        CHECK(restored.magic    == orig.magic,    "round-trip: magic совпадает");
        CHECK(restored.length   == orig.length,   "round-trip: length совпадает");
        CHECK(restored.version  == orig.version,  "round-trip: version совпадает");
        CHECK(restored.checksum == orig.checksum, "round-trip: checksum совпадает");
        CHECK(restored.type     == orig.type,     "round-trip: type совпадает");
    }

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
