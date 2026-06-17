#include <cstdio>
#include <semaphore.h>
#include <cstddef>
#include <thread>

// Идентичное определение структуры (общего заголовка нет).
struct SemQueue {
    static constexpr std::size_t CAP = 16;
    int    buf[CAP];
    sem_t  empty;
    sem_t  full;
    std::size_t head;
    std::size_t tail;
};

int  sq_init(SemQueue* q);
void sq_put(SemQueue* q, int v);
int  sq_get(SemQueue* q);
void sq_destroy(SemQueue* q);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

static constexpr int N = 200000;

int main() {
    std::printf("=== 06-posix-sem ===\n");

    SemQueue q;
    CHECK(sq_init(&q) == 0, "sq_init успешен");

    // Производитель шлёт 0..N-1; ёмкость 16 заставляет реально блокироваться на
    // empty/full — тест проверяет, что семафоры корректно гейтят поток.
    std::thread producer([&] {
        for (int i = 0; i < N; ++i) sq_put(&q, i);
    });

    bool order_ok = true;
    long sum = 0;
    for (int i = 0; i < N; ++i) {
        int v = sq_get(&q);
        if (v != i) order_ok = false;
        sum += v;
    }
    producer.join();

    long expected_sum = (long)N * (N - 1) / 2;
    CHECK(order_ok, "потребитель получил 0..N-1 строго по порядку (FIFO через bounded buffer)");
    CHECK(sum == expected_sum, "сумма всех значений верна (ничего не потеряно/задвоено)");

    sq_destroy(&q);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
