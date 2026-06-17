# Задание: `timerfd` в epoll-loop

Таймеры в Linux — тоже дескрипторы. `timerfd` становится «читаемым» на каждую
сработку, а чтение возвращает `uint64` — сколько раз он сработал с прошлого
чтения. Это позволяет держать таймеры в общем epoll-loop без отдельного потока и
без `SIGALRM`.

## Что реализовать

```cpp
long wait_n_ticks(int interval_ms, int n);
```

Создать периодический `timerfd` с интервалом `interval_ms`, ждать его сработок
через `epoll_wait`, суммируя число сработок, пока сумма не достигнет `n`. Вернуть
итог (`== n`), либо `-1` при ошибке сисколла.

## Скелет

```cpp
int tf = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
itimerspec its{};
its.it_value.tv_sec     = interval_ms / 1000;
its.it_value.tv_nsec    = (interval_ms % 1000) * 1'000'000L;
its.it_interval = its.it_value;                 // периодический
timerfd_settime(tf, 0, &its, nullptr);

int ep = epoll_create1(EPOLL_CLOEXEC);
epoll_event ev{}; ev.events = EPOLLIN; ev.data.fd = tf;
epoll_ctl(ep, EPOLL_CTL_ADD, tf, &ev);

long total = 0;
epoll_event evs[4];
while (total < n) {
    int k = epoll_wait(ep, evs, 4, -1);
    if (k < 0) { if (errno == EINTR) continue; /* close, return -1 */ }
    for (int i = 0; i < k; ++i) {
        uint64_t exp = 0;
        if (read(tf, &exp, sizeof exp) == (ssize_t)sizeof exp)
            total += (long)exp;                 // exp может быть > 1, если отстали
    }
}
close(tf); close(ep);
return total;
```

Важная деталь: читать нужно **ровно 8 байт** в `uint64`, и значение может быть
**больше 1** (если loop был занят и таймер сработал несколько раз между чтениями)
— поэтому суммируем `exp`, а не считаем «по событию».

## C-эквивалент

Идентичные сисколлы в C. В демоне это типичный способ делать heartbeat/таймауты
(см. C4/C5).

## Проверка

Автопрогон: `wait_n_ticks(10, 4)` обязан вернуть 4, и пройти должно не меньше
~30 мс (доказательство, что таймер реально тикал, а не вернулся мгновенно).
