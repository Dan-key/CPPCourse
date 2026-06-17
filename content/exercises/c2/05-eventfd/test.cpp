#include <cstdio>
#include <thread>
#include <chrono>
#include <cstdint>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>

int      ev_post(int efd, uint64_t v);
uint64_t ev_wait(int epfd, int efd);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 05-eventfd ===\n");

    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    int ep  = epoll_create1(EPOLL_CLOEXEC);
    epoll_event ev{}; ev.events = EPOLLIN; ev.data.fd = efd;
    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev);

    CHECK(efd >= 0 && ep >= 0, "eventfd и epoll созданы");

    // Продюсер: «пинает» loop N раз по 1 (с паузами, чтобы пробудить несколько раз).
    constexpr int N = 1000;
    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            ev_post(efd, 1);
            if ((i % 100) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Консьюмер (этот поток) копит, пока не наберёт N.
    uint64_t total = 0;
    int wakeups = 0;
    while (total < (uint64_t)N) {
        total += ev_wait(ep, efd);
        ++wakeups;
        if (wakeups > N + 5) break;   // защита от зависания при баге
    }
    producer.join();

    CHECK(total == (uint64_t)N, "консьюмер забрал ровно N уведомлений (счётчик накапливается верно)");
    CHECK(wakeups <= N, "за одно пробуждение забиралось накопленное (пробуждений не больше числа post)");
    std::printf("  (пробуждений: %d на %d post)\n", wakeups, N);

    close(efd); close(ep);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
