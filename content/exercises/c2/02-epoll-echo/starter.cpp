#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

// Level-triggered epoll эхо-сервер.
// listen_fd — уже слушающий неблокирующий сокет. stop_fd — eventfd: как только
// он становится читаемым, сервер должен корректно вернуть 0.
// Сервер принимает соединения, читает данные и возвращает их назад (эхо).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//
// Скелет:
//   ep = epoll_create1; добавить listen_fd и stop_fd на EPOLLIN.
//   loop: epoll_wait
//     - событие на stop_fd  → return 0 (можно закрыть ep)
//     - событие на listen_fd→ accept4(... SOCK_NONBLOCK|SOCK_CLOEXEC) В ЦИКЛЕ до EAGAIN,
//                             каждый новый conn добавить в epoll на EPOLLIN
//     - событие на conn     → read; r>0: записать те же байты назад (write_all);
//                             r==0/ошибка: EPOLL_CTL_DEL + close(conn)
// Вернуть 0 при штатной остановке, -1 при ошибке epoll_wait.
int echo_server(int listen_fd, int stop_fd) {
    (void)listen_fd; (void)stop_fd;
    return -1; // TODO
}
