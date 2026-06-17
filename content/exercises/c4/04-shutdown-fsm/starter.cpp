// Конечный автомат graceful shutdown демона — реальная управляющая логика
// сервиса, которую обычно кормят события из signalfd (C2/C4) и таймера.
//
// Состояния:  0 = RUNNING (работаем, принимаем запросы)
//             1 = DRAINING (перестали принимать новые, доделываем активные)
//             2 = STOPPING (выходим; терминальное состояние)
//
// События:    0 = SIGTERM   (просьба остановиться)
//             1 = SIGHUP     (перечитать конфиг на лету)
//             2 = DRAINED    (все активные соединения завершились)
//             3 = TIMEOUT    (истёк дедлайн graceful-остановки)
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog

class Daemon {
public:
    // Обработать событие. Реализуй таблицу переходов:
    //   RUNNING  + SIGTERM  -> DRAINING (начать graceful, перестать accept)
    //   RUNNING  + SIGHUP   -> RUNNING, reloads_++ (горячая перезагрузка конфига)
    //   RUNNING  + DRAINED  -> RUNNING (нечего сливать — игнор)
    //   RUNNING  + TIMEOUT  -> RUNNING (игнор)
    //   DRAINING + DRAINED  -> STOPPING (все доделали — выходим штатно)
    //   DRAINING + TIMEOUT  -> STOPPING (дедлайн — выходим принудительно)
    //   DRAINING + SIGTERM  -> STOPPING (ВТОРОЙ SIGTERM = «хватит ждать», форс)
    //   DRAINING + SIGHUP   -> DRAINING (перезагрузка во время остановки — игнор)
    //   STOPPING + *        -> STOPPING (терминальное)
    void on(int event) {
        (void)event;
        // TODO
    }

    int state()   const { return state_; }
    int reloads() const { return reloads_; }

private:
    int state_   = 0;   // RUNNING
    int reloads_ = 0;
};

extern "C" {
    Daemon* dmn_create()              { return new Daemon(); }
    void dmn_destroy(Daemon* d)       { delete d; }
    void dmn_on(Daemon* d, int ev)    { d->on(ev); }
    int  dmn_state(Daemon* d)         { return d->state(); }
    int  dmn_reloads(Daemon* d)       { return d->reloads(); }
}
