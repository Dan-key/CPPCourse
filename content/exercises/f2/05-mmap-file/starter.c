#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

/* Прочитать содержимое файла через mmap.
   Возвращает указатель на аллоцированный буфер с содержимым (null-terminated),
   *size = размер файла (без нуль-терминатора).
   Вызывающий должен free() результат.
   Возвращает NULL при ошибке. */
char *mmap_read_file(const char *path, size_t *size) {
    /* TODO:
       1. open(O_RDONLY)
       2. fstat → st_size
       3. Если size == 0: malloc(1), buf[0]=0, *size=0, close, return buf
       4. mmap(MAP_PRIVATE, PROT_READ)
       5. malloc(st_size + 1), memcpy, buf[st_size] = 0
       6. munmap, close
       7. *size = st_size, return buf */
    (void)path; (void)size;
    return NULL;
}

/* Записать данные в файл через mmap (создать/перезаписать).
   Возвращает 0 при успехе, -1 при ошибке.
   Гарантирует msync перед возвратом. */
int mmap_write_file(const char *path, const void *data, size_t size) {
    /* TODO:
       1. open(O_RDWR|O_CREAT|O_TRUNC, 0644)
       2. Если size == 0: close, return 0
       3. ftruncate(fd, size) — установить размер файла
       4. mmap(MAP_SHARED, PROT_READ|PROT_WRITE)
       5. memcpy(mapped, data, size)
       6. msync(MS_SYNC)
       7. munmap, close */
    (void)path; (void)data; (void)size;
    return -1;
}

/* Посчитать вхождения символа c в файле через mmap (без копирования в буфер).
   Возвращает количество вхождений, -1 при ошибке.
   Файл нулевой длины → 0. */
ssize_t mmap_count_char(const char *path, char c) {
    /* TODO:
       1. open(O_RDONLY)
       2. fstat → size
       3. Если size == 0: close, return 0
       4. mmap(MAP_PRIVATE, PROT_READ)
       5. Подсчёт c в mapped[0..size-1]
       6. munmap, close */
    (void)path; (void)c;
    return -1;
}
