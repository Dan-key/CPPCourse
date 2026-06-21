#include <stdint.h>
#include <stddef.h>

/*
 * Упражнение 03 — переполнение и UB-арифметика.
 * Все три функции содержат по одному дефекту, который проявляется
 * на граничных входах. Найди их с помощью UBSan
 * (gcc -fsanitize=undefined -fno-sanitize-recover=all) и почини.
 */

/* BUG: аккумулятор int32 переполняется на больших суммах. */
int64_t array_sum(const int32_t *a, size_t n)
{
    int32_t s = 0;                 /* TODO: тип аккумулятора */
    for (size_t i = 0; i < n; i++)
        s += a[i];
    return s;
}

/* BUG: инициализация максимума нулём ломает массив из одних отрицательных. */
int32_t array_max(const int32_t *a, size_t n)
{
    int32_t m = 0;                 /* TODO: с чего начинать максимум? */
    for (size_t i = 0; i < n; i++)
        if (a[i] > m)
            m = a[i];
    return m;
}

/* BUG: (1u << k) - 1 при k == 32 — сдвиг на ширину типа, это UB. */
uint32_t low_bits_mask(unsigned k)
{
    return (1u << k) - 1u;         /* TODO: обработай k == 32 */
}
