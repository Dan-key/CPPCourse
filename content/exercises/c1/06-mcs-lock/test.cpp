#include <cstdio>
#include <thread>
#include <vector>

struct McsLock;
extern "C" {
    McsLock* mcs_create();
    void     mcs_destroy(McsLock* m);
    void     mcs_lock(McsLock* m);
    void     mcs_unlock(McsLock* m);
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
    std::printf("=== 06-mcs-lock ===\n");

    McsLock* lock = mcs_create();
    CHECK(lock != nullptr, "mcs_create() != nullptr");

    // Базовая семантика в одном потоке.
    mcs_lock(lock);
    mcs_unlock(lock);
    CHECK(true, "lock/unlock в одном потоке проходит без зависания");

    long shared  = 0;
    int  in_cs   = 0;
    bool overlap = false;

    std::vector<std::thread> th;
    for (int t = 0; t < THREADS; ++t) {
        th.emplace_back([&] {
            for (int i = 0; i < ITERS; ++i) {
                mcs_lock(lock);
                if (++in_cs != 1) overlap = true;   // одновременно >1 в секции?
                ++shared;
                --in_cs;
                mcs_unlock(lock);
            }
        });
    }
    for (auto& t : th) t.join();

    CHECK(!overlap, "взаимное исключение: внутри секции всегда ровно один поток");
    CHECK(shared == (long)THREADS * ITERS,
          "ни один инкремент не потерян: shared == THREADS*ITERS");

    mcs_destroy(lock);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
