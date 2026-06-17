#include <cstdio>
#include <thread>
#include <vector>

struct Spinlock;
extern "C" {
    Spinlock* spin_create();
    void      spin_destroy(Spinlock* s);
    void      spin_lock(Spinlock* s);
    void      spin_unlock(Spinlock* s);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static constexpr int THREADS = 8;
static constexpr int ITERS   = 100000;

int main() {
    std::printf("=== 02-ttas-spinlock ===\n");

    Spinlock* lock = spin_create();
    CHECK(lock != nullptr, "spin_create() != nullptr");

    // Базовая семантика: lock/unlock в одном потоке не должны зависать.
    spin_lock(lock);
    spin_unlock(lock);
    CHECK(true, "lock/unlock в одном потоке проходит без зависания");

    long shared  = 0;       // ОБЫЧНАЯ переменная, защищена только локом
    int  in_cs   = 0;       // счётчик «внутри критической секции»
    bool overlap = false;   // зафиксировано нарушение взаимного исключения

    std::vector<std::thread> th;
    for (int t = 0; t < THREADS; ++t) {
        th.emplace_back([&] {
            for (int i = 0; i < ITERS; ++i) {
                spin_lock(lock);
                if (++in_cs != 1) overlap = true;   // одновременно >1 потока в секции
                ++shared;                            // критическая секция
                --in_cs;
                spin_unlock(lock);
            }
        });
    }
    for (auto& t : th) t.join();

    CHECK(!overlap, "взаимное исключение: внутри секции никогда не было >1 потока");
    CHECK(shared == static_cast<long>(THREADS) * ITERS,
          "ни один инкремент не потерян: shared == THREADS*ITERS");

    spin_destroy(lock);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
