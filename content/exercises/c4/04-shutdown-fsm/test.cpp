#include <cstdio>

struct Daemon;
extern "C" {
    Daemon* dmn_create();
    void dmn_destroy(Daemon* d);
    void dmn_on(Daemon* d, int ev);
    int  dmn_state(Daemon* d);
    int  dmn_reloads(Daemon* d);
}

enum { RUNNING = 0, DRAINING = 1, STOPPING = 2 };
enum { SIGTERM_E = 0, SIGHUP_E = 1, DRAINED_E = 2, TIMEOUT_E = 3 };

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 04-shutdown-fsm ===\n");

    // Сценарий 1: штатная graceful-остановка.
    {
        Daemon* d = dmn_create();
        CHECK(dmn_state(d) == RUNNING, "старт в RUNNING");
        dmn_on(d, SIGHUP_E);
        CHECK(dmn_state(d) == RUNNING && dmn_reloads(d) == 1, "SIGHUP: перезагрузка, остаёмся RUNNING");
        dmn_on(d, SIGTERM_E);
        CHECK(dmn_state(d) == DRAINING, "SIGTERM в RUNNING → DRAINING");
        dmn_on(d, DRAINED_E);
        CHECK(dmn_state(d) == STOPPING, "все соединения слиты → STOPPING");
        dmn_on(d, SIGHUP_E);
        CHECK(dmn_state(d) == STOPPING, "STOPPING терминально (события игнорируются)");
        dmn_destroy(d);
    }

    // Сценарий 2: второй SIGTERM = форс.
    {
        Daemon* d = dmn_create();
        dmn_on(d, SIGTERM_E);
        CHECK(dmn_state(d) == DRAINING, "первый SIGTERM → DRAINING");
        dmn_on(d, SIGTERM_E);
        CHECK(dmn_state(d) == STOPPING, "ВТОРОЙ SIGTERM → STOPPING (форс)");
        dmn_destroy(d);
    }

    // Сценарий 3: дедлайн graceful истёк.
    {
        Daemon* d = dmn_create();
        dmn_on(d, SIGTERM_E);
        dmn_on(d, TIMEOUT_E);
        CHECK(dmn_state(d) == STOPPING, "TIMEOUT в DRAINING → STOPPING (дедлайн)");
        dmn_destroy(d);
    }

    // Сценарий 4: множественные SIGHUP в RUNNING.
    {
        Daemon* d = dmn_create();
        dmn_on(d, SIGHUP_E); dmn_on(d, SIGHUP_E); dmn_on(d, SIGHUP_E);
        CHECK(dmn_state(d) == RUNNING && dmn_reloads(d) == 3, "3×SIGHUP: reloads=3, всё ещё RUNNING");
        dmn_destroy(d);
    }

    // Сценарий 5: «лишние» события в RUNNING игнорируются.
    {
        Daemon* d = dmn_create();
        dmn_on(d, DRAINED_E); dmn_on(d, TIMEOUT_E);
        CHECK(dmn_state(d) == RUNNING, "DRAINED/TIMEOUT в RUNNING игнорируются");
        // SIGHUP во время DRAINING не перезагружает и не меняет состояние.
        dmn_on(d, SIGTERM_E);
        int r = dmn_reloads(d);
        dmn_on(d, SIGHUP_E);
        CHECK(dmn_state(d) == DRAINING && dmn_reloads(d) == r, "SIGHUP в DRAINING игнорируется");
        dmn_destroy(d);
    }

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
