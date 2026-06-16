#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/*
 * Примечание: при запуске с AddressSanitizer используй:
 *   ASAN_OPTIONS=detect_leaks=0 ./prog
 * чтобы избежать ложных срабатываний LeakSanitizer в дочерних процессах.
 */

int fork_and_collect(int n, int *results);
int run_echo(const char *arg);
int check_signal_death(int sig);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

/* Проверить что массив results содержит все значения 1..n (в любом порядке) */
static int contains_all(const int *results, int n)
{
    int found[64] = {0};
    if (n <= 0 || n > 63) return 0;
    for (int i = 0; i < n; i++) {
        int v = results[i];
        if (v < 1 || v > n) return 0;
        found[v - 1]++;
    }
    for (int i = 0; i < n; i++) {
        if (found[i] != 1) return 0;
    }
    return 1;
}

/* Проверить что все элементы уникальны */
static int all_unique(const int *results, int n)
{
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (results[i] == results[j]) return 0;
        }
    }
    return 1;
}

int main(void)
{
    printf("=== fork_and_collect ===\n");

    /* Тест 1: n=3 — получить коды 1,2,3 в любом порядке */
    {
        int results[3] = {0, 0, 0};
        int rc = fork_and_collect(3, results);
        CHECK(rc == 0, "fork_and_collect(3): возвращает 0");
        CHECK(contains_all(results, 3),
              "fork_and_collect(3): results содержит {1,2,3} в любом порядке");
    }

    /* Тест 2: n=1 */
    {
        int results[1] = {0};
        int rc = fork_and_collect(1, results);
        CHECK(rc == 0, "fork_and_collect(1): возвращает 0");
        CHECK(results[0] == 1, "fork_and_collect(1): results[0] == 1");
    }

    /* Тест 3: n=0 — граничный случай, нет потомков */
    {
        int rc = fork_and_collect(0, NULL);
        CHECK(rc == 0, "fork_and_collect(0): возвращает 0, без fork()");
    }

    /* Тест 4: n=5 */
    {
        int results[5] = {0};
        int rc = fork_and_collect(5, results);
        CHECK(rc == 0, "fork_and_collect(5): возвращает 0");
        CHECK(contains_all(results, 5),
              "fork_and_collect(5): results содержит {1,2,3,4,5}");
    }

    /* Тест 5: уникальность результатов при n=3 */
    {
        int results[3] = {0};
        fork_and_collect(3, results);
        CHECK(all_unique(results, 3),
              "fork_and_collect(3): все exit codes уникальны");
    }

    /* Тест 6: уникальность результатов при n=5 */
    {
        int results[5] = {0};
        fork_and_collect(5, results);
        CHECK(all_unique(results, 5),
              "fork_and_collect(5): все exit codes уникальны");
    }

    printf("\n=== run_echo ===\n");

    /* Тест 7: echo "hello" завершается с кодом 0 */
    {
        int rc = run_echo("hello");
        CHECK(rc == 0, "run_echo(\"hello\"): exit code == 0");
    }

    /* Тест 8: echo "" — пустая строка */
    {
        int rc = run_echo("");
        CHECK(rc == 0, "run_echo(\"\"): exit code == 0");
    }

    /* Тест 9: echo с длинным аргументом */
    {
        int rc = run_echo("long_argument_string_12345");
        CHECK(rc == 0, "run_echo(длинный аргумент): exit code == 0");
    }

    printf("\n=== check_signal_death ===\n");

    /* Тест 10: SIGTERM (15) */
    {
        int sig = check_signal_death(SIGTERM);
        CHECK(sig == SIGTERM,
              "check_signal_death(SIGTERM): возвращает SIGTERM (15)");
    }

    /* Тест 11: SIGKILL (9) */
    {
        int sig = check_signal_death(SIGKILL);
        CHECK(sig == SIGKILL,
              "check_signal_death(SIGKILL): возвращает SIGKILL (9)");
    }

    /* Тест 12: SIGUSR1 */
    {
        int sig = check_signal_death(SIGUSR1);
        CHECK(sig == SIGUSR1,
              "check_signal_death(SIGUSR1): возвращает SIGUSR1");
    }

    /* Тест 13: процесс убитый сигналом — WIFSIGNALED должен быть true.
     * Проверяем косвенно: check_signal_death должна вернуть > 0 */
    {
        int sig = check_signal_death(SIGABRT);
        CHECK(sig == SIGABRT,
              "check_signal_death(SIGABRT): WIFSIGNALED + WTERMSIG корректны");
    }

    /* Тест 14: нормальное завершение не путается с сигналом.
     * fork_and_collect должна вернуть 0 (не сигнал) для нормально завершившихся потомков */
    {
        int results[1] = {0};
        int rc = fork_and_collect(1, results);
        CHECK(rc == 0 && results[0] == 1,
              "нормальное завершение: WIFEXITED+WEXITSTATUS корректны (не WIFSIGNALED)");
    }

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
