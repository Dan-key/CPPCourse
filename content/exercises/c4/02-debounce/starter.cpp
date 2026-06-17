#include <cstdint>

// Debounce — «успокоитель» потока событий. Классическая задача: файл конфигурации
// меняют пачкой записей (редактор сохраняет, git checkout трогает 100 файлов),
// а перечитать конфиг надо ОДИН раз, КОГДА всё утихло. Debouncer копит события и
// «выстреливает» один раз, если в течение quiet_ms не было новых.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//
// В реальном loop: на каждое событие перевзводишь timerfd на now+quiet_ms; когда
// он сработал (новых событий не было quiet_ms) — выполняешь отложенное действие.

class Debouncer {
public:
    explicit Debouncer(uint64_t quiet_ms) : quiet_(quiet_ms) {}

    // Зафиксировать событие в момент now_ms. Сдвигает дедлайн «тишины» вперёд.
    void event(uint64_t now_ms) {
        (void)now_ms;
        // TODO: запомнить время события, отметить, что есть несработавшее
    }

    // Проверить, пора ли «выстрелить» к моменту now_ms.
    // Вернуть true РОВНО ОДИН раз, когда с последнего события прошло >= quiet_ms
    // и есть несработавшее ожидание; в *fire_time записать дедлайн (время сработки).
    // После срабатывания до следующего event() должен возвращать false.
    bool poll(uint64_t now_ms, uint64_t* fire_time) {
        (void)now_ms; (void)fire_time;
        return false; // TODO
    }

    // Когда планируется ближайшая сработка (для вооружения timerfd).
    // false, если нет несработавшего ожидания.
    bool next_fire(uint64_t* out) const {
        (void)out;
        return false; // TODO
    }

private:
    uint64_t quiet_;
    uint64_t last_event_ = 0;
    bool     pending_    = false;
};

extern "C" {
    Debouncer* db_create(uint64_t quiet_ms)              { return new Debouncer(quiet_ms); }
    void db_destroy(Debouncer* d)                        { delete d; }
    void db_event(Debouncer* d, uint64_t now)            { d->event(now); }
    int  db_poll(Debouncer* d, uint64_t now, uint64_t* ft){ return d->poll(now, ft) ? 1 : 0; }
    int  db_next(Debouncer* d, uint64_t* out)            { return d->next_fire(out) ? 1 : 0; }
}
