/*
 * Упражнение F1-07: Safe Strings
 *
 * Темы: безопасные строковые операции, strtol с полной проверкой ошибок,
 *       null-terminator гарантии, off-by-one, parse_int корректный.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -Wconversion -fsanitize=address,undefined \
 *       -O1 -g starter.c test.c -o prog
 */
#define _GNU_SOURCE
#include <stddef.h>
#include <string.h>   /* strlen, memcpy */
#include <stdlib.h>   /* malloc, strtol */
#include <errno.h>    /* errno, ERANGE */
#include <limits.h>   /* LONG_MIN, LONG_MAX, INT_MIN, INT_MAX */

/*
 * safe_strcpy — безопасное копирование строки.
 *
 * Копирует src в dst, не более dst_size байт включая нулевой терминатор.
 * dst ВСЕГДА заканчивается '\0' (если dst_size > 0).
 *
 * Возвращает:
 *   0  — строка скопирована целиком (src влезла в dst_size)
 *  -1  — строка усечена (src длиннее dst_size-1)
 *
 * Контракт: dst != NULL, dst_size > 0, src != NULL.
 */
int safe_strcpy(char *dst, size_t dst_size, const char *src) {
    return 0; /* TODO */
}

/*
 * safe_strcat — безопасная конкатенация строки к буферу.
 *
 * Дописывает src к существующей строке в dst.
 * dst_size — полный размер буфера (включая уже записанное и нулевой терминатор).
 * dst ВСЕГДА заканчивается '\0'.
 *
 * Возвращает:
 *   0  — src дописан целиком
 *  -1  — усечено (не хватило места)
 *
 * Контракт: dst гарантированно null-terminated на входе, dst_size > 0.
 */
int safe_strcat(char *dst, size_t dst_size, const char *src) {
    return 0; /* TODO */
}

/*
 * parse_int — разобрать целое из строки с полной проверкой ошибок.
 *
 * Использует strtol. Возвращает 0 при успехе, -1 при любой ошибке:
 *   - пустая строка
 *   - нечисловые символы (включая trailing garbage: "42abc" → ошибка)
 *   - переполнение (ERANGE от strtol, или вне диапазона int)
 *   - значение вне диапазона [INT_MIN, INT_MAX]
 *
 * Канонический паттерн strtol:
 *   char *end;
 *   errno = 0;
 *   long val = strtol(s, &end, 10);
 *   if (errno == ERANGE || end == s || *end != '\0') → ошибка
 *   if (val < INT_MIN || val > INT_MAX)               → ошибка
 *   *out = (int)val; return 0;
 */
int parse_int(const char *s, int *out) {
    return -1; /* TODO */
}

/*
 * safe_strdup — дублирует строку, возвращает NULL при ошибке malloc.
 *
 * В отличие от POSIX strdup, явно обрабатывает NULL (возвращает NULL).
 * Возвращённый буфер должен быть освобождён вызывающим через free().
 */
char *safe_strdup(const char *s) {
    return NULL; /* TODO */
}
