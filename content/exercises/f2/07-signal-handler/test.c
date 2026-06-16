#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>

void shutdown_handler(int sig);
void counter_handler(int sig);
int  setup_handlers(void);
int  get_shutdown(void);
int  get_counter(void);
void reset_flags(void);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

/* Заглушка: если обработчики не установлены, сигналы убьют процесс.
   Устанавливаем SIG_IGN на SIGTERM/SIGINT/SIGUSR1 перед вызовом setup_handlers,
   чтобы тест не падал от дефолтного обработчика. */
static void safety_ignore(void) {
    signal(SIGTERM, SIG_IGN);
    signal(SIGINT,  SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
}

int main(void) {
    /* Защита: игнорировать сигналы до установки обработчиков */
    safety_ignore();

    /* ---- 1: setup_handlers возвращает 0 ---- */
    printf("=== Установка обработчиков ===\n");
    int r = setup_handlers();
    CHECK(r == 0, "setup_handlers() == 0");

    /* ---- 2: начальное состояние ---- */
    printf("=== Начальное состояние ===\n");
    CHECK(get_shutdown() == 0, "g_shutdown изначально 0");
    CHECK(get_counter() == 0,  "g_counter изначально 0");

    /* ---- 3: SIGTERM устанавливает g_shutdown ---- */
    printf("=== SIGTERM ===\n");
    raise(SIGTERM);
    CHECK(get_shutdown() == 1, "после SIGTERM: get_shutdown() == 1");

    /* ---- 4: сброс + SIGINT тоже устанавливает g_shutdown ---- */
    printf("=== SIGINT ===\n");
    reset_flags();
    CHECK(get_shutdown() == 0, "после reset_flags: g_shutdown == 0");
    raise(SIGINT);
    CHECK(get_shutdown() == 1, "после SIGINT: get_shutdown() == 1");

    /* ---- 5-6: SIGUSR1 увеличивает счётчик ---- */
    printf("=== SIGUSR1 ===\n");
    reset_flags();
    CHECK(get_counter() == 0, "до SIGUSR1: g_counter == 0");
    raise(SIGUSR1);
    CHECK(get_counter() == 1, "после 1x SIGUSR1: g_counter == 1");

    /* ---- 7: несколько SIGUSR1 ---- */
    printf("=== Несколько SIGUSR1 ===\n");
    raise(SIGUSR1);
    raise(SIGUSR1);
    raise(SIGUSR1);
    CHECK(get_counter() == 4, "после 4x SIGUSR1: g_counter == 4");

    /* ---- 8: одновременно shutdown + counter ---- */
    printf("=== Оба флага ===\n");
    reset_flags();
    raise(SIGUSR1);
    raise(SIGTERM);
    CHECK(get_shutdown() == 1 && get_counter() == 1,
          "оба флага установлены независимо");

    /* ---- 9: reset работает ---- */
    printf("=== reset_flags ===\n");
    reset_flags();
    CHECK(get_shutdown() == 0 && get_counter() == 0,
          "после reset_flags оба флага 0");

    /* ---- 10: обработчики установлены через sigaction ---- */
    printf("=== Проверка через sigaction ===\n");
    {
        struct sigaction sa;
        int ret = sigaction(SIGTERM, NULL, &sa);
        CHECK(ret == 0, "sigaction(SIGTERM, NULL, &sa) успешен");
        CHECK(sa.sa_handler == shutdown_handler,
              "обработчик SIGTERM — shutdown_handler");
    }

    /* ---- 11: SA_RESTART установлен ---- */
    printf("=== SA_RESTART ===\n");
    {
        struct sigaction sa;
        sigaction(SIGTERM, NULL, &sa);
        CHECK((sa.sa_flags & SA_RESTART) != 0,
              "SA_RESTART установлен для SIGTERM");
        sigaction(SIGUSR1, NULL, &sa);
        CHECK((sa.sa_flags & SA_RESTART) != 0,
              "SA_RESTART установлен для SIGUSR1");
    }

    /* ---- 12: kill(getpid(), ...) эквивалентен raise() ---- */
    printf("=== kill(getpid()) ===\n");
    reset_flags();
    kill(getpid(), SIGTERM);
    CHECK(get_shutdown() == 1, "kill(getpid(), SIGTERM) устанавливает g_shutdown");
    reset_flags();
    kill(getpid(), SIGUSR1);
    CHECK(get_counter() == 1, "kill(getpid(), SIGUSR1) увеличивает счётчик");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
