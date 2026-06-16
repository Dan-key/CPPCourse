# Задание: рекурсивная статистика директорий

Реализуй рекурсивный обход директорий и поиск наибольшего файла.

## Интерфейс

```c
typedef struct {
    long file_count;
    long dir_count;
    long symlink_count;
    long total_size;
} dir_stats_t;

int  collect_stats(const char *path, dir_stats_t *stats);
long find_largest_file(const char *dir_path, char *name_buf, size_t buf_size);
```

## Требования

### `collect_stats`
- Рекурсивный обход через `opendir()` / `readdir()` / `closedir()`
- **Использовать `lstat()`, а не `stat()`** — не следовать символическим ссылкам
- Пропускать `.` и `..`
- `S_ISREG` → `file_count++`, `total_size += st.st_size`
- `S_ISDIR` → `dir_count++`, рекурсивный вызов
- `S_ISLNK` → `symlink_count++` (только с `lstat`!)
- `stats` не обнуляется внутри — вызывающий инициализирует перед первым вызовом

### `find_largest_file`
- Нерекурсивный обход, только регулярные файлы
- Скопировать **имя** (не полный путь) наибольшего файла в `name_buf`
- Вернуть размер в байтах, `0` если нет файлов, `-1` при ошибке

## Компиляция

```bash
gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
    -fsanitize=address,undefined -O1 -g solution.c test.c -o prog
./prog
```
