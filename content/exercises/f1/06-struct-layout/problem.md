# Задание: Struct Layout и container_of

Разберись с выравниванием, padding и `offsetof`, перепакуй структуру для минимального размера и реализуй макрос ядра `container_of`.

## Интерфейс

```c
#include <stddef.h>

size_t bad_layout_size(void);
size_t good_layout_size(void);
size_t bad_b_offset(void);
size_t good_d_offset(void);

struct list_item *item_from_node(struct node *n);
```

## Что нужно сделать

- **`struct good_layout`** — переставь поля `bad_layout` (`char a; int b; char c; double d; short e;`) так, чтобы `sizeof` был минимальным. Правило: поля с бо́льшим выравниванием раньше. Цель — `sizeof(good_layout) == 16` против `sizeof(bad_layout) == 32`.
- **`item_from_node`** — по указателю на встроенный `node` внутри `list_item` верни указатель на сам `list_item` через макрос `container_of`.

## Разбор padding для `bad_layout`

```
offset 0:  char  a      (1)   offset 9:  pad   (7)  → выравнивание double
offset 1:  pad   (3)         offset 16: double d  (8)
offset 4:  int   b      (4)   offset 24: short  e  (2)
offset 8:  char  c      (1)   offset 26: pad    (6) → выравнивание struct
                              sizeof = 32
```

## container_of

```c
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
```
Адрес поля минус его смещение внутри структуры = адрес начала структуры.

## Ожидаемые результаты

- `bad_layout_size() == 32`, `good_layout_size() == 16`
- `bad_b_offset() == 4` (после `char a` + 3 байта padding)
- `good_d_offset() == 0` (`double` стоит первым)
- `item_from_node(&item.node) == &item`, поля восстанавливаются корректно

## Компиляция и запуск

```bash
gcc -std=c17 -Wall -Wextra -fsanitize=address,undefined \
    -O1 -g starter.c test.c -o prog && ./prog
```
