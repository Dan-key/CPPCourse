/*
 * Тест-харнес для F1-07: Safe Strings.
 * Не изменяй этот файл.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -Wconversion -fsanitize=address,undefined \
 *       -O1 -g starter.c test.c -o prog && ./prog
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* Объявления из решения студента */
int   safe_strcpy(char *dst, size_t dst_size, const char *src);
int   safe_strcat(char *dst, size_t dst_size, const char *src);
int   parse_int(const char *s, int *out);
char *safe_strdup(const char *s);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    printf("=== safe_strcpy ===\n");
    {
        char buf[8];

        /* Нормальное копирование — влезает */
        memset(buf, 0xAA, sizeof buf);
        CHECK(safe_strcpy(buf, sizeof buf, "hello") == 0,
              "strcpy 'hello' в buf[8] → 0 (влезает)");
        CHECK(strcmp(buf, "hello") == 0,
              "strcpy: buf содержит 'hello'");

        /* Точно в размер: src = 7 символов + '\0' = 8 байт */
        memset(buf, 0xAA, sizeof buf);
        CHECK(safe_strcpy(buf, sizeof buf, "1234567") == 0,
              "strcpy '1234567' (7 символов) в buf[8] → 0 (точно влезает)");
        CHECK(buf[7] == '\0',
              "strcpy: null-terminator на позиции 7");

        /* Усечение — src длиннее dst_size-1 */
        memset(buf, 0xAA, sizeof buf);
        CHECK(safe_strcpy(buf, sizeof buf, "toolongstring") == -1,
              "strcpy слишком длинной строки → -1 (усечено)");
        CHECK(buf[7] == '\0',
              "strcpy при усечении: null-terminator гарантирован");

        /* Пустая строка */
        memset(buf, 0xAA, sizeof buf);
        CHECK(safe_strcpy(buf, sizeof buf, "") == 0,
              "strcpy '' → 0");
        CHECK(buf[0] == '\0',
              "strcpy '': buf[0] == '\\0'");
    }

    printf("=== safe_strcat ===\n");
    {
        char buf[16];

        /* Нормальная конкатенация */
        safe_strcpy(buf, sizeof buf, "hello");
        CHECK(safe_strcat(buf, sizeof buf, " world") == 0,
              "strcat 'hello' + ' world' → 0");
        CHECK(strcmp(buf, "hello world") == 0,
              "strcat: результат 'hello world'");

        /* Усечение */
        safe_strcpy(buf, sizeof buf, "hello");
        CHECK(safe_strcat(buf, sizeof buf, " world and more text") == -1,
              "strcat с усечением → -1");
        CHECK(buf[15] == '\0',
              "strcat при усечении: null-terminator гарантирован");
    }

    printf("=== parse_int ===\n");
    {
        int v = 0xDEAD;

        CHECK(parse_int("42", &v) == 0 && v == 42,
              "parse_int('42') == 0, v == 42");
        CHECK(parse_int("-1", &v) == 0 && v == -1,
              "parse_int('-1') == 0, v == -1");
        CHECK(parse_int("0", &v) == 0 && v == 0,
              "parse_int('0') == 0, v == 0");
        CHECK(parse_int("abc", &v) == -1,
              "parse_int('abc') == -1 (не число)");
        /*
         * 2147483648 > INT_MAX (2147483647 на 32-бит) → overflow.
         * strtol вернёт ERANGE или значение > INT_MAX.
         */
        CHECK(parse_int("2147483648", &v) == -1,
              "parse_int('2147483648') == -1 (overflow int32)");
        CHECK(parse_int("42abc", &v) == -1,
              "parse_int('42abc') == -1 (trailing garbage)");
        CHECK(parse_int("", &v) == -1,
              "parse_int('') == -1 (пустая строка)");
        CHECK(parse_int("-2147483648", &v) == 0 && v == INT_MIN,
              "parse_int('-2147483648') == 0, v == INT_MIN");
        CHECK(parse_int("2147483647", &v) == 0 && v == INT_MAX,
              "parse_int('2147483647') == 0, v == INT_MAX");
    }

    printf("=== safe_strdup ===\n");
    {
        char *copy = safe_strdup("hello");
        CHECK(copy != NULL,
              "strdup 'hello': не NULL");
        if (copy != NULL) {
            CHECK(strcmp(copy, "hello") == 0,
                  "strdup 'hello': содержимое совпадает");
            CHECK(copy != (void *)"hello",  /* другой адрес */
                  "strdup 'hello': новый буфер (не тот же указатель)");
            free(copy);
        } else {
            g_run += 2;
            printf("  [FAIL] strdup содержимое — пропускаем (NULL)\n");
            printf("  [FAIL] strdup адрес — пропускаем (NULL)\n");
        }

        char *empty = safe_strdup("");
        CHECK(empty != NULL,
              "strdup '': не NULL");
        if (empty != NULL) {
            CHECK(empty[0] == '\0',
                  "strdup '': пустая строка скопирована");
            free(empty);
        } else {
            g_run++;
            printf("  [FAIL] strdup '' содержимое — пропускаем (NULL)\n");
        }
    }

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
