#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

// Edge-triggered (EPOLLET) epoll эхо-сервер.
// ОТЛИЧИЕ ОТ LT (упражнение 02): событие приходит ТОЛЬКО на «фронт» — когда
// пришли НОВЫЕ данные. Поэтому, получив EPOLLIN, нужно читать В ЦИКЛЕ ДО EAGAIN,
// иначе непрочитанный «хвост» зависнет и НЕ придёт второго события. Аналогично
// accept нужно вызывать до EAGAIN. ET имеет смысл только с НЕблокирующими fd.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//
// Скелет:
//   ep = epoll_create1; listen_fd добавить на EPOLLIN | EPOLLET;
//                       stop_fd — на EPOLLIN (можно без ET).
//   loop: epoll_wait
//     - stop_fd  → return 0
//     - listen_fd→ accept4(...) В ЦИКЛЕ до EAGAIN, новые conn на EPOLLIN | EPOLLET
//     - conn     → ЦИКЛ read до EAGAIN: каждый прочитанный кусок сразу писать назад;
//                  read==0 → EOF (DEL+close); EAGAIN → выйти из дренаж-цикла.
// Вернуть 0 при остановке, -1 при ошибке epoll_wait.
int echo_server_et(int listen_fd, int stop_fd) {
    (void)listen_fd; (void)stop_fd;
    return -1; // TODO
}
