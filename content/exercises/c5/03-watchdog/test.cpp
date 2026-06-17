#include <cstdio>

struct Wd;
extern "C" Wd* wd_create(int my_pid, const char* watchdog_usec, const char* watchdog_pid);
extern "C" void wd_destroy(Wd*);
extern "C" int wd_enabled(Wd*);
extern "C" unsigned long long wd_interval_usec(Wd*);
extern "C" int wd_should_ping(Wd*, unsigned long long now_usec);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 03-watchdog ===\n");

    // Включён: WATCHDOG_USEC=10000000 (10 c), наш PID. Интервал = 5 c.
    {
        Wd* w = wd_create(100, "10000000", "100");
        CHECK(wd_enabled(w) == 1, "включён при заданном WATCHDOG_USEC и нашем PID");
        CHECK(wd_interval_usec(w) == 5000000ULL, "интервал = WATCHDOG_USEC/2");

        CHECK(wd_should_ping(w, 0) == 1,        "первый пинг — сразу (t=0)");
        CHECK(wd_should_ping(w, 1000000) == 0,  "через 1 c — рано");
        CHECK(wd_should_ping(w, 4999999) == 0,  "за мкс до интервала — рано");
        CHECK(wd_should_ping(w, 5000000) == 1,  "ровно через интервал — пора");
        CHECK(wd_should_ping(w, 9000000) == 0,  "через 4 c после пинга — рано");
        CHECK(wd_should_ping(w, 10000000) == 1, "ещё интервал прошёл — пора");
        wd_destroy(w);
    }

    // Watchdog не задан → выключен, никогда не пингуем.
    {
        Wd* w = wd_create(100, nullptr, "100");
        CHECK(wd_enabled(w) == 0, "нет WATCHDOG_USEC → выключен");
        CHECK(wd_interval_usec(w) == 0, "интервал 0 при выключенном");
        CHECK(wd_should_ping(w, 0) == 0 && wd_should_ping(w, 1000000000ULL) == 0,
              "выключенный watchdog не пингует");
        wd_destroy(w);
    }

    // WATCHDOG_USEC=0 → выключен.
    {
        Wd* w = wd_create(100, "0", nullptr);
        CHECK(wd_enabled(w) == 0, "WATCHDOG_USEC=0 → выключен");
        wd_destroy(w);
    }

    // Чужой WATCHDOG_PID → не наш watchdog, выключен.
    {
        Wd* w = wd_create(100, "10000000", "999");
        CHECK(wd_enabled(w) == 0, "чужой WATCHDOG_PID → выключен");
        wd_destroy(w);
    }

    // WATCHDOG_PID не задан → считаем, что наш.
    {
        Wd* w = wd_create(100, "2000000", nullptr);
        CHECK(wd_enabled(w) == 1, "нет WATCHDOG_PID → наш");
        CHECK(wd_interval_usec(w) == 1000000ULL, "интервал = 1 c");
        wd_destroy(w);
    }

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
