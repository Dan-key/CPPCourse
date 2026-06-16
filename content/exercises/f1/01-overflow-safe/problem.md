# Задание: Overflow-safe arithmetic

Реализуй функции безопасной арифметики с детектом переполнения через `__builtin_*_overflow`.

## Интерфейс

```c
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Возвращает true если переполнилось.
// Если не переполнилось — *out содержит результат.
bool add_safe_int(int a, int b, int *out);
bool mul_safe_int(int a, int b, int *out);
bool add_safe_size(size_t a, size_t b, size_t *out);
```

## Что проверяется

- `add_safe_int(INT_MAX, 1, &r)` → `true` (переполнение)
- `add_safe_int(1, 2, &r)` → `false`, `r == 3`
- `add_safe_int(INT_MIN, -1, &r)` → `true`
- `mul_safe_int(INT_MAX, 2, &r)` → `true`
- `mul_safe_int(0, INT_MAX, &r)` → `false`, `r == 0`
- `mul_safe_int(INT_MIN, -1, &r)` → `true`
- `add_safe_size(SIZE_MAX, 1, &r)` → `true`

## Подсказка

Используй `__builtin_add_overflow(a, b, out)` и `__builtin_mul_overflow(a, b, out)` — GCC/Clang. Возвращают `1` если переполнение, `0` если нет. `*out` корректен только если нет переполнения.

## Компиляция и запуск

```bash
gcc -std=c17 -Wall -Wextra -fsanitize=address,undefined -O1 solution.c test.c -o test && ./test
```
