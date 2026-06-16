#define _POSIX_C_SOURCE 200809L
#include <stddef.h>

/*
 * УПРАЖНЕНИЕ: программа компилируется без ошибок и без ASan-ошибок,
 * но даёт неправильные результаты.
 *
 * Найди баги с помощью GDB:
 *   gcc -std=c17 -g -O0 starter.c test.c -o prog
 *   gdb ./prog
 *   (gdb) break binary_search
 *   (gdb) run
 *   (gdb) print lo
 *   (gdb) print hi
 *   (gdb) print mid    <- смотри переменные
 *   (gdb) next         <- шагай по строкам
 *
 * В этом файле 3 бага. Ни один из них не вызывает краш или ASan-ошибку —
 * программа просто возвращает неправильные значения.
 */

/* Бинарный поиск значения val в отсортированном массиве arr[0..n-1].
   Вернуть индекс найденного элемента или -1 если не найден.
   Подсказка GDB: поставь breakpoint на строку return, напечатай mid. */
int binary_search(const int *arr, int n, int val)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (arr[mid] == val) return mid + 1; /* БАГ: должен вернуть mid, не mid+1 */
        if (arr[mid] < val) lo = mid + 1;
        else                hi = mid - 1;
    }
    return -1;
}

/* Вычислить n-й элемент последовательности Фибоначчи (0-индексация):
   fib(0)=0, fib(1)=1, fib(2)=1, fib(3)=2, fib(4)=3, fib(5)=5, fib(6)=8, ...
   Вернуть -1 если n < 0. */
int fib(int n)
{
    if (n < 0)  return -1;
    if (n == 0) return 1;              /* БАГ: должен вернуть 0 */
    if (n == 1) return 1;
    return fib(n - 1) + fib(n - 2);
}

/* Найти индекс первого вхождения символа c в строке s длиной len.
   Вернуть -1 если не найден. Не использует strchr. */
int str_find_char(const char *s, size_t len, char c)
{
    for (size_t i = 1; i < len; i++)   /* БАГ: i должен начинаться с 0 */
        if (s[i] == c) return (int)i;
    return -1;
}
