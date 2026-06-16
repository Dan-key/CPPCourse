#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>

/*
 * Реализуй через syscall(SYS_write) и т.д.
 * Не вызывай write/read/close/getpid напрямую.
 */

ssize_t my_write(int fd, const void *buf, size_t n) {
    /* TODO: syscall(SYS_write, ...) */
    (void)fd; (void)buf; (void)n;
    return -1;
}

ssize_t my_read(int fd, void *buf, size_t n) {
    /* TODO */
    (void)fd; (void)buf; (void)n;
    return -1;
}

int my_close(int fd) {
    /* TODO */
    (void)fd;
    return -1;
}

pid_t my_getpid(void) {
    /* TODO */
    return -1;
}
