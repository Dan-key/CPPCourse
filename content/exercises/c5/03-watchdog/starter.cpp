// Watchdog-пейсер: разбор WATCHDOG_USEC/WATCHDOG_PID и логика «пора слать WATCHDOG=1».
//
// systemd перезапускает сервис, не приславший WATCHDOG=1 в срок WATCHDOG_USEC.
// Канонический интервал отправки — половина периода (запас на джиттер).
// Время инъектируется (now_usec) — логика детерминирована и тестируема.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g solution.cpp test.cpp -o prog

struct Wd {
    bool               enabled  = false;
    unsigned long long interval = 0;     // WATCHDOG_USEC / 2
    unsigned long long last     = 0;     // время последнего пинга
    bool               pinged   = false; // был ли уже хоть один пинг
    // TODO: при желании добавь поля
};

// enabled ⇔ usec>0 И (watchdog_pid пуст ИЛИ число(watchdog_pid)==my_pid).
// interval = usec/2.
extern "C" Wd* wd_create(int my_pid, const char* watchdog_usec, const char* watchdog_pid) {
    (void)my_pid; (void)watchdog_usec; (void)watchdog_pid;
    return new Wd();   // TODO: распарсить окружение и заполнить поля
}

extern "C" void wd_destroy(Wd* w) { delete w; }

extern "C" int wd_enabled(Wd* w) {
    (void)w;
    return -1; // TODO
}

extern "C" unsigned long long wd_interval_usec(Wd* w) {
    (void)w;
    return 0; // TODO
}

// 0/1: пора ли слать WATCHDOG=1.
//   выключен → 0; первый вызов → 1; иначе now-last>=interval → 1 (обновить last).
extern "C" int wd_should_ping(Wd* w, unsigned long long now_usec) {
    (void)w; (void)now_usec;
    return -1; // TODO
}
