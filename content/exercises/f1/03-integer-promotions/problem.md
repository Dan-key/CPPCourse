# Задание: Integer Promotions

Разберись с integer promotion (§6.3.1.1) и usual arithmetic conversions (§6.3.1.8) на четырёх маленьких функциях.

## Интерфейс

```c
#include <stdint.h>

int     uint8_sum_overflows(uint8_t a, uint8_t b);
uint8_t bitwise_not_u8(uint8_t x);
int     signed_less_than_unsigned(void);
int     safe_lt(int a, unsigned b);
```

## Что нужно реализовать

- **`uint8_sum_overflows`** — вернуть `1`, если `a + b` переполнится. Ловушка: `uint8_t` продвигаются до `int` ДО сложения, поэтому `255 + 255 = 510` прекрасно влезает в `int`. Переполнения нет никогда → всегда `0`.
- **`bitwise_not_u8`** — вернуть `(uint8_t)(~x)`. Ловушка: `~` продвигает операнд до `int`, `~(int)0 == -1`, а не `255`. Каст к `uint8_t` отрезает лишние биты.
- **`signed_less_than_unsigned`** — вернуть результат `i < u`, где `i = -1`, `u = 0u`. При usual arithmetic conversions `(unsigned)(-1) = UINT_MAX > 0` → результат `0` (ложь).
- **`safe_lt`** — честное математическое сравнение `a < b` без UB и без `-Wsign-compare`: если `a < 0` → `1`, иначе кастуй `a` к `unsigned` и сравнивай.

## Ожидаемые результаты

| Функция | Вход | Результат |
|---|---|---|
| `uint8_sum_overflows` | `255, 255` | `0` |
| `bitwise_not_u8` | `0` | `255` |
| `bitwise_not_u8` | `0x0F` | `0xF0` |
| `signed_less_than_unsigned` | — | `0` |
| `safe_lt` | `-1, 0` | `1` |
| `safe_lt` | `100, 100` | `0` |
| `safe_lt` | `INT_MAX, 0` | `0` |

## Компиляция и запуск

```bash
gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
    -fsanitize=address,undefined -O1 -g starter.c test.c -o prog && ./prog
```
