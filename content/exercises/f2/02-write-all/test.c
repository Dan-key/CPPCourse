#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>

ssize_t write_all(int fd, const void *buf, size_t n);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    /* ---- Обычная запись ---- */
    printf("=== Обычная запись ===\n");
    char tmpname[] = "/tmp/cppcourse_wa_XXXXXX";
    int fd = mkstemp(tmpname);
    CHECK(fd >= 0, "mkstemp OK");

    const char *data = "Hello, write_all!";
    size_t dlen = strlen(data);

    ssize_t r = write_all(fd, data, dlen);
    CHECK(r == (ssize_t)dlen, "write_all вернул правильное кол-во байт");

    /* Прочитать и проверить содержимое */
    lseek(fd, 0, SEEK_SET);
    char buf[64] = {0};
    ssize_t rb = read(fd, buf, sizeof(buf) - 1);
    CHECK(rb == (ssize_t)dlen, "содержимое записано полностью");
    CHECK(memcmp(buf, data, dlen) == 0, "данные совпадают");
    close(fd);

    /* ---- Запись на bad fd ---- */
    printf("=== Ошибка EBADF ===\n");
    errno = 0;
    r = write_all(-1, "x", 1);
    CHECK(r == -1,          "write_all(-1,...) возвращает -1");
    CHECK(errno == EBADF,   "errno == EBADF");

    /* ---- Запись нуля байт ---- */
    printf("=== Нулевая запись ===\n");
    int fd2 = open("/dev/null", O_WRONLY);
    r = write_all(fd2, "x", 0);
    CHECK(r == 0, "write_all с n=0 возвращает 0");
    close(fd2);

    /* ---- Большой объём данных ---- */
    printf("=== Большая запись ===\n");
    char tmpname2[] = "/tmp/cppcourse_wa2_XXXXXX";
    int fd3 = mkstemp(tmpname2);
    const int BIG = 65536;
    char *bigbuf = malloc(BIG);
    memset(bigbuf, 'A', BIG);
    r = write_all(fd3, bigbuf, BIG);
    CHECK(r == BIG, "write_all записал 64K байт");
    close(fd3);
    free(bigbuf);

    remove(tmpname);
    remove(tmpname2);

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
