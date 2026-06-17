#include <cstdio>

struct Drain;
extern "C" {
    Drain* drain_create();
    void   drain_destroy(Drain*);
    int    drain_admit(Drain*);
    void   drain_finish(Drain*);
    void   drain_begin_shutdown(Drain*);
    void   drain_timeout(Drain*);
    int    drain_state(Drain*);
    int    drain_active(Drain*);
}

enum { RUNNING = 0, DRAINING = 1, STOPPED = 2 };

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 04-drain-tracker ===\n");

    // Сценарий 1: штатный drain — доделать 3 in-flight, потом STOPPED.
    {
        Drain* d = drain_create();
        CHECK(drain_state(d) == RUNNING, "старт RUNNING");
        CHECK(drain_admit(d) == 1 && drain_admit(d) == 1 && drain_admit(d) == 1,
              "приняли 3 запроса");
        CHECK(drain_active(d) == 3, "active == 3");
        drain_begin_shutdown(d);
        CHECK(drain_state(d) == DRAINING, "begin_shutdown → DRAINING");
        CHECK(drain_admit(d) == 0, "в DRAINING новые запросы отклоняются");
        CHECK(drain_active(d) == 3, "отклонённый запрос не увеличил active");
        drain_finish(d);
        CHECK(drain_state(d) == DRAINING && drain_active(d) == 2, "1 доделан → ещё DRAINING");
        drain_finish(d);
        CHECK(drain_state(d) == DRAINING && drain_active(d) == 1, "2 доделано → ещё DRAINING");
        drain_finish(d);
        CHECK(drain_state(d) == STOPPED && drain_active(d) == 0, "последний доделан → STOPPED");
        CHECK(drain_admit(d) == 0, "после STOPPED admit отклоняет");
        drain_destroy(d);
    }

    // Сценарий 2: пустой drain — нечего сливать → сразу STOPPED.
    {
        Drain* d = drain_create();
        drain_begin_shutdown(d);
        CHECK(drain_state(d) == STOPPED, "begin_shutdown при active==0 → сразу STOPPED");
        drain_destroy(d);
    }

    // Сценарий 3: дедлайн форсирует выход при зависших in-flight.
    {
        Drain* d = drain_create();
        drain_admit(d); drain_admit(d);
        drain_begin_shutdown(d);
        CHECK(drain_state(d) == DRAINING && drain_active(d) == 2, "DRAINING с 2 in-flight");
        drain_timeout(d);
        CHECK(drain_state(d) == STOPPED, "TIMEOUT → форс STOPPED, не ждём зависших");
        drain_destroy(d);
    }

    // Сценарий 4: finish без admit не уводит active в минус.
    {
        Drain* d = drain_create();
        drain_finish(d);
        CHECK(drain_active(d) == 0, "finish при active==0 не уводит в минус");
        CHECK(drain_state(d) == RUNNING, "состояние не изменилось");
        drain_destroy(d);
    }

    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
