#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>

char *mmap_read_file(const char *path, size_t *size);
int   mmap_write_file(const char *path, const void *data, size_t size);
ssize_t mmap_count_char(const char *path, char c);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

/* Вспомогательная функция: создать временный файл с заданным содержимым */
static char *make_tmpfile(const char *content, size_t len, char *tmpl) {
    int fd = mkstemp(tmpl);
    if (fd < 0) return NULL;
    if (len > 0) {
        ssize_t w = write(fd, content, len);
        (void)w;
    }
    close(fd);
    return tmpl;
}

int main(void) {
    /* ---- 1: mmap_write_file создаёт файл ---- */
    printf("=== mmap_write_file ===\n");
    char tmp1[] = "/tmp/cppcourse_mmap1_XXXXXX";
    {
        int fd = mkstemp(tmp1); close(fd); /* занять имя */
        int r = mmap_write_file(tmp1, "hello", 5);
        CHECK(r == 0, "mmap_write_file возвращает 0");
    }

    /* ---- 2: mmap_read_file читает то, что записано ---- */
    printf("=== mmap_read_file ===\n");
    {
        size_t sz = 0;
        char *buf = mmap_read_file(tmp1, &sz);
        CHECK(buf != NULL, "mmap_read_file != NULL");
        CHECK(sz == 5, "mmap_read_file: size == 5");
        CHECK(buf != NULL && memcmp(buf, "hello", 5) == 0, "содержимое 'hello'");
        free(buf);
    }

    /* ---- 3: round-trip write + read ---- */
    printf("=== Round-trip ===\n");
    char tmp2[] = "/tmp/cppcourse_mmap2_XXXXXX";
    {
        int fd = mkstemp(tmp2); close(fd);
        const char *data = "The quick brown fox";
        size_t dlen = strlen(data);
        mmap_write_file(tmp2, data, dlen);
        size_t sz = 0;
        char *buf = mmap_read_file(tmp2, &sz);
        CHECK(buf != NULL && sz == dlen && memcmp(buf, data, dlen) == 0,
              "round-trip write+read корректен");
        free(buf);
    }

    /* ---- 4: mmap_read_file на несуществующем файле → NULL ---- */
    printf("=== Несуществующий файл ===\n");
    {
        size_t sz = 99;
        char *buf = mmap_read_file("/tmp/cppcourse_nonexistent_xyz123", &sz);
        CHECK(buf == NULL, "mmap_read_file на несущ. файле → NULL");
    }

    /* ---- 5-6: mmap_count_char ---- */
    printf("=== mmap_count_char ===\n");
    char tmp3[] = "/tmp/cppcourse_mmap3_XXXXXX";
    {
        int fd = mkstemp(tmp3); close(fd);
        const char *text = "abracadabra";
        mmap_write_file(tmp3, text, strlen(text));
        ssize_t cnt = mmap_count_char(tmp3, 'a');
        CHECK(cnt == 5, "mmap_count_char('a') в 'abracadabra' == 5");
        cnt = mmap_count_char(tmp3, 'z');
        CHECK(cnt == 0, "mmap_count_char('z') (нет в файле) == 0");
    }

    /* ---- 7: пустой файл ---- */
    printf("=== Пустой файл ===\n");
    char tmp4[] = "/tmp/cppcourse_mmap4_XXXXXX";
    {
        int fd = mkstemp(tmp4); close(fd);
        /* Файл уже пустой */
        size_t sz = 99;
        char *buf = mmap_read_file(tmp4, &sz);
        CHECK(buf != NULL, "mmap_read_file пустого файла != NULL");
        CHECK(sz == 0, "mmap_read_file пустого файла: size == 0");
        free(buf);
        ssize_t cnt = mmap_count_char(tmp4, 'a');
        CHECK(cnt == 0, "mmap_count_char на пустом файле == 0");
    }

    /* ---- 8: mmap_write_file с size=0 ---- */
    {
        char tmp5[] = "/tmp/cppcourse_mmap5_XXXXXX";
        int fd = mkstemp(tmp5); close(fd);
        int r = mmap_write_file(tmp5, "", 0);
        CHECK(r == 0, "mmap_write_file с size=0 возвращает 0");
        remove(tmp5);
    }

    /* ---- 9-10: большой файл (64K) ---- */
    printf("=== Большой файл ===\n");
    char tmp6[] = "/tmp/cppcourse_mmap6_XXXXXX";
    {
        int fd = mkstemp(tmp6); close(fd);
        const int BIG = 65536;
        char *bigdata = malloc((size_t)BIG);
        for (int i = 0; i < BIG; i++) bigdata[i] = (char)('A' + (i % 26));
        mmap_write_file(tmp6, bigdata, (size_t)BIG);
        size_t sz = 0;
        char *buf = mmap_read_file(tmp6, &sz);
        CHECK(buf != NULL && (int)sz == BIG, "большой файл: размер 64K");
        CHECK(buf != NULL && memcmp(buf, bigdata, (size_t)BIG) == 0,
              "большой файл: содержимое совпадает");
        free(buf);
        free(bigdata);
    }

    /* ---- 11: результат mmap_read_file null-terminated ---- */
    printf("=== Null-terminated ===\n");
    {
        size_t sz = 0;
        char *buf = mmap_read_file(tmp2, &sz);
        CHECK(buf != NULL && buf[sz] == '\0', "результат null-terminated");
        free(buf);
    }

    /* ---- 12: write + count_char ---- */
    printf("=== write + count_char ===\n");
    {
        char tmp7[] = "/tmp/cppcourse_mmap7_XXXXXX";
        int fd = mkstemp(tmp7); close(fd);
        mmap_write_file(tmp7, "banana", 6);
        ssize_t cnt = mmap_count_char(tmp7, 'a');
        CHECK(cnt == 3, "mmap_count_char 'a' в 'banana' == 3");
        remove(tmp7);
    }

    remove(tmp1);
    remove(tmp2);
    remove(tmp3);
    remove(tmp4);
    remove(tmp6);

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
