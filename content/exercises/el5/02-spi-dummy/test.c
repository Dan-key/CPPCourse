#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Объявления из starter.c */
void    sim_flash_init(void);
void    sim_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len);
int     flash_read_jedec(uint8_t id[3]);
int     flash_read(uint8_t addr, uint8_t *buf, uint8_t len);
int     flash_write_enable(void);
int     flash_program(uint8_t addr, const uint8_t *buf, uint8_t len);
int     flash_chip_erase(void);
uint8_t flash_read_status(void);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void)
{
    sim_flash_init();

    /* --- flash_read_jedec --- */
    printf("=== JEDEC ID ===\n");
    uint8_t id[3] = {0};
    CHECK(flash_read_jedec(id) == 0,   "read_jedec: returns 0");
    CHECK(id[0] == 0xEFu,              "JEDEC: manufacturer = 0xEF (Winbond)");
    CHECK(id[1] == 0x40u,              "JEDEC: memory type = 0x40");
    CHECK(id[2] == 0x14u,              "JEDEC: capacity = 0x14");

    /* --- flash_read (erased state) --- */
    printf("=== Read (erased) ===\n");
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));
    CHECK(flash_read(0x00u, buf, 4) == 0,            "read: returns 0");
    CHECK(buf[0] == 0xFFu && buf[3] == 0xFFu,        "erased: reads 0xFF");

    /* --- flash_write_enable --- */
    printf("=== Write Enable ===\n");
    CHECK(flash_write_enable() == 0,                  "write_enable: returns 0");
    CHECK((flash_read_status() & 0x02u) != 0u,        "WEL bit set after write_enable");

    /* --- flash_program --- */
    printf("=== Program ===\n");
    uint8_t data[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
    flash_write_enable();
    CHECK(flash_program(0x10u, data, 4) == 0,         "program: returns 0");
    memset(buf, 0, sizeof(buf));
    flash_read(0x10u, buf, 4);
    CHECK(buf[0] == 0xDEu && buf[1] == 0xADu,         "programmed: bytes 0,1 correct");
    CHECK(buf[2] == 0xBEu && buf[3] == 0xEFu,         "programmed: bytes 2,3 correct");

    /* --- flash_chip_erase --- */
    printf("=== Chip Erase ===\n");
    CHECK(flash_chip_erase() == 0,                    "chip_erase: returns 0");
    memset(buf, 0, sizeof(buf));
    flash_read(0x10u, buf, 4);
    CHECK(buf[0] == 0xFFu && buf[3] == 0xFFu,         "after erase: reads 0xFF");

    /* --- Read-write-read round-trip --- */
    printf("=== Round-trip ===\n");
    flash_write_enable();
    uint8_t data2[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    flash_program(0x20u, data2, 4);
    memset(buf, 0, sizeof(buf));
    flash_read(0x20u, buf, 4);
    CHECK(buf[0] == 0x11u && buf[1] == 0x22u,         "round-trip: bytes 0,1");
    CHECK(buf[2] == 0x33u && buf[3] == 0x44u,         "round-trip: bytes 2,3");

    /* --- WEL cleared after program --- */
    printf("=== WEL management ===\n");
    CHECK((flash_read_status() & 0x02u) == 0u,         "WEL cleared after program");

    /* --- Program without write_enable should not work --- */
    flash_chip_erase();   /* uses write_enable internally */
    uint8_t data3[2] = {0xAAu, 0xBBu};
    flash_program(0x00u, data3, 2);   /* WEL not set → should be ignored */
    memset(buf, 0, sizeof(buf));
    flash_read(0x00u, buf, 2);
    CHECK(buf[0] == 0xFFu, "program without WEL: flash unchanged");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
