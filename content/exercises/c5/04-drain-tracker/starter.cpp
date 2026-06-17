// Graceful drain: учёт in-flight запросов, чтобы не потерять их при остановке.
//
// Состояния: 0=RUNNING (принимаем), 1=DRAINING (доделываем активные, новые не
// принимаем), 2=STOPPED (активных не осталось / форс по дедлайну; терминальное).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g solution.cpp test.cpp -o prog

class Drain {
public:
    // Начать запрос: 1 принят (active++), 0 отклонён (DRAINING/STOPPED).
    int admit() {
        // TODO
        return -1;
    }
    // Запрос завершился: active--; если DRAINING и active==0 → STOPPED.
    void finish() {
        // TODO
    }
    // RUNNING → DRAINING; если active==0 (нечего сливать) → сразу STOPPED.
    void begin_shutdown() {
        // TODO
    }
    // Дедлайн истёк → форс STOPPED.
    void timeout() {
        // TODO
    }
    int state()  const { return state_; }
    int active() const { return active_; }

private:
    int state_  = 0;   // RUNNING
    int active_ = 0;   // in-flight
};

extern "C" {
    Drain* drain_create()                 { return new Drain(); }
    void   drain_destroy(Drain* d)        { delete d; }
    int    drain_admit(Drain* d)          { return d->admit(); }
    void   drain_finish(Drain* d)         { d->finish(); }
    void   drain_begin_shutdown(Drain* d) { d->begin_shutdown(); }
    void   drain_timeout(Drain* d)        { d->timeout(); }
    int    drain_state(Drain* d)          { return d->state(); }
    int    drain_active(Drain* d)         { return d->active(); }
}
