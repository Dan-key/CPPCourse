#include <sys/signalfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

// signalfd превращает доставку сигналов в читаемые события — их можно
// обрабатывать СИНХРОННО в event-loop (без async-signal-safety, C2 §7.2).
// Здесь ты пишешь корректный «дренаж» signalfd и заодно увидишь фундаментальную
// семантику Unix: СТАНДАРТНЫЕ сигналы при блокировке СХЛОПЫВАЮТСЯ (3 одинаковых =
// 1 ожидающий), а сигналы реального времени (SIGRTMIN..SIGRTMAX) — ОЧЕРЕДЯТСЯ.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog

// Вычитать ВСЕ доступные записи из неблокирующего signalfd sfd, посчитав сигналы
// по номеру: counts[signo]++ (counts длиной counts_len, индекс = ssi_signo).
// Вернуть общее число прочитанных записей (>=0), -1 при настоящей ошибке.
//
// Каждая запись — это struct signalfd_siginfo (фикс. размер); читаем по одной
// (или пачкой) до EAGAIN. ssi_signo — номер сигнала. EINTR — повторить.
int sig_drain(int sfd, int* counts, int counts_len) {
    (void)sfd; (void)counts; (void)counts_len;
    return -1; // TODO
}
