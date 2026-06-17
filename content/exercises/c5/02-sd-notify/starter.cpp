// Протокол sd_notify (Type=notify): построение тела сообщений сервиса для systemd.
//
// Сообщение — это датаграмма в $NOTIFY_SOCKET, тело — строки KEY=VALUE через '\n'.
// Здесь реализуется построение тела по канону sd_notify(3).
//
// Все функции пишут текст в buf (с завершающим '\0') и возвращают длину строки
// (без '\0'), или -1, если buf слишком мал (НЕ выходи за границу — это ловит ASan).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g solution.cpp test.cpp -o prog

// "READY=1\n" и, если status задан и непуст, ещё "STATUS=<status>\n".
extern "C" int notify_ready(char* buf, int cap, const char* status) {
    (void)buf; (void)cap; (void)status;
    return -1; // TODO
}

// "RELOADING=1\nMONOTONIC_USEC=<mono_usec>\n"
extern "C" int notify_reloading(char* buf, int cap, unsigned long long mono_usec) {
    (void)buf; (void)cap; (void)mono_usec;
    return -1; // TODO
}

// "STOPPING=1\n"
extern "C" int notify_stopping(char* buf, int cap) {
    (void)buf; (void)cap;
    return -1; // TODO
}

// "WATCHDOG=1\n"
extern "C" int notify_watchdog(char* buf, int cap) {
    (void)buf; (void)cap;
    return -1; // TODO
}
