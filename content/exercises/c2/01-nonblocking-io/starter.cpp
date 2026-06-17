#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>
#include <string>

// Базовые кирпичи неблокирующего I/O. Сюда же — корректная обработка четырёх
// исходов read/write: >0 (данные), 0 (EOF), -1+EAGAIN (пусто, не ошибка),
// -1+EINTR (прерван — повторить).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog

// Перевести дескриптор в неблокирующий режим (добавить O_NONBLOCK к флагам).
// Вернуть 0 при успехе, -1 при ошибке (errno установлен).
int set_nonblocking(int fd) {
    (void)fd;
    return -1; // TODO
}

// Записать РОВНО n байт из buf в fd (fd блокирующий).
// Обрабатывать частичную запись (write мог записать меньше) и EINTR (повторить).
// Вернуть n при успехе, -1 при настоящей ошибке.
ssize_t write_all(int fd, const void* buf, size_t n) {
    (void)fd; (void)buf; (void)n;
    return -1; // TODO
}

// Прочитать ВСЁ доступное из НЕблокирующего fd, дописывая в out, пока не EAGAIN
// (или EOF). Вернуть число прочитанных за вызов байт (>= 0), -1 при настоящей
// ошибке. EINTR — повторить; EAGAIN/EWOULDBLOCK — выйти (это не ошибка).
// Это шаблон «дренажа до EAGAIN» для edge-triggered epoll.
ssize_t read_drain(int fd, std::string& out) {
    (void)fd; (void)out;
    return -1; // TODO
}
