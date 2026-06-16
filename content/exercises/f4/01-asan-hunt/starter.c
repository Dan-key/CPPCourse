#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>

/*
 * УПРАЖНЕНИЕ: найди и исправь баги с помощью ASan и GDB.
 *
 * Рабочий процесс:
 *   gcc -std=c17 -fsanitize=address,undefined -g starter.c test.c -o prog
 *   ./prog          <- ASan сообщит тип и строку ошибки
 *   gdb ./prog      <- run -> bt -> посмотри переменные
 *
 * В этом файле 3 бага. Найди и исправь каждый.
 */

/* Вернуть копию строки s в верхнем регистре.
   Вызывающая сторона освобождает память. */
char *str_upper(const char *s)
{
    size_t len = strlen(s);
    char *buf = malloc(len);            /* БАГ: не хватает +1 для '\0' */
    for (size_t i = 0; i < len; i++)
        buf[i] = (char)toupper((unsigned char)s[i]);
    buf[len] = '\0';                    /* heap overflow здесь */
    return buf;
}

/* Вернуть длину самого длинного слова в строке s (слова разделены пробелами). */
int longest_word(const char *s)
{
    char *copy = strdup(s);
    int max_len = 0;
    char *token = strtok(copy, " ");
    while (token != NULL) {
        int len = (int)strlen(token);
        if (len > max_len) max_len = len;
        token = strtok(NULL, " ");
    }
    free(copy);
    return max_len;
}

/* Заполнить массив out[0..n-1] квадратами чисел 0..n-1.
   Возвращает сумму квадратов. */
long fill_squares(int *out, int n)
{
    long sum = 0;
    for (int i = 0; i <= n; i++) {     /* БАГ: i <= n пишет в out[n] */
        out[i] = i * i;
        sum += i * i;
    }
    return sum;
}

/* Вернуть указатель на первый встреченный максимальный элемент в arr[0..n-1].
   Вернуть NULL если n == 0. */
const int *find_max_ptr(const int *arr, int n)
{
    if (n <= 0) return NULL;
    int *tmp = malloc((size_t)n * sizeof(int));
    memcpy(tmp, arr, (size_t)n * sizeof(int));
    int max_idx = 0;
    for (int i = 1; i < n; i++)
        if (tmp[i] > tmp[max_idx]) max_idx = i;
    /* БАГ: use-after-free — сохраняем адрес элемента, затем освобождаем блок */
    uintptr_t addr = (uintptr_t)&tmp[max_idx];
    free(tmp);
    return (const int *)addr;
}
