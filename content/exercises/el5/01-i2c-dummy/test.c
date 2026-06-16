#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Объявления из starter.c */
void     sim_device_init(void);
void     sim_set_data_ready(void);
int      sim_read_reg(uint8_t reg);
int      sim_write_reg(uint8_t reg, uint8_t val);

int      device_check_id(void);
int      device_enable(void);
int      device_disable(void);
uint16_t device_read_data(void);
int      device_set_scale(uint8_t scale);
int      device_data_ready(void);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void)
{
    sim_device_init();

    /* --- device_check_id --- */
    printf("=== device_check_id ===\n");
    CHECK(device_check_id() == 0, "check_id: WHO_AM_I=0x42 → 0");

    sim_write_reg(0x00, 0xAB);  /* испортить WHO_AM_I */
    CHECK(device_check_id() == -1, "check_id: WHO_AM_I=0xAB → -1");
    sim_write_reg(0x00, 0x42);  /* восстановить */

    /* --- device_enable --- */
    printf("=== device_enable ===\n");
    sim_device_init();
    CHECK(device_enable() == 0, "enable: returns 0");
    CHECK((sim_read_reg(0x01) & 0x01) == 1, "enable: bit[0] of reg 0x01 set");
    CHECK((sim_read_reg(0x01) & 0xFE) == 0, "enable: other bits unchanged (were 0)");

    /* --- device_disable --- */
    printf("=== device_disable ===\n");
    sim_write_reg(0x01, 0x07u);  /* set bits [2:0] */
    CHECK(device_disable() == 0, "disable: returns 0");
    CHECK((sim_read_reg(0x01) & 0x01) == 0, "disable: bit[0] cleared");
    CHECK((sim_read_reg(0x01) & 0x06) == 6, "disable: bits[2:1] preserved");

    /* --- device_read_data --- */
    printf("=== device_read_data ===\n");
    sim_device_init();  /* reset: reg[0x10]=0x03, reg[0x11]=0xE8 */
    CHECK(device_read_data() == 0x03E8u, "read_data: 0x03E8 == 1000");

    sim_write_reg(0x10, 0xFF);
    sim_write_reg(0x11, 0xFF);
    CHECK(device_read_data() == 0xFFFFu, "read_data: 0xFFFF");

    /* --- device_set_scale --- */
    printf("=== device_set_scale ===\n");
    sim_device_init();  /* reg[0x02] = 0x18 = 0b00011000 */

    CHECK(device_set_scale(0) == 0, "set_scale(0): returns 0");
    CHECK((sim_read_reg(0x02) & 0x30) == 0x00, "set_scale(0): bits[5:4] = 00");
    CHECK((sim_read_reg(0x02) & 0xCFu) == 0x08u, "set_scale(0): bits[7:6] and [3:0] unchanged");

    CHECK(device_set_scale(3) == 0, "set_scale(3): returns 0");
    CHECK((sim_read_reg(0x02) & 0x30) == 0x30, "set_scale(3): bits[5:4] = 11");

    CHECK(device_set_scale(2) == 0, "set_scale(2): returns 0");
    CHECK((sim_read_reg(0x02) & 0x30) == 0x20, "set_scale(2): bits[5:4] = 10");

    CHECK(device_set_scale(4) == -1, "set_scale(4): invalid → -1");

    /* --- device_data_ready --- */
    printf("=== device_data_ready ===\n");
    sim_device_init();
    CHECK(device_data_ready() == 0, "data_ready: initially 0");
    sim_set_data_ready();
    CHECK(device_data_ready() == 1, "data_ready: after sim_set → 1");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
