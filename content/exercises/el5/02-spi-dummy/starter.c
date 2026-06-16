#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ===================================================================
 * Симуляция SPI NOR flash (userspace, без /dev/spidevX.Y)
 * Цель: реализовать функции работы с flash через SPI команды.
 *
 * В реальном SPI драйвере использовался бы:
 *   spi_sync(spi, &msg) или spi_write_then_read(spi, tx, tx_len, rx, rx_len)
 * =================================================================== */

#define FLASH_SIZE 256u

/* Команды NOR flash (упрощённое подмножество W25Q серии) */
#define CMD_JEDEC_ID    0x9Fu
#define CMD_READ_STATUS 0x05u
#define CMD_WRITE_EN    0x06u
#define CMD_READ        0x03u
#define CMD_PAGE_PROG   0x02u
#define CMD_CHIP_ERASE  0x60u

/* --- Внутренний симулятор (НЕ ИЗМЕНЯТЬ) --- */
static uint8_t g_flash[FLASH_SIZE];
static uint8_t g_status = 0x00u;  /* bit[0]=WIP, bit[1]=WEL */

void sim_flash_init(void)
{
    memset(g_flash, 0xFF, sizeof(g_flash));  /* erased = 0xFF */
    g_status = 0x00u;
}

/* Симуляция полного SPI full-duplex transfer.
   tx: байты для отправки (первый = команда).
   rx: буфер для приёма (может быть NULL если ответ не нужен).
   len: количество байт.

   Реализует логику NOR flash:
   - 0x9F (JEDEC_ID): отвечает 3 байтами ID начиная с rx[1]
   - 0x05 (READ_STATUS): отвечает STATUS в rx[1]
   - 0x06 (WRITE_EN): устанавливает WEL бит
   - 0x03 (READ): читает данные из флэш начиная с addr=tx[1]
   - 0x02 (PAGE_PROG): пишет данные если WEL установлен
   - 0x60 (CHIP_ERASE): стирает всё если WEL установлен */
void sim_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!tx || len == 0u) return;
    if (rx) memset(rx, 0xFFu, len);

    switch (tx[0]) {
    case CMD_JEDEC_ID:
        if (rx && len >= 4u) {
            rx[1] = 0xEFu;  /* Winbond */
            rx[2] = 0x40u;  /* W25Q family */
            rx[3] = 0x14u;  /* 8Mbit */
        }
        break;
    case CMD_READ_STATUS:
        if (rx && len >= 2u) rx[1] = g_status;
        break;
    case CMD_WRITE_EN:
        g_status |= 0x02u;
        break;
    case CMD_READ:
        if (len >= 2u) {
            uint8_t addr = tx[1];
            for (size_t i = 2u; i < len; i++) {
                if (rx) rx[i] = g_flash[(addr + (uint8_t)(i - 2u)) % FLASH_SIZE];
            }
        }
        break;
    case CMD_PAGE_PROG:
        if ((g_status & 0x02u) && len >= 2u) {
            uint8_t addr = tx[1];
            for (size_t i = 2u; i < len; i++) {
                /* Flash: программирование = AND (только сбросить биты) */
                g_flash[(addr + (uint8_t)(i - 2u)) % FLASH_SIZE] &= tx[i];
            }
            g_status &= (uint8_t)~0x02u;  /* clear WEL */
        }
        break;
    case CMD_CHIP_ERASE:
        if (g_status & 0x02u) {
            memset(g_flash, 0xFF, sizeof(g_flash));
            g_status &= (uint8_t)~0x02u;
        }
        break;
    default:
        break;
    }
}

/* ===================================================================
 * РЕАЛИЗОВАТЬ НИЖЕ
 *
 * Каждая функция формирует SPI транзакцию через sim_spi_transfer.
 * Формат: первый байт = команда, остальные = аргументы или ответные данные.
 * =================================================================== */

/* Прочитать JEDEC ID.
   Транзакция: отправить 1 байт команды 0x9F, принять 3 байта ответа в id[0..2].
   id[0] = manufacturer, id[1] = memory type, id[2] = capacity.
   Вернуть 0. */
int flash_read_jedec(uint8_t id[3])
{
    (void)id;
    return -1; /* TODO */
}

/* Прочитать len байт из addr в buf.
   Транзакция: tx = [CMD_READ, addr], затем len байт для чтения.
   Итоговая длина transfer = 2 + len.
   Данные из flash оказываются в rx[2..2+len-1].
   Вернуть 0. */
int flash_read(uint8_t addr, uint8_t *buf, uint8_t len)
{
    (void)addr; (void)buf; (void)len;
    return -1; /* TODO */
}

/* Включить запись (Write Enable).
   Транзакция: отправить 1 байт команды CMD_WRITE_EN.
   После неё flash_read_status() должен вернуть значение с WEL бит[1] = 1.
   Вернуть 0. */
int flash_write_enable(void)
{
    return -1; /* TODO */
}

/* Записать len байт из buf по addr (Page Program).
   Транзакция: tx = [CMD_PAGE_PROG, addr, buf[0], buf[1], ...].
   Итоговая длина transfer = 2 + len.
   ВАЖНО: перед вызовом должен быть выполнен flash_write_enable().
   Вернуть 0. */
int flash_program(uint8_t addr, const uint8_t *buf, uint8_t len)
{
    (void)addr; (void)buf; (void)len;
    return -1; /* TODO */
}

/* Стереть весь чип.
   Сначала вызвать flash_write_enable(), затем отправить 1 байт CMD_CHIP_ERASE.
   После стирания все байты flash должны быть 0xFF.
   Вернуть 0. */
int flash_chip_erase(void)
{
    return -1; /* TODO */
}

/* Прочитать STATUS регистр.
   Транзакция: tx = [CMD_READ_STATUS, 0x00], rx[1] = STATUS.
   Вернуть значение STATUS байта. */
uint8_t flash_read_status(void)
{
    return 0xFFu; /* TODO */
}
