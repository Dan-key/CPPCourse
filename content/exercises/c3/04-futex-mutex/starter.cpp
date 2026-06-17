#include <atomic>
#include <climits>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

// Мьютекс на futex — «fast userspace mutex». Без контеншна это просто атомарный
// CAS в userspace (БЕЗ системного вызова); только при контеншне зовётся
// futex(2), чтобы усыпить/разбудить поток в ядре. Это машина состояний 0/1/2 из
// C1 §8.2 — теперь по-настоящему. (Тот же примитив лежит под std::mutex и под
// межпроцессной синхронизацией в разделяемой памяти.)
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//
// Состояния слова мьютекса:
//   0 = свободен; 1 = занят, ждущих нет; 2 = занят, есть ждущие (надо будить).

// Обёртка сисколла (glibc-обёртки нет). FUTEX_*_PRIVATE — быстрее для одного процесса.
static int futex(std::atomic<int>* a, int op, int val) {
    return (int)syscall(SYS_futex, reinterpret_cast<int*>(a), op, val,
                        (const struct timespec*)nullptr, (int*)nullptr, 0);
}

// Захват. Алгоритм Дреппера:
//   c = CAS(m, 0 -> 1, acquire); если *m было 0 — успех, выходим.
//   иначе (занят): если c != 2 -> c = exchange(m, 2, acquire);
//   пока c != 0: futex(m, FUTEX_WAIT_PRIVATE, 2);  c = exchange(m, 2, acquire);
void futex_lock(std::atomic<int>* m) {
    (void)m;
    // TODO
}

// Освобождение:
//   если fetch_sub(m, 1, release) != 1 (значит было 2 — есть ждущие):
//       store(m, 0, release);  futex(m, FUTEX_WAKE_PRIVATE, 1);
void futex_unlock(std::atomic<int>* m) {
    (void)m;
    // TODO
}
