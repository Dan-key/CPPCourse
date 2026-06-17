# Задание: epoll эхо-сервер (level-triggered)

Собери полноценный неблокирующий эхо-сервер на `epoll` в **level-triggered**
режиме (по умолчанию). Это базовая форма любого сетевого сервиса на Linux.

## Что реализовать

```cpp
int echo_server(int listen_fd, int stop_fd);
```

- `listen_fd` — уже слушающий **неблокирующий** TCP-сокет.
- `stop_fd` — `eventfd`; как только он становится читаемым, сервер штатно
  возвращает `0`.
- Сервер принимает соединения, читает данные и пишет их обратно (эхо).

## Скелет

```cpp
int ep = epoll_create1(EPOLL_CLOEXEC);
auto add = [&](int fd, uint32_t ev){ epoll_event e{}; e.events=ev; e.data.fd=fd;
                                     epoll_ctl(ep, EPOLL_CTL_ADD, fd, &e); };
add(listen_fd, EPOLLIN);
add(stop_fd,   EPOLLIN);

epoll_event evs[64];
for (;;) {
    int n = epoll_wait(ep, evs, 64, -1);
    if (n < 0) { if (errno == EINTR) continue; close(ep); return -1; }
    for (int i = 0; i < n; ++i) {
        int fd = evs[i].data.fd;
        if (fd == stop_fd) { close(ep); return 0; }       // команда «стоп»
        else if (fd == listen_fd) {
            for (;;) {                                     // принять ВСЕ соединения
                int c = accept4(listen_fd, nullptr, nullptr, SOCK_NONBLOCK|SOCK_CLOEXEC);
                if (c < 0) break;                          // EAGAIN — приняли всех
                add(c, EPOLLIN);
            }
        } else {                                           // данные от клиента
            char buf[4096];
            ssize_t r = read(fd, buf, sizeof buf);
            if (r > 0) { /* записать те же r байт назад (учти частичный write) */ }
            else { epoll_ctl(ep, EPOLL_CTL_DEL, fd, nullptr); close(fd); }  // EOF/ошибка
        }
    }
}
```

## Ключевые моменты

- **`accept` в цикле до `EAGAIN`.** Одно событие на listen-сокете может означать
  несколько ожидающих соединений.
- **Эхо-запись** должна записать все `r` байт (частичный `write` — обработать;
  для небольших сообщений теста хватает простого цикла).
- В **LT-режиме** читать можно по одному `read` на событие: если осталось — epoll
  снова сообщит. (В ET так нельзя — см. упражнение 03.)
- **Снимай fd с epoll (`DEL`) перед `close`** при EOF/ошибке.

## C-эквивалент

Все вызовы (`epoll_*`, `accept4`, `read`, `write`) — сисколлы, одинаковые в C.
В ядре обратная сторона — `poll_wait`/wait queues драйвера (модуль K3).

## Проверка

Автопрогон (ASan/UBSan): 20 клиентов на loopback по очереди шлют сообщение и
обязаны получить точное эхо; затем тест «пинает» `stop_fd`, и `echo_server`
должен вернуть `0`. Поиграй вживую: `scripts/io-playground.py flood --connect
127.0.0.1:<порт> --conns 2000` под `strace -c`.
