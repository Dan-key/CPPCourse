# Задание: mmap — чтение и запись файлов

Реализуй операции с файлами через `mmap` вместо `read()`/`write()`.

## Интерфейс

```c
char    *mmap_read_file(const char *path, size_t *size);
int      mmap_write_file(const char *path, const void *data, size_t size);
ssize_t  mmap_count_char(const char *path, char c);
```

## Требования

### `mmap_read_file`
- Открыть файл `O_RDONLY`, получить размер через `fstat()`
- Отобразить через `mmap(MAP_PRIVATE, PROT_READ)`
- Скопировать содержимое в `malloc`-буфер, добавить `'\0'`
- `munmap()` + `close()` (порядок неважен)
- Вернуть буфер (вызывающий должен `free()`), записать размер в `*size`
- Пустой файл → вернуть `malloc(1)` с `'\0'`, `*size = 0`

### `mmap_write_file`
- Открыть/создать файл `O_RDWR|O_CREAT|O_TRUNC`
- `ftruncate(fd, size)` — установить размер (нужно до `mmap`!)
- `mmap(MAP_SHARED, PROT_READ|PROT_WRITE)`
- `memcpy()`, затем `msync(MS_SYNC)`, затем `munmap()` + `close()`

### `mmap_count_char`
- Отобразить файл через `mmap(MAP_PRIVATE, PROT_READ)`, подсчитать символы, `munmap`

## Ловушки
- `mmap()` при ошибке возвращает `MAP_FAILED`, **не** `NULL`
- Нельзя `mmap` файл нулевой длины — проверяй `st_size > 0` перед вызовом
- `munmap()` не закрывает `fd` — закрывать отдельно

## Компиляция

```bash
gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
    -fsanitize=address,undefined -O1 -g solution.c test.c -o prog
./prog
```
