#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>

struct TreiberStack;
extern "C" {
    TreiberStack* ts_create();
    void          ts_destroy(TreiberStack* s);
    void          ts_push(TreiberStack* s, int v);
    int           ts_pop(TreiberStack* s, int* o);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static constexpr int PRODUCERS = 4;
static constexpr int K         = 50000;   // каждый producer кладёт значения 0..K-1

int main() {
    std::printf("=== 05-treiber-stack ===\n");

    // --- Однопоточно: LIFO-порядок ---
    {
        TreiberStack* s = ts_create();
        int out = -1;
        CHECK(ts_pop(s, &out) == 0, "pop из пустого стека возвращает 0");
        ts_push(s, 1); ts_push(s, 2); ts_push(s, 3);
        CHECK(ts_pop(s, &out) == 1 && out == 3, "LIFO: первым снимается 3");
        CHECK(ts_pop(s, &out) == 1 && out == 2, "LIFO: затем 2");
        CHECK(ts_pop(s, &out) == 1 && out == 1, "LIFO: затем 1");
        CHECK(ts_pop(s, &out) == 0, "после опустошения pop возвращает 0");
        ts_destroy(s);
    }

    // --- Конкурентно: PRODUCERS кладут 0..K-1, потребители снимают всё ---
    {
        TreiberStack* s = ts_create();

        std::vector<std::thread> producers;
        for (int p = 0; p < PRODUCERS; ++p)
            producers.emplace_back([s] { for (int i = 0; i < K; ++i) ts_push(s, i); });

        const long expected_count = (long)PRODUCERS * K;
        const long expected_sum   = (long)PRODUCERS * ((long)K * (K - 1) / 2);

        std::atomic<long> popped_count{0};
        std::atomic<long> popped_sum{0};
        std::atomic<bool> producers_done{false};

        std::vector<std::thread> consumers;
        for (int c = 0; c < 3; ++c) {
            consumers.emplace_back([&] {
                long cnt = 0, sum = 0;
                int v;
                for (;;) {
                    if (ts_pop(s, &v)) { ++cnt; sum += v; }
                    // После join'а продюсеров пустой стек уже не наполнится → выходим.
                    else if (producers_done.load(std::memory_order_acquire)) break;
                }
                popped_count.fetch_add(cnt);
                popped_sum.fetch_add(sum);
            });
        }

        for (auto& t : producers) t.join();
        producers_done.store(true, std::memory_order_release);
        for (auto& t : consumers) t.join();

        CHECK(popped_count.load() == expected_count,
              "снято ровно PRODUCERS*K элементов (ничего не потеряно и не задвоено)");
        CHECK(popped_sum.load() == expected_sum,
              "сумма снятых значений совпадает с суммой положенных");

        int leftover;
        CHECK(ts_pop(s, &leftover) == 0, "стек пуст после прогона");
        ts_destroy(s);
    }

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
