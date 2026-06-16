# Задание: read_full / write_full

Реализуй гарантированное чтение и запись с обработкой `EINTR` и частичного I/O.

## Интерфейс

```c
ssize_t read_full(int fd, void *buf, size_t n);
ssize_t write_full(int fd, const void *buf, size_t n);
```

## Требования

### `read_full`
- Читает ровно `n` байт в `buf`
- При `EINTR` — повторяет вызов автоматически
- При частичном read — продолжает с оставшимися байтами
- При EOF (read вернул 0) — возвращает прочитанное количество (может быть < n)
- При ошибке — возвращает -1, `errno` установлен

### `write_full`
- Записывает ровно `n` байт из `buf`
- При `EINTR` — повторяет вызов автоматически
- При частичном write — продолжает с оставшимися байтами
- При `n == 0` — возвращает 0 немедленно
- При ошибке — возвращает -1, `errno` установлен

## Включения

```c
#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
```

## Компиляция

```bash
gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
    -fsanitize=address,undefined -O1 -g solution.c test.c -o prog
./prog
```
