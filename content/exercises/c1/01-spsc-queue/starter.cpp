#include <atomic>
#include <cstddef>
#include <new>          // std::hardware_destructive_interference_size (если доступно)

// Lock-free SPSC (single-producer single-consumer) ring buffer.
//
// Ровно ОДИН поток зовёт push, ровно ОДИН — pop. Поэтому CAS не нужен:
// достаточно двух атомарных индексов с acquire/release.
//
// Сборка (как на сервере курса):
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
// Проверка гонок (запусти руками!):
//   g++ -std=c++20 -fsanitize=thread             -O1 -g -pthread solution.cpp test.cpp -o prog && ./prog

#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t CACHELINE = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t CACHELINE = 64;
#endif

class SpscQueue {
public:
    static constexpr std::size_t CAP = 64;          // степень двойки
    static_assert((CAP & (CAP - 1)) == 0, "CAP must be a power of two");

    // Положить значение. true при успехе, false если очередь полна.
    // Ordering:
    //   - свой индекс tail_ читаем relaxed;
    //   - чужой индекс head_ читаем acquire;
    //   - данные пишем ДО публикации; tail_ публикуем release.
    bool push(int v) {
        (void)v;
        return false; // TODO
    }

    // Забрать значение в out. true при успехе, false если очередь пуста.
    // Ordering:
    //   - свой индекс head_ читаем relaxed;
    //   - чужой индекс tail_ читаем acquire;
    //   - данные читаем ДО сдвига head_; head_ публикуем release.
    bool pop(int& out) {
        (void)out;
        return false; // TODO
    }

private:
    int buf_[CAP] = {};
    // head_ пишет только consumer, tail_ — только producer.
    // Разводим по разным линиям кэша против false sharing.
    alignas(CACHELINE) std::atomic<std::size_t> head_{0};
    alignas(CACHELINE) std::atomic<std::size_t> tail_{0};
};

// Тест работает через эти C-совместимые обёртки (чтобы не тащить класс в test.cpp).
extern "C" {
    SpscQueue* spsc_create()                  { return new SpscQueue(); }
    void       spsc_destroy(SpscQueue* q)     { delete q; }
    int        spsc_push(SpscQueue* q, int v) { return q->push(v) ? 1 : 0; }
    int        spsc_pop(SpscQueue* q, int* o) { return q->pop(*o) ? 1 : 0; }
}
