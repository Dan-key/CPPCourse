#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>

ssize_t my_write(int fd, const void *buf, size_t n);
ssize_t my_read(int fd, void *buf, size_t n);
int     my_close(int fd);
pid_t   my_getpid(void);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    /* ---- getpid ---- */
    printf("=== my_getpid ===\n");
    pid_t my  = my_getpid();
    pid_t ref = getpid();
    CHECK(my == ref, "my_getpid() == getpid()");
    CHECK(my > 0,    "my_getpid() > 0");

    /* ---- write/read через temp file ---- */
    printf("=== my_write / my_read ===\n");
    char tmpname[] = "/tmp/cppcourse_f2_XXXXXX";
    int fd = mkstemp(tmpname);
    CHECK(fd >= 0, "mkstemp OK");

    const char *msg = "Hello syscall!";
    ssize_t n = my_write(fd, msg, strlen(msg));
    CHECK(n == (ssize_t)strlen(msg), "my_write wrote correct count");

    lseek(fd, 0, SEEK_SET);
    char buf[64] = {0};
    ssize_t r = my_read(fd, buf, sizeof(buf) - 1);
    CHECK(r == (ssize_t)strlen(msg), "my_read returned correct count");
    CHECK(memcmp(buf, msg, strlen(msg)) == 0, "my_read content matches");

    /* ---- close ---- */
    printf("=== my_close ===\n");
    int ret = my_close(fd);
    CHECK(ret == 0, "my_close returned 0");

    /* Закрытый fd: повторный close должен вернуть -1, errno = EBADF */
    errno = 0;
    ret = my_close(fd);
    CHECK(ret == -1,       "my_close on closed fd returns -1");
    CHECK(errno == EBADF,  "errno == EBADF for closed fd");

    /* ---- write на bad fd ---- */
    printf("=== errno на ошибках ===\n");
    errno = 0;
    n = my_write(-1, "x", 1);
    CHECK(n == -1,         "my_write(-1,...) returns -1");
    CHECK(errno == EBADF,  "my_write(-1,...) sets EBADF");

    remove(tmpname);

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
