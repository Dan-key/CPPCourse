#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstdint>
#include <cerrno>

// eventfd — 64-битный счётчик-в-ядре с дескриптором: запись увеличивает,
// чтение возвращает накопленное и обнуляет. Главное применение — РАЗБУДИТЬ
// event loop из другого потока (мост к C1: внешний поток кладёт задачу в
// очередь и «пинает» loop через eventfd).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog

// «Пнуть» loop: добавить v к счётчику eventfd (одна запись 8 байт). 0/-1.
int ev_post(int efd, uint64_t v) {
    (void)efd; (void)v;
    return -1; // TODO
}

// Подождать уведомления и забрать накопленное.
// Блокироваться на epoll_wait(epfd) до готовности efd, затем прочитать счётчик
// (8 байт) — вернуть накопленное значение (сумма всех ev_post с прошлого чтения).
// efd создан с EFD_NONBLOCK; epfd уже наблюдает за efd (EPOLLIN).
// EINTR на epoll_wait — повторить. При ошибке вернуть 0.
uint64_t ev_wait(int epfd, int efd) {
    (void)epfd; (void)efd;
    return 0; // TODO
}
