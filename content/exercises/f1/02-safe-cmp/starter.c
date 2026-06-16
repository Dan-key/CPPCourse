#include <stdbool.h>
#include <stddef.h>

/*
 * Безопасное сравнение a < b без UB и без -Wsign-compare.
 * Подсказка: проверь знак a отдельно — если a < 0, ответ сразу известен.
 */

bool safe_less_si(int a, size_t b) {
    /* TODO */
    (void)a; (void)b;
    return false;
}

bool safe_less_iu(int a, unsigned b) {
    /* TODO */
    (void)a; (void)b;
    return false;
}
