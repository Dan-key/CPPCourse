#include <cstdio>
#include <chrono>

long wait_n_ticks(int interval_ms, int n);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 04-timerfd ===\n");

    using clock = std::chrono::steady_clock;

    // 4 сработки по 10 мс ≈ 40 мс.
    auto t0 = clock::now();
    long got = wait_n_ticks(10, 4);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();

    CHECK(got == 4, "wait_n_ticks(10ms, 4) учёл ровно 4 сработки");
    CHECK(ms >= 30, "прошло не меньше ~30 мс (таймер реально тикал, а не вернулся мгновенно)");
    std::printf("  (фактически прошло %lld мс)\n", (long long)ms);

    // Одиночная сработка.
    long one = wait_n_ticks(15, 1);
    CHECK(one == 1, "wait_n_ticks(15ms, 1) == 1");

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
