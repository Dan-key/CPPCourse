#include <cstdio>
#include <thread>

// Опаковый тип — раскрыт в solution.cpp; обёртки с C-связыванием.
struct SpscQueue;
extern "C" {
    SpscQueue* spsc_create();
    void       spsc_destroy(SpscQueue* q);
    int        spsc_push(SpscQueue* q, int v);
    int        spsc_pop(SpscQueue* q, int* out);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static constexpr int N = 200000;   // много больше ёмкости → wrap и переполнения

int main() {
    std::printf("=== 01-spsc-queue ===\n");

    // --- Однопоточная семантика ---
    {
        SpscQueue* q = spsc_create();
        CHECK(q != nullptr, "spsc_create() != nullptr");

        int out = -1;
        CHECK(spsc_pop(q, &out) == 0, "pop из пустой очереди возвращает 0");
        CHECK(spsc_push(q, 11) == 1,  "push в пустую возвращает 1");
        CHECK(spsc_push(q, 22) == 1,  "push второго возвращает 1");
        CHECK(spsc_pop(q, &out) == 1 && out == 11, "FIFO: 11 первым");
        CHECK(spsc_pop(q, &out) == 1 && out == 22, "FIFO: 22 вторым");
        CHECK(spsc_pop(q, &out) == 0, "после опустошения pop возвращает 0");

        int pushed = 0;
        while (spsc_push(q, pushed) == 1) {
            ++pushed;
            if (pushed > 100000) break;   // защита от бесконечного цикла при баге
        }
        CHECK(pushed > 0 && pushed <= 100000, "ёмкость конечна (push в полную = 0)");
        spsc_destroy(q);
    }

    // --- Конкурентный прогон: producer + consumer, проверка FIFO без потерь ---
    {
        SpscQueue* q = spsc_create();

        std::thread producer([q] {
            for (int i = 0; i < N; ++i)
                while (spsc_push(q, i) == 0)   // busy-retry, если полна
                    ;
        });

        bool ok_order = true;
        long expect = 0;
        int out;
        for (long got = 0; got < N; ) {
            if (spsc_pop(q, &out)) {
                if (out != static_cast<int>(expect)) ok_order = false; // потеря/дубль/реордер
                ++expect;
                ++got;
            }
        }
        producer.join();

        int leftover;
        CHECK(spsc_pop(q, &leftover) == 0, "после прогона очередь пуста (ничего не зависло)");
        CHECK(ok_order, "consumer получил 0..N-1 строго по порядку (FIFO, без потерь и дублей)");
        CHECK(expect == N, "получено ровно N элементов");
        spsc_destroy(q);
    }

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
