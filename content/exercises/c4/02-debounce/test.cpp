#include <cstdio>
#include <cstdint>

struct Debouncer;
extern "C" {
    Debouncer* db_create(uint64_t quiet_ms);
    void db_destroy(Debouncer* d);
    void db_event(Debouncer* d, uint64_t now);
    int  db_poll(Debouncer* d, uint64_t now, uint64_t* ft);
    int  db_next(Debouncer* d, uint64_t* out);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    ++g_run; \
    if (cond) { ++g_pass; std::printf("  [OK]   %s\n", msg); } \
    else      {           std::printf("  [FAIL] %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== 02-debounce ===\n");

    Debouncer* d = db_create(50);   // тишина 50 мс
    uint64_t ft = 0, nf = 0;

    // Нет событий — ничего не срабатывает.
    CHECK(db_poll(d, 1000, &ft) == 0, "без событий poll → false");
    CHECK(db_next(d, &nf) == 0, "без событий next_fire → false");

    // Одно событие в t=0 → сработать должно в t>=50.
    db_event(d, 0);
    CHECK(db_next(d, &nf) == 1 && nf == 50, "после event(0) ближайшая сработка == 50");
    CHECK(db_poll(d, 40, &ft) == 0, "в t=40 ещё рано (тишина не прошла)");
    CHECK(db_poll(d, 50, &ft) == 1 && ft == 50, "в t=50 срабатывает ровно один раз (fire_time=50)");
    CHECK(db_poll(d, 60, &ft) == 0, "повторный poll после сработки → false (нет новых событий)");

    // Пачка событий 100,110,120 — должна СХЛОПНУТЬСЯ в одну сработку в 170.
    db_event(d, 100);
    db_event(d, 110);
    db_event(d, 120);
    CHECK(db_next(d, &nf) == 1 && nf == 170, "пачка событий: ближайшая сработка == 170 (120+50)");
    CHECK(db_poll(d, 160, &ft) == 0, "в t=160 ещё рано (последнее событие 120)");
    CHECK(db_poll(d, 170, &ft) == 1 && ft == 170, "пачка схлопнулась в ОДНУ сработку в 170");
    CHECK(db_poll(d, 250, &ft) == 0, "после сработки тишина → false");

    // Новое событие после сработки начинает новый цикл.
    db_event(d, 300);
    CHECK(db_poll(d, 350, &ft) == 1 && ft == 350, "новый цикл: событие(300) → сработка в 350");

    db_destroy(d);
    std::printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
