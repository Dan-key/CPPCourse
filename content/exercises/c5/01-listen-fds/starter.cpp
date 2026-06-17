// socket activation: разбор переданных дескрипторов (аналог sd_listen_fds).
//
// systemd открывает слушающий сокет и передаёт его сервису по наследству,
// начиная с дескриптора 3 (SD_LISTEN_FDS_START). Сервис узнаёт детали из
// переменных окружения LISTEN_PID / LISTEN_FDS / LISTEN_FDNAMES.
//
// Значения переменных приходят строками-аргументами (nullptr = не задано).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g solution.cpp test.cpp -o prog

constexpr int SD_LISTEN_FDS_START = 3;

// Сколько дескрипторов передано ИМЕННО нам (0, если не для нас или мусор).
//   1) listen_pid пуст → 0
//   2) число(listen_pid) != my_pid → 0
//   3) listen_fds пуст / нечисло / < 0 → 0
//   4) иначе вернуть N; дескрипторы это 3 .. 3+N-1
extern "C" int my_listen_fds(int my_pid, const char* listen_pid, const char* listen_fds) {
    (void)my_pid; (void)listen_pid; (void)listen_fds;
    return -1; // TODO
}

// Номер дескриптора по имени сокета (3 + индекс), или -1 если имени нет.
//   N = my_listen_fds(...); если 0 или listen_fdnames == nullptr → -1
//   разбить listen_fdnames по ':' (максимум N имён); найти name → вернуть 3+i
extern "C" int my_listen_fd_by_name(int my_pid, const char* listen_pid,
                                    const char* listen_fds, const char* listen_fdnames,
                                    const char* name) {
    (void)my_pid; (void)listen_pid; (void)listen_fds; (void)listen_fdnames; (void)name;
    return -1; // TODO
}
