#include <atomic>
#include <cstddef>

// Lock-free SPSC очередь в РАЗДЕЛЯЕМОЙ ПАМЯТИ между процессами.
// Это синтез C1 (SPSC + memory ordering) и C3 (shared memory): структура лежит в
// mmap(MAP_SHARED), один процесс-производитель и один процесс-потребитель
// общаются БЕЗ системных вызовов на каждый обмен — только атомарные индексы.
// Работает между процессами, потому что атомик lock-free (на x86-64 size_t),
// а память физически общая.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog

// ВАЖНО: структура должна быть идентична в solution.cpp и test.cpp (общего
// заголовка нет — прогонщик компилирует solution из временного каталога).
// Никаких указателей внутри — только индексы и массив (память общая между
// процессами с разными адресными пространствами).
struct SpscRing {
    static constexpr std::size_t CAP = 1024;        // степень двойки
    std::atomic<std::size_t> head;                  // двигает только потребитель
    std::atomic<std::size_t> tail;                  // двигает только производитель
    int buf[CAP];
};

// Положить v. 1 при успехе, 0 если очередь полна.
// Ordering как в C1 §11.1: свой индекс (tail) — relaxed, чужой (head) — acquire,
// данные пишем ДО публикации, tail публикуем release.
int spsc_push(SpscRing* q, int v) {
    (void)q; (void)v;
    return 0; // TODO
}

// Забрать в *out. 1 при успехе, 0 если пусто.
// Свой индекс (head) — relaxed, чужой (tail) — acquire, читаем данные ДО сдвига
// head, head публикуем release.
int spsc_pop(SpscRing* q, int* out) {
    (void)q; (void)out;
    return 0; // TODO
}
