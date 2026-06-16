#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

/*
 * Гарантированная полная запись n байт в fd.
 * Повторяет при EINTR. Учитывает частичный write.
 * Возвращает n при успехе, -1 при ошибке.
 */
ssize_t write_all(int fd, const void *buf, size_t n) {
    /* TODO */
    (void)fd; (void)buf; (void)n;
    return -1;
}
