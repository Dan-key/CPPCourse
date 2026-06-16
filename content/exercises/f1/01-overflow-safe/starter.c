#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

/*
 * Вернуть true если переполнилось, false + *out = результат если нет.
 * Используй __builtin_add_overflow / __builtin_mul_overflow.
 */

bool add_safe_int(int a, int b, int *out) {
    /* TODO */
    (void)a; (void)b; (void)out;
    return false;
}

bool mul_safe_int(int a, int b, int *out) {
    /* TODO */
    (void)a; (void)b; (void)out;
    return false;
}

bool add_safe_size(size_t a, size_t b, size_t *out) {
    /* TODO */
    (void)a; (void)b; (void)out;
    return false;
}
