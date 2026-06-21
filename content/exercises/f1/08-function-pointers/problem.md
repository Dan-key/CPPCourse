# Задание: Function Pointers и Dispatch Table

Реализуй компараторы для `qsort`, обёртку сортировки и dispatch table — замену `switch`/vtable на массив указателей на функции.

## Интерфейс

```c
#include <stddef.h>

typedef int (*cmp_fn)(const void *a, const void *b);

int  cmp_int_asc(const void *a, const void *b);
int  cmp_int_desc(const void *a, const void *b);
void sort_ints(int *arr, size_t n, cmp_fn cmp);
int  dispatch(int op, int arg);
```

## Что нужно реализовать

- **`cmp_int_asc`** — компаратор по возрастанию для `qsort`. **Не используй `*x - *y`** — переполнение для `INT_MIN`/`-1`! Применяй `(*x > *y) - (*x < *y)` или сравнение через `if`.
- **`cmp_int_desc`** — по убыванию (поменяй порядок аргументов).
- **`sort_ints`** — обёртка над `qsort(arr, n, sizeof *arr, cmp)`.
- **`dispatch`** — выполнить операцию по коду через **dispatch table** (массив `handler_fn`), НЕ через `switch`/`if-else`. `0→op_double`, `1→op_square`, `2→op_negate`, `3→op_identity`; вне `[0,3]` → `-1`.

## Зачем dispatch table

Вместо `switch(op) { case 0: ...; case 1: ...; }` индексируем массив — это паттерн, аналогичный vtable в C++. Преимущества: O(1), легко расширяется, нет ошибок fall-through.

## Ожидаемые результаты

- `cmp_int_asc(3, 7) < 0`, `cmp_int_asc(3, 3) == 0`, `cmp_int_desc(3, 7) > 0`
- `sort_ints([3,1,4,1,5,9,2,6], asc)` → `[1,1,2,3,4,5,6,9]`
- `sort_ints(arr, 0, ...)` — пустой массив не падает
- `dispatch(0, 5) == 10`, `dispatch(1, 5) == 25`, `dispatch(2, 5) == -5`, `dispatch(3, 7) == 7`

## Компиляция и запуск

```bash
gcc -std=c17 -Wall -Wextra -fsanitize=address,undefined \
    -O1 -g starter.c test.c -o prog && ./prog
```
