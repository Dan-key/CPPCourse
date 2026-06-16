#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ===================================================================
 * Симуляция I2C регистрового файла сенсора (userspace, без /dev/i2c-*)
 * Цель: реализовать функции работы с "устройством" через симулятор.
 *
 * В реальном драйвере вместо sim_read_reg() использовался бы:
 *   i2c_smbus_read_byte_data(client, reg)
 * =================================================================== */

/* --- Внутренний симулятор (НЕ ИЗМЕНЯТЬ) --- */
static uint8_t g_regs[256];
static int     g_initialized = 0;

void sim_device_init(void)
{
    memset(g_regs, 0, sizeof(g_regs));
    g_regs[0x00] = 0x42;   /* WHO_AM_I: должен быть 0x42 */
    g_regs[0x01] = 0x00;   /* CONFIG: bit[0] = enable */
    g_regs[0x02] = 0x18;   /* SCALE: bits[5:4] = 01 (0x18 = 0b00011000) */
    g_regs[0x10] = 0x03;   /* DATA_H: старший байт */
    g_regs[0x11] = 0xE8;   /* DATA_L: младший байт → 0x03E8 = 1000 */
    g_regs[0x20] = 0x00;   /* STATUS: bit[0] = data_ready */
    g_initialized = 1;
}

/* Симуляция i2c_smbus_read_byte_data.
   Возвращает значение регистра (0-255) или -1 при ошибке. */
int sim_read_reg(uint8_t reg)
{
    if (!g_initialized) return -1;
    return (int)g_regs[reg];
}

/* Симуляция i2c_smbus_write_byte_data.
   Возвращает 0 при успехе, -1 при ошибке. */
int sim_write_reg(uint8_t reg, uint8_t val)
{
    if (!g_initialized) return -1;
    g_regs[reg] = val;
    return 0;
}

/* Установить флаг data_ready (используется тестом) */
void sim_set_data_ready(void)
{
    if (g_initialized) g_regs[0x20] |= 0x01u;
}

/* ===================================================================
 * РЕАЛИЗОВАТЬ НИЖЕ
 * =================================================================== */

/* Прочитать WHO_AM_I (регистр 0x00).
   Вернуть 0 если значение == 0x42 (устройство найдено).
   Вернуть -1 если значение не 0x42 (неверный chip ID).
   Вернуть -1 если sim_read_reg вернул -1 (ошибка шины). */
int device_check_id(void)
{
    return -1; /* TODO */
}

/* Включить устройство: записать 1 в бит[0] регистра 0x01.
   Остальные биты регистра 0x01 должны остаться БЕЗ ИЗМЕНЕНИЙ (read-modify-write).
   Вернуть 0 при успехе, -1 при ошибке (sim_read_reg или sim_write_reg вернул -1). */
int device_enable(void)
{
    return -1; /* TODO */
}

/* Выключить устройство: сбросить бит[0] регистра 0x01.
   Остальные биты не трогать.
   Вернуть 0 при успехе, -1 при ошибке. */
int device_disable(void)
{
    return -1; /* TODO */
}

/* Прочитать 16-bit данные из регистров 0x10 (MSB) и 0x11 (LSB).
   Формат: big-endian, т.е. value = (reg[0x10] << 8) | reg[0x11]
   Если любой вызов sim_read_reg вернул -1 — вернуть 0.
   (В реальном коде использовали бы i2c_smbus_read_i2c_block_data или i2c_transfer) */
uint16_t device_read_data(void)
{
    return 0; /* TODO */
}

/* Установить масштаб: записать scale (значение 0-3) в биты[5:4] регистра 0x02.
   Биты[7:6] и биты[3:0] регистра 0x02 НЕ менять (read-modify-write).
   Если scale > 3 — вернуть -1 (некорректный аргумент).
   При ошибке чтения/записи — вернуть -1.
   При успехе — вернуть 0. */
int device_set_scale(uint8_t scale)
{
    return -1; /* TODO */
}

/* Проверить флаг data_ready в регистре 0x20.
   Вернуть 1 если бит[0] регистра 0x20 установлен.
   Вернуть 0 если бит[0] не установлен.
   Вернуть -1 если sim_read_reg вернул -1. */
int device_data_ready(void)
{
    return -1; /* TODO */
}
