#include <cstdio>
#include <cstdint>

struct TokenBucket;
extern "C" {
    TokenBucket* tb_create(double cap, double rate_per_sec);
    void tb_destroy(TokenBucket* b);
    int  tb_allow(TokenBucket* b, uint64_t now);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

// Сколько запросов подряд разрешено в момент now.
static int allowed_burst(TokenBucket* b, uint64_t now) {
    int n = 0;
    while (tb_allow(b, now)) ++n;
    return n;
}

int main() {
    std::printf("=== 03-token-bucket ===\n");

    // Ёмкость 3, скорость 1000 токенов/с = 1 токен/мс (чистая арифметика).
    TokenBucket* b = tb_create(3, 1000);

    // t=0: ведро полное → ровно 3 разрешено, дальше отказ.
    CHECK(allowed_burst(b, 0) == 3, "стартовый всплеск == ёмкости (3 разрешено)");
    CHECK(tb_allow(b, 0) == 0, "4-й запрос в t=0 отклонён (ведро пусто)");

    // t=1 мс: долился 1 токен → ровно 1 разрешён.
    CHECK(allowed_burst(b, 1) == 1, "через 1 мс долился 1 токен → 1 разрешён");
    CHECK(tb_allow(b, 1) == 0, "следующий в тот же момент отклонён");

    // t=2 мс: ещё 1 токен.
    CHECK(allowed_burst(b, 2) == 1, "через ещё 1 мс → ещё 1 разрешён");

    // Долгая пауза: долив ограничен ёмкостью (не накапливается бесконечно).
    CHECK(allowed_burst(b, 100) == 3, "после долгой паузы доступно не больше ёмкости (3, не 98)");
    CHECK(tb_allow(b, 100) == 0, "после исчерпания всплеска — отказ");

    // Средняя скорость: за 10 мс при 1 токен/мс — ~10 запросов (плюс остаток ведра).
    {
        TokenBucket* b2 = tb_create(2, 1000);   // ёмкость 2
        int total = 0;
        for (uint64_t t = 0; t <= 10; ++t)       // по одному запросу каждую мс
            total += tb_allow(b2, t) ? 1 : 0;
        // 2 (старт) + ~10 доливов, но запрашиваем по 1/мс → ограничены скоростью.
        CHECK(total >= 9 && total <= 12, "при 1 запрос/мс и скорости 1/мс пропущено ~10-12 (держит среднюю скорость)");
        tb_destroy(b2);
    }

    tb_destroy(b);
    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
