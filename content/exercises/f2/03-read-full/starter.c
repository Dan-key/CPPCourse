#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>

/* Гарантированно читает ровно n байт.
   Возвращает n при успехе, меньше n при EOF до n байт,
   -1 при ошибке (errno установлен).
   EINTR → перезапускает автоматически. */
ssize_t read_full(int fd, void *buf, size_t n) {
    return -1; /* TODO */
}

/* Гарантированно записывает ровно n байт.
   Возвращает n при успехе, -1 при ошибке.
   EINTR → перезапускает автоматически.
   Partial write → продолжает с остатком. */
ssize_t write_full(int fd, const void *buf, size_t n) {
    return -1; /* TODO */
}
