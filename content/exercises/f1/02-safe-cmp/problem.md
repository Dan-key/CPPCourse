# Задание: Safe comparison — знаковое vs беззнаковое

Реализуй безопасное сравнение знакового и беззнакового без UB и без предупреждений `-Wsign-compare`.

## Интерфейс

```c
#include <stdbool.h>
#include <stddef.h>

// Возвращает true если a < b (знаковый int < беззнаковый size_t)
bool safe_less_si(int a, size_t b);

// Возвращает true если a < b (знаковый int < unsigned int)
bool safe_less_iu(int a, unsigned b);
```

## Почему обычное сравнение неверно

```c
int a = -1;
size_t b = 0;
if (a < b) { ... }  // НЕВЕРНО: -1 приводится к size_t → SIZE_MAX > 0
```

Usual arithmetic conversions: при сравнении signed и unsigned одного ранга signed приводится к unsigned → отрицательные числа становятся огромными.

## Ожидаемые результаты

| a | b | safe_less_si(a,b) |
|---|---|---|
| -1 | 0 | true |
| 0 | 0 | false |
| 1 | 2 | true |
| 2 | 1 | false |
| INT_MAX | SIZE_MAX | true |
| INT_MIN | 0 | true |

## Подсказка

Если `a < 0` — однозначно `a < (любое беззнаковое)`. Если `a >= 0` — оба неотрицательны, кастуй безопасно.
