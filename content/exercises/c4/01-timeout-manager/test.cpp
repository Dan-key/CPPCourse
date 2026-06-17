#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>

struct TimeoutManager;
extern "C" {
    TimeoutManager* tm_create();
    void   tm_destroy(TimeoutManager* t);
    void   tm_add(TimeoutManager* t, uint64_t id, uint64_t dl);
    void   tm_cancel(TimeoutManager* t, uint64_t id);
    int    tm_next(TimeoutManager* t, uint64_t* out);
    size_t tm_expire(TimeoutManager* t, uint64_t now, uint64_t* out, size_t max);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 01-timeout-manager ===\n");

    TimeoutManager* t = tm_create();
    uint64_t out[64];

    // Пусто.
    uint64_t d;
    CHECK(tm_next(t, &d) == 0, "next_deadline на пустом → пусто");
    CHECK(tm_expire(t, 1000, out, 64) == 0, "expire на пустом → 0");

    // Добавляем таймеры с разными дедлайнами.
    tm_add(t, 100, 500);
    tm_add(t, 101, 200);
    tm_add(t, 102, 800);
    tm_add(t, 103, 200);   // тот же дедлайн, что 101
    CHECK(tm_next(t, &d) == 1 && d == 200, "ближайший дедлайн == 200");

    // expire(now=200) должен забрать оба с дедлайном 200 (id 101 и 103).
    size_t n = tm_expire(t, 200, out, 64);
    std::vector<uint64_t> got(out, out + n);
    std::sort(got.begin(), got.end());
    CHECK(n == 2 && got == std::vector<uint64_t>({101, 103}),
          "expire(200) забрал ровно таймеры с дедлайном <= 200 (id 101,103)");
    CHECK(tm_next(t, &d) == 1 && d == 500, "следующий ближайший == 500");

    // Отмена.
    tm_cancel(t, 100);                       // отменяем дедлайн 500
    CHECK(tm_next(t, &d) == 1 && d == 800, "после cancel(100) ближайший == 800 (отменённый пропущен)");

    // expire далеко в будущем — должен остаться только 102 (100 отменён).
    n = tm_expire(t, 10000, out, 64);
    CHECK(n == 1 && out[0] == 102, "expire(10000): остался только неотменённый 102");
    CHECK(tm_next(t, &d) == 0, "менеджер снова пуст");

    // Порядок возрастания дедлайна в одном expire.
    tm_add(t, 1, 300);
    tm_add(t, 2, 100);
    tm_add(t, 3, 200);
    n = tm_expire(t, 1000, out, 64);
    CHECK(n == 3 && out[0] == 2 && out[1] == 3 && out[2] == 1,
          "expire возвращает истёкшие в порядке ВОЗРАСТАНИЯ дедлайна (2,3,1)");

    // Стресс: много таймеров, проверка корректности кучи.
    {
        TimeoutManager* s = tm_create();
        const int N = 5000;
        for (int i = 0; i < N; ++i) tm_add(s, (uint64_t)i, (uint64_t)((i * 7919) % 100000));
        // отменим каждый третий
        int cancelled = 0;
        for (int i = 0; i < N; i += 3) { tm_cancel(s, (uint64_t)i); ++cancelled; }
        std::vector<uint64_t> all;
        for (;;) {
            size_t k = tm_expire(s, 1'000'000, out, 64);
            if (k == 0) break;
            for (size_t i = 0; i < k; ++i) all.push_back(out[i]);
        }
        CHECK((int)all.size() == N - cancelled, "стресс: извлечены все неотменённые таймеры, ровно столько");
        tm_destroy(s);
    }

    tm_destroy(t);
    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
