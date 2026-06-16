#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

typedef struct {
    long file_count;    /* обычные файлы */
    long dir_count;     /* директории (не считая . и ..) */
    long symlink_count; /* символические ссылки */
    long total_size;    /* суммарный размер файлов в байтах */
} dir_stats_t;

/* Рекурсивно обойти директорию path и заполнить stats.
   Не следовать символическим ссылкам (использовать lstat, не stat).
   Возвращает 0 при успехе, -1 при ошибке.
   Примечание: stats не обнуляется внутри функции —
   вызывающий должен инициализировать его перед вызовом. */
int collect_stats(const char *path, dir_stats_t *stats) {
    /* TODO:
       1. opendir(path) — если NULL, вернуть -1
       2. readdir в цикле
       3. Пропускать "." и ".."
       4. Построить полный путь: snprintf(child, sizeof(child), "%s/%s", path, e->d_name)
       5. lstat(child, &st) — НЕ stat (чтобы не следовать symlink)
       6. S_ISREG → file_count++, total_size += st.st_size
          S_ISDIR → dir_count++, рекурсивный вызов collect_stats(child, stats)
          S_ISLNK → symlink_count++
       7. closedir, return 0 */
    (void)path; (void)stats;
    return -1;
}

/* Найти самый большой файл в директории (нерекурсивно).
   Записать имя файла в name_buf (не более buf_size байт включая '\0').
   Возвращает размер найденного файла (> 0), 0 если директория пуста/нет файлов,
   -1 при ошибке (нет директории и т.п.). */
long find_largest_file(const char *dir_path, char *name_buf, size_t buf_size) {
    /* TODO:
       1. opendir
       2. Перебрать все записи (readdir), для регулярных файлов (lstat) запомнить max
       3. Скопировать имя наибольшего файла в name_buf через snprintf
       4. closedir, вернуть размер (0 если нет файлов, -1 при ошибке открытия) */
    (void)dir_path; (void)name_buf; (void)buf_size;
    return -1;
}
