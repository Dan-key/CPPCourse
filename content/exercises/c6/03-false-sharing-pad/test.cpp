#include <cstdio>

struct Counters;
extern "C" Counters* counters_create(int n);
extern "C" void counters_destroy(Counters*);
extern "C" void counters_inc(Counters*, int i);
extern "C" long counters_get(Counters*, int i);
extern "C" int  counters_elem_size();
extern "C" int  counters_on_distinct_lines(Counters*, int n);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 03-false-sharing-pad ===\n");

    int sz = counters_elem_size();
    CHECK(sz >= 64, "размер padded-счётчика >= 64 (кэш-линия)");
    CHECK(sz % 64 == 0, "размер кратен 64 (выравнивание массива)");

    Counters* c = counters_create(8);

    // Функциональная независимость: инкрементируем по-разному, читаем верно.
    for (int k = 0; k < 3; ++k) counters_inc(c, 0);
    for (int k = 0; k < 5; ++k) counters_inc(c, 3);
    counters_inc(c, 7);
    CHECK(counters_get(c, 0) == 3, "счётчик 0 == 3");
    CHECK(counters_get(c, 3) == 5, "счётчик 3 == 5");
    CHECK(counters_get(c, 7) == 1, "счётчик 7 == 1");
    CHECK(counters_get(c, 1) == 0, "нетронутый счётчик 1 == 0 (нет перекрёстного эффекта)");

    // Структурная проверка: никакие два счётчика не делят кэш-линию.
    CHECK(counters_on_distinct_lines(c, 8) == 1, "все 8 счётчиков на разных 64B-линиях");

    counters_destroy(c);

    // Маленький массив тоже корректен.
    Counters* c2 = counters_create(2);
    counters_inc(c2, 0); counters_inc(c2, 1); counters_inc(c2, 1);
    CHECK(counters_get(c2, 0) == 1 && counters_get(c2, 1) == 2, "n=2: счётчики независимы");
    CHECK(counters_on_distinct_lines(c2, 2) == 1, "n=2: разные линии");
    counters_destroy(c2);

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
