#include <cstdio>
#include <cstddef>
#include <thread>

struct Counters;
extern "C" {
    Counters*   counters_create();
    void        counters_destroy(Counters* c);
    void        counters_inc(Counters* c, int i);
    long        counters_get(Counters* c, int i);
    std::size_t counters_gap(Counters* c);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static constexpr long ITERS = 2000000;

int main() {
    std::printf("=== 04-false-sharing ===\n");

    Counters* c = counters_create();
    CHECK(c != nullptr, "counters_create() != nullptr");

    // Главная проверка урока: счётчики на РАЗНЫХ линиях кэша.
    std::size_t gap = counters_gap(c);
    std::printf("  (gap между счётчиками = %zu байт)\n", gap);
    CHECK(gap >= 64, "счётчики разнесены минимум на 64 байта (нет false sharing)");

    // Функциональная корректность: два потока бьют по своему счётчику.
    std::thread t0([&] { for (long i = 0; i < ITERS; ++i) counters_inc(c, 0); });
    std::thread t1([&] { for (long i = 0; i < ITERS; ++i) counters_inc(c, 1); });
    t0.join();
    t1.join();

    CHECK(counters_get(c, 0) == ITERS, "счётчик 0 == ITERS (без потерь)");
    CHECK(counters_get(c, 1) == ITERS, "счётчик 1 == ITERS (без потерь)");

    counters_destroy(c);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    std::printf("Замер эффекта: сравни время этой версии (alignas 64) с версией\n");
    std::printf("без выравнивания — см. problem.md, раздел «Замер».\n");
    return (g_pass == g_run) ? 0 : 1;
}
