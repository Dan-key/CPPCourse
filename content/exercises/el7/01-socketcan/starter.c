#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ===================================================================
 * CAN фрейм утилиты (userspace, без реального CAN оборудования)
 *
 * Упрощённые определения без linux/can.h заголовков.
 * В реальном коде: #include <linux/can.h> #include <linux/can/raw.h>
 * =================================================================== */

#define CAN_MAX_DLC      8u
#define CAN_SFF_MASK     0x000007FFu    /* 11-бит Standard Frame Format */
#define CAN_EFF_MASK     0x1FFFFFFFu    /* 29-бит Extended Frame Format */
#define CAN_EFF_FLAG     0x80000000u    /* расширенный фрейм (29-bit ID) */
#define CAN_RTR_FLAG     0x40000000u    /* Remote Transmission Request */
#define CAN_ERR_FLAG     0x20000000u    /* error frame */

typedef struct {
    uint32_t can_id;                    /* 11/29-bit ID + flags */
    uint8_t  can_dlc;                   /* data length code (0-8) */
    uint8_t  __pad[3];
    uint8_t  data[CAN_MAX_DLC];
} can_frame_t;

/* ===================================================================
 * РЕАЛИЗОВАТЬ НИЖЕ
 * =================================================================== */

/* Создать стандартный CAN фрейм (11-bit ID, Standard Frame Format).
   - can_id = id & CAN_SFF_MASK (без установки CAN_EFF_FLAG)
   - can_dlc = dlc (не проверяем — тест проверит валидность отдельно)
   - data копируется из data если data != NULL и dlc > 0
   - Если data == NULL или dlc == 0 — data остаётся нулевым */
can_frame_t make_frame(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    can_frame_t f;
    memset(&f, 0, sizeof(f));
    return f; /* TODO */
}

/* Создать расширенный CAN фрейм (29-bit ID, Extended Frame Format).
   - can_id = (id & CAN_EFF_MASK) | CAN_EFF_FLAG */
can_frame_t make_frame_ext(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    can_frame_t f;
    memset(&f, 0, sizeof(f));
    return f; /* TODO */
}

/* Проверить валидность фрейма.
   Вернуть 1 если фрейм валиден, 0 если нет.
   Условия невалидности:
   - can_dlc > 8
   - CAN_ERR_FLAG установлен
   - CAN_EFF_FLAG НЕ установлен, но ID > CAN_SFF_MASK (11-bit переполнение) */
int frame_valid(const can_frame_t *f)
{
    return 0; /* TODO */
}

/* Сериализовать фрейм в строку формата candump: "123#DEADBEEF"
   Формат:
   - SFF (11-bit): "%03X#%s" где %03X = ID, %s = hex data в верхнем регистре
   - EFF (29-bit): "%08X#%s" где %08X = ID без флагов (ID & CAN_EFF_MASK)
   - Пустой фрейм (dlc=0): "123#" (только ID и символ #)

   Записать строку в buf (размером bufsz), завершить '\0'.
   Вернуть длину строки (без '\0') при успехе.
   Вернуть -1 если bufsz слишком мал. */
int frame_to_candump(const can_frame_t *f, char *buf, size_t bufsz)
{
    (void)f; (void)buf; (void)bufsz;
    return -1; /* TODO */
}

/* Разобрать строку формата "123#DEADBEEF" в фрейм.
   Правила:
   - Всё до '#' = HEX ID
   - ID > 0x7FF → установить CAN_EFF_FLAG
   - После '#' = HEX данные (кратно 2 символам, макс 8 байт = 16 hex символов)
   - Пустые данные ("123#") → dlc = 0

   Вернуть 0 при успехе.
   Вернуть -1 при ошибке формата (нет '#', некорректный hex, dlc > 8). */
int candump_to_frame(const char *s, can_frame_t *f)
{
    (void)s; (void)f;
    return -1; /* TODO */
}

/* Проверить соответствие фрейма SocketCAN фильтру.
   Условие: (f->can_id & mask) == (filter_id & mask)
   Вернуть 1 если совпадает, 0 если нет. */
int frame_matches_filter(const can_frame_t *f, uint32_t filter_id, uint32_t mask)
{
    (void)f; (void)filter_id; (void)mask;
    return 0; /* TODO */
}
