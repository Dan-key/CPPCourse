#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>

ssize_t read_full(int fd, void *buf, size_t n);
ssize_t write_full(int fd, const void *buf, size_t n);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    /* ---- 1-2: round-trip через pipe ---- */
    printf("=== Round-trip ===\n");
    int pfd[2];
    pipe(pfd);
    const char *msg = "Hello, pipe!";
    size_t mlen = strlen(msg);

    ssize_t wr = write_full(pfd[1], msg, mlen);
    CHECK(wr == (ssize_t)mlen, "write_full вернул mlen байт");

    char rbuf[64] = {0};
    ssize_t rd = read_full(pfd[0], rbuf, mlen);
    CHECK(rd == (ssize_t)mlen && memcmp(rbuf, msg, mlen) == 0,
          "read_full вернул те же байты");

    /* ---- 3: EOF когда write-конец закрыт ---- */
    printf("=== EOF при закрытом write-конце ===\n");
    close(pfd[1]);
    rd = read_full(pfd[0], rbuf, sizeof(rbuf));
    CHECK(rd == 0, "read_full возвращает 0 при EOF");
    close(pfd[0]);

    /* ---- 4: write_full на bad fd → -1, EBADF ---- */
    printf("=== Ошибки ===\n");
    errno = 0;
    wr = write_full(-1, "x", 1);
    CHECK(wr == -1, "write_full(-1,...) возвращает -1");
    CHECK(errno == EBADF, "write_full(-1,...) errno == EBADF");

    /* ---- 5: write_full 0 байт → 0 ---- */
    int devnull = open("/dev/null", O_WRONLY);
    wr = write_full(devnull, "x", 0);
    CHECK(wr == 0, "write_full с n=0 возвращает 0");
    close(devnull);

    /* ---- 6: read_full 0 байт → 0 ---- */
    int pfd2[2];
    pipe(pfd2);
    rd = read_full(pfd2[0], rbuf, 0);
    CHECK(rd == 0, "read_full с n=0 возвращает 0");
    close(pfd2[0]);
    close(pfd2[1]);

    /* ---- 7-8: большие данные (65536 байт) ---- */
    printf("=== Большие данные ===\n");
    const int BIG = 65536;
    char *wbuf = malloc((size_t)BIG);
    char *readback = malloc((size_t)BIG);
    for (int i = 0; i < BIG; i++) wbuf[i] = (char)(i & 0x7f);

    int pfd3[2];
    pipe(pfd3);
    /* Пишем в отдельном потоке было бы правильнее, но для простоты:
       pipe-буфер = 64KiB, поэтому ровно 65536 байт может заблокироваться.
       Используем неблокирующий write-конец + цикл чтения. */
    /* Записываем кусками по PIPE_BUF чтобы не заблокироваться */
    {
        ssize_t total_wr = 0;
        size_t rem = (size_t)BIG;
        const char *p = wbuf;
        while (rem > 0) {
            size_t chunk = rem > 4096 ? 4096 : rem;
            ssize_t w = write(pfd3[1], p, chunk);
            if (w <= 0) break;
            /* read enough to make room */
            ssize_t r = read(pfd3[0], readback + total_wr, (size_t)w);
            total_wr += r;
            p += w;
            rem -= (size_t)w;
        }
        /* Verify via a proper pipe + write_full / read_full on a temp file instead */
        (void)total_wr;
    }
    close(pfd3[0]);
    close(pfd3[1]);

    /* Round-trip через temp file */
    char tmpname[] = "/tmp/cppcourse_rf_XXXXXX";
    int tfd = mkstemp(tmpname);
    wr = write_full(tfd, wbuf, (size_t)BIG);
    CHECK(wr == BIG, "write_full записал 64K байт");
    lseek(tfd, 0, SEEK_SET);
    rd = read_full(tfd, readback, (size_t)BIG);
    CHECK(rd == BIG, "read_full прочитал 64K байт");
    CHECK(memcmp(wbuf, readback, (size_t)BIG) == 0, "64K байт совпадают");
    close(tfd);
    free(wbuf);
    free(readback);

    /* ---- 9: read_full на закрытом fd → -1 ---- */
    printf("=== read_full на закрытом fd ===\n");
    int pfd4[2];
    pipe(pfd4);
    close(pfd4[0]);
    close(pfd4[1]);
    errno = 0;
    char tmp[4];
    rd = read_full(pfd4[0], tmp, 4);
    CHECK(rd == -1, "read_full на закрытом fd возвращает -1");

    /* ---- 10: write_full в /dev/null всегда успешен ---- */
    printf("=== /dev/null ===\n");
    int dn = open("/dev/null", O_WRONLY);
    wr = write_full(dn, "abcdefgh", 8);
    CHECK(wr == 8, "write_full в /dev/null → 8");
    close(dn);

    /* ---- 11: errno после write_full на bad fd сохраняется ---- */
    printf("=== Сохранение errno ===\n");
    errno = 0;
    write_full(-1, "x", 1);
    int saved = errno;
    CHECK(saved == EBADF, "errno == EBADF после write_full(-1)");

    /* ---- 12: write_full записывает все байты (проверка через read back) ---- */
    printf("=== Полнота записи ===\n");
    char tmpname2[] = "/tmp/cppcourse_rf2_XXXXXX";
    int tfd2 = mkstemp(tmpname2);
    const char *sentence = "The quick brown fox jumps over the lazy dog";
    size_t slen = strlen(sentence);
    write_full(tfd2, sentence, slen);
    lseek(tfd2, 0, SEEK_SET);
    char vbuf[64] = {0};
    read(tfd2, vbuf, sizeof(vbuf) - 1);
    CHECK(strcmp(vbuf, sentence) == 0, "все байты записаны корректно");
    close(tfd2);
    remove(tmpname);
    remove(tmpname2);

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
