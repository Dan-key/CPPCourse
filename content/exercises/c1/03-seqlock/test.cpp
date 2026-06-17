#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>

struct Seqlock;
extern "C" {
    Seqlock* seq_create();
    void     seq_destroy(Seqlock* s);
    void     seq_write(Seqlock* s, long v);
    void     seq_read(Seqlock* s, long* a, long* b);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static constexpr long WRITES  = 300000;
static constexpr int  READERS = 4;

int main() {
    std::printf("=== 03-seqlock ===\n");

    Seqlock* s = seq_create();
    CHECK(s != nullptr, "seq_create() != nullptr");

    // Однопоточная семантика.
    seq_write(s, 7);
    {
        long a = -1, b = -1;
        seq_read(s, &a, &b);
        CHECK(a == 7 && b == 7, "read после write(7) возвращает согласованный (7,7)");
    }

    std::atomic<bool> done{false};
    std::atomic<bool> torn{false};         // зафиксирован несогласованный снимок a != b
    std::atomic<long> total_reads{0};

    std::thread writer([&] {
        for (long v = 0; v < WRITES; ++v)
            seq_write(s, v);
        done.store(true, std::memory_order_release);
    });

    std::vector<std::thread> readers;
    for (int r = 0; r < READERS; ++r) {
        readers.emplace_back([&] {
            long local = 0;
            while (!done.load(std::memory_order_acquire)) {
                long a, b;
                seq_read(s, &a, &b);
                if (a != b) torn.store(true, std::memory_order_relaxed); // нарушен инвариант
                ++local;
            }
            total_reads.fetch_add(local, std::memory_order_relaxed);
        });
    }

    writer.join();
    for (auto& t : readers) t.join();

    // Финальное значение должно дойти.
    {
        long a = -1, b = -1;
        seq_read(s, &a, &b);
        CHECK(a == WRITES - 1 && b == WRITES - 1, "финальный снимок == (WRITES-1, WRITES-1)");
    }

    CHECK(!torn.load(), "читатели НИКОГДА не видели рваный снимок (всегда x == y)");
    CHECK(total_reads.load() > 0, "читатели реально выполняли чтения (>0)");

    seq_destroy(s);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
