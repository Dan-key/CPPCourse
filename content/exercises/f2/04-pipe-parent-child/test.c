#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

/* Примечание: при запуске с AddressSanitizer используй:
   ASAN_OPTIONS=detect_leaks=0 ./prog
   чтобы избежать ложных срабатываний LeakSanitizer в дочерних процессах. */

int send_to_child(const char *message);
ssize_t capture_command_output(const char *const argv[], char *buf, size_t bufsz);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    /* ---- 1-3: send_to_child ---- */
    printf("=== send_to_child ===\n");
    int r;

    r = send_to_child("hello");
    CHECK(r == 5, "send_to_child(\"hello\") == 5");

    r = send_to_child("");
    CHECK(r == 0, "send_to_child(\"\") == 0");

    r = send_to_child("test message");
    CHECK(r == 12, "send_to_child(\"test message\") == 12");

    r = send_to_child("x");
    CHECK(r == 1, "send_to_child(\"x\") == 1 (1-байтовое сообщение)");

    /* ---- 5-10: capture_command_output ---- */
    printf("=== capture_command_output ===\n");
    char buf[4096];
    memset(buf, 0, sizeof(buf));

    /* echo -n hello */
    {
        const char *argv[] = {"/bin/echo", "-n", "hello", NULL};
        ssize_t n = capture_command_output(argv, buf, sizeof(buf));
        CHECK(n == 5, "capture echo -n hello: 5 байт");
        CHECK(memcmp(buf, "hello", 5) == 0, "capture echo -n hello: содержимое 'hello'");
    }

    /* echo -n '' */
    {
        memset(buf, 0, sizeof(buf));
        const char *argv[] = {"/bin/echo", "-n", "", NULL};
        ssize_t n = capture_command_output(argv, buf, sizeof(buf));
        CHECK(n == 0, "capture echo -n '': 0 байт");
    }

    /* echo line (с переносом строки) */
    {
        memset(buf, 0, sizeof(buf));
        const char *argv[] = {"/bin/echo", "line", NULL};
        ssize_t n = capture_command_output(argv, buf, sizeof(buf));
        CHECK(n >= 5, "capture echo line: >= 5 байт");
        CHECK(buf[n-1] == '\n', "capture echo line: последний байт — перенос строки");
    }

    /* Проверка что write-концы закрываются (нет зависания) */
    {
        memset(buf, 0, sizeof(buf));
        const char *argv[] = {"/bin/echo", "-n", "noblock", NULL};
        ssize_t n = capture_command_output(argv, buf, sizeof(buf));
        CHECK(n == 7, "write-концы закрыты корректно (нет зависания)");
    }

    /* Многострочный вывод */
    {
        memset(buf, 0, sizeof(buf));
        /* printf передаёт строки разделённые \n */
        const char *argv[] = {"/usr/bin/printf", "line1\nline2\nline3\n", NULL};
        ssize_t n = capture_command_output(argv, buf, sizeof(buf));
        CHECK(n == 18, "capture printf: 18 байт (3 строки)");
    }

    /* send_to_child со строкой длиной 10 */
    r = send_to_child("0123456789");
    CHECK(r == 10, "send_to_child(\"0123456789\") == 10");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
