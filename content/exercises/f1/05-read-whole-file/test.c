#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

char *read_whole_file(const char *path, size_t *out_len);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

static void write_tmp(const char *path, const char *data, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror("fopen tmp"); exit(1); }
    if (n > 0) fwrite(data, 1, n, f);
    fclose(f);
}

int main(void) {
    size_t len = 0;
    char *buf  = NULL;

    /* ---- 1. Несуществующий файл ---- */
    printf("=== Несуществующий файл ===\n");
    buf = read_whole_file("/tmp/__no_such_file_cppcourse__", &len);
    CHECK(buf == NULL, "NULL на несуществующем файле");

    /* ---- 2. Пустой файл ---- */
    printf("=== Пустой файл ===\n");
    write_tmp("/tmp/cppcourse_empty.bin", "", 0);
    buf = read_whole_file("/tmp/cppcourse_empty.bin", &len);
    CHECK(buf != NULL,    "не NULL на пустом файле");
    CHECK(len == 0,       "len == 0 для пустого файла");
    CHECK(buf && buf[0] == '\0', "нуль-терминатор на пустом файле");
    free(buf); buf = NULL; len = 0;

    /* ---- 3. Обычный файл ---- */
    printf("=== Обычный файл ===\n");
    const char *content = "Hello, system programming!\n";
    size_t content_len = strlen(content);
    write_tmp("/tmp/cppcourse_test.txt", content, content_len);

    buf = read_whole_file("/tmp/cppcourse_test.txt", &len);
    CHECK(buf != NULL,                           "не NULL на обычном файле");
    CHECK(len == content_len,                    "len совпадает с длиной содержимого");
    CHECK(buf && memcmp(buf, content, len) == 0, "содержимое совпадает");
    CHECK(buf && buf[len] == '\0',               "нуль-терминатор присутствует");
    free(buf); buf = NULL; len = 0;

    /* ---- 4. Бинарный файл с нулями ---- */
    printf("=== Бинарный файл с нулями ===\n");
    char binary[8] = {0x00, 0x01, 0x02, 0x00, 0xFF, 0xFE, 0x00, 0x7F};
    write_tmp("/tmp/cppcourse_bin.bin", binary, sizeof(binary));
    buf = read_whole_file("/tmp/cppcourse_bin.bin", &len);
    CHECK(buf != NULL,                                  "не NULL на бинарном файле");
    CHECK(len == 8,                                     "len == 8 для бинарного файла");
    CHECK(buf && memcmp(buf, binary, 8) == 0,           "бинарные данные совпадают");
    free(buf); buf = NULL; len = 0;

    /* Удалить временные файлы */
    remove("/tmp/cppcourse_empty.bin");
    remove("/tmp/cppcourse_test.txt");
    remove("/tmp/cppcourse_bin.bin");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
