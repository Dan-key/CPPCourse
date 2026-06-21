# Задание: Safe Strings

Реализуй безопасные строковые операции с гарантией null-terminator и парсинг целого через `strtol` с полной проверкой ошибок.

## Интерфейс

```c
#include <stddef.h>

int   safe_strcpy(char *dst, size_t dst_size, const char *src);
int   safe_strcat(char *dst, size_t dst_size, const char *src);
int   parse_int(const char *s, int *out);
char *safe_strdup(const char *s);
```

## Что нужно реализовать

- **`safe_strcpy`** — копирует `src` в `dst`, не более `dst_size` байт включая `'\0'`. `dst` ВСЕГДА заканчивается `'\0'`. Возвращает `0`, если влезло целиком, `-1` при усечении.
- **`safe_strcat`** — дописывает `src` к строке в `dst`; `dst_size` — полный размер буфера. Результат всегда null-terminated. `0` / `-1` как выше.
- **`parse_int`** — разбор целого через `strtol` с полной проверкой: пустая строка, нечисловые символы, trailing garbage (`"42abc"`), переполнение, выход за `[INT_MIN, INT_MAX]` — всё это ошибка (`-1`).
- **`safe_strdup`** — дублирует строку через `malloc`, возвращает `NULL` при ошибке аллокации или `NULL`-входе. Буфер освобождает вызывающий.

## Канонический паттерн strtol

```c
char *end;
errno = 0;
long val = strtol(s, &end, 10);
if (errno == ERANGE || end == s || *end != '\0') return -1;  // ошибка
if (val < INT_MIN || val > INT_MAX)               return -1;  // вне int
*out = (int)val; return 0;
```

## Крайние случаи

- `safe_strcpy` точно в размер: `"1234567"` (7 симв.) в `buf[8]` → `0`, `buf[7] == '\0'`
- `parse_int("2147483648")` → `-1` (overflow int32), `parse_int("-2147483648")` → `0`, `INT_MIN`
- `parse_int("")`, `parse_int("abc")`, `parse_int("42abc")` → `-1`
- `safe_strdup("")` → валидный буфер с `'\0'`, не `NULL`

## Компиляция и запуск

```bash
gcc -std=c17 -Wall -Wextra -Wconversion -fsanitize=address,undefined \
    -O1 -g starter.c test.c -o prog && ./prog
```
