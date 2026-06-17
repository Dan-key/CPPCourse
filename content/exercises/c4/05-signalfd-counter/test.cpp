#include <cstdio>
#include <csignal>
#include <sys/signalfd.h>
#include <unistd.h>

int sig_drain(int sfd, int* counts, int counts_len);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 05-signalfd-counter ===\n");

    const int RT = SIGRTMIN;          // первый сигнал реального времени

    // Блокируем сигналы (обязательно для signalfd) и заводим signalfd.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, RT);
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    int sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    CHECK(sfd >= 0, "signalfd создан");

    // СТАНДАРТНЫЙ сигнал, посланный 3 раза при блокировке → схлопывается в 1.
    raise(SIGUSR1); raise(SIGUSR1); raise(SIGUSR1);
    // другой стандартный — 1.
    raise(SIGUSR2);
    // Сигнал РЕАЛЬНОГО ВРЕМЕНИ, посланный 3 раза → ОЧЕРЕДЬ из 3.
    raise(RT); raise(RT); raise(RT);

    int counts[256] = {0};
    int total = sig_drain(sfd, counts, 256);

    CHECK(total == 5, "всего прочитано 5 записей (1 + 1 + 3), а не 7");
    CHECK(counts[SIGUSR1] == 1, "СТАНДАРТНЫЙ SIGUSR1 ×3 СХЛОПНУЛСЯ в 1 (coalescing)");
    CHECK(counts[SIGUSR2] == 1, "SIGUSR2 ×1 → 1");
    CHECK(counts[RT] == 3,      "сигнал РЕАЛЬНОГО ВРЕМЕНИ ×3 ОЧЕРЕДИЛСЯ → 3 (queueing)");

    // Повторный дренаж на пустом → 0 (корректно выходим по EAGAIN, не зависаем).
    int c2[256] = {0};
    int again = sig_drain(sfd, c2, 256);
    CHECK(again == 0, "повторный дренаж пустого signalfd → 0 (вышли по EAGAIN)");

    close(sfd);
    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
