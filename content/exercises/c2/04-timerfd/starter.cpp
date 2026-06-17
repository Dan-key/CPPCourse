#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <ctime>

// timerfd внутри epoll-loop. Таймеры в Linux — это тоже дескрипторы: настроил
// период, fd становится «читаемым» на каждую сработку, чтение отдаёт uint64 —
// число сработок с прошлого чтения.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog

// Создать периодический timerfd с интервалом interval_ms, добавить его в epoll
// и ждать через epoll_wait, СУММИРУЯ число сработок (uint64 из read), пока сумма
// не достигнет n. Вернуть итоговое число учтённых сработок (== n при успехе),
// -1 при ошибке любого сисколла.
//
// Подсказки:
//   int tf = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
//   itimerspec its{};  its.it_value = its.it_interval = {interval_ms в sec+nsec};
//   timerfd_settime(tf, 0, &its, nullptr);
//   epoll_ctl(ADD, tf, EPOLLIN);
//   цикл: epoll_wait → на EPOLLIN: read(tf, &exp, 8) → total += exp;
//   не забудь close(tf), close(ep).
long wait_n_ticks(int interval_ms, int n) {
    (void)interval_ms; (void)n;
    return -1; // TODO
}
