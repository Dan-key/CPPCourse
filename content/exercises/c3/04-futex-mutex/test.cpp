#include <cstdio>
#include <atomic>
#include <thread>
#include <vector>

void futex_lock(std::atomic<int>* m);
void futex_unlock(std::atomic<int>* m);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static constexpr int THREADS = 8;
static constexpr int ITERS   = 100000;

int main() {
    std::printf("=== 04-futex-mutex ===\n");

    std::atomic<int> m{0};       // 0 = свободен

    // Базовая семантика в одном потоке.
    futex_lock(&m);
    futex_unlock(&m);
    CHECK(true, "lock/unlock в одном потоке без зависания");

    long shared  = 0;
    int  in_cs   = 0;
    bool overlap = false;

    std::vector<std::thread> th;
    for (int t = 0; t < THREADS; ++t) {
        th.emplace_back([&] {
            for (int i = 0; i < ITERS; ++i) {
                futex_lock(&m);
                if (++in_cs != 1) overlap = true;   // одновременно >1 в секции?
                ++shared;
                --in_cs;
                futex_unlock(&m);
            }
        });
    }
    for (auto& t : th) t.join();

    CHECK(!overlap, "взаимное исключение: внутри секции всегда ровно один поток");
    CHECK(shared == (long)THREADS * ITERS,
          "ни один инкремент не потерян: shared == THREADS*ITERS");
    CHECK(m.load() == 0, "после всех unlock мьютекс свободен (== 0)");

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
