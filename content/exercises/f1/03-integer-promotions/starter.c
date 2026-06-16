/*
 * Упражнение F1-03: Integer Promotions
 *
 * Темы: integer promotion (§6.3.1.1), usual arithmetic conversions (§6.3.1.8),
 *       signed/unsigned comparisons, safe_lt без -Wsign-compare.
 *
 * Компиляция (проверь сам перед сдачей):
 *   gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
 *       -fsanitize=address,undefined -O1 -g starter.c test.c -o prog
 */
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>   /* INT_MAX, INT_MIN */

/*
 * Верни 1 если вычисление (a + b) через int переполнится.
 *
 * Подсказка: a и b — uint8_t. До арифметики они продвигаются до int
 * (§6.3.1.1). Максимум: int(255) + int(255) = 510 — прекрасно влезает
 * в int на любой современной платформе (int минимум 16 бит по стандарту,
 * а INT_MAX >= 32767). Переполнения НЕТ, функция должна всегда вернуть 0.
 *
 * Напиши объясняющий комментарий, верни 0.
 */
int uint8_sum_overflows(uint8_t a, uint8_t b) {
    /* TODO: объясни в комментарии, почему переполнение невозможно, верни 0 */
    (void)a; (void)b;
    return -1; /* заглушка */
}

/*
 * Верни значение (uint8_t)(~x) корректно.
 *
 * Ловушка: унарный ~ продвигает операнд до int (§6.3.1.1).
 * ~(uint8_t)0  =>  ~(int)0  =>  (int)(-1), а не 255.
 * Нужно явно замаскировать результат: (uint8_t)(~x & 0xFFu),
 * или просто вернуть (uint8_t)(~x) — усечение при касте отрежет лишние биты.
 */
uint8_t bitwise_not_u8(uint8_t x) {
    return 0; /* заглушка */
}

/*
 * Верни 1 если (-1 < (unsigned)0).
 *
 * Правильный ответ: 0 (ложь).
 * При usual arithmetic conversions (§6.3.1.8) int приводится к unsigned:
 * (int)(-1) → (unsigned)UINT_MAX = 4294967295 на 32-бит.
 * UINT_MAX > 0, поэтому сравнение «-1 < 0u» ложно.
 *
 * Эта функция просто возвращает результат выражения — не меняй логику,
 * только убери заглушку.
 */
int signed_less_than_unsigned(void) {
    int i = -1;
    unsigned u = 0;
    /*
     * TODO: убери заглушку и верни `i < u` напрямую (без каста).
     * Что произойдёт? При usual arithmetic conversions (§6.3.1.8)
     * int приводится к unsigned: (unsigned)(-1) = UINT_MAX > 0 → ложь (0).
     */
    (void)i; (void)u;
    return -1; /* заглушка */
}

/*
 * Безопасное сравнение: верни 1 если математическое значение a < b,
 * без UB и без предупреждений -Wsign-compare/-Wsign-conversion.
 *
 * Алгоритм:
 *   - если a < 0: a заведомо меньше любого беззнакового → вернуть 1;
 *   - иначе a >= 0: безопасно кастуем a к unsigned и сравниваем.
 *
 * Пример: safe_lt(-1, 0) == 1  (−1 меньше 0 как целые)
 *         safe_lt(5,  5) == 0
 *         safe_lt(5,  6) == 1
 */
int safe_lt(int a, unsigned b) {
    return 0; /* TODO */
}
