#include <atomic>
#include <cstddef>

// False sharing: два независимых счётчика, которые пишут РАЗНЫЕ потоки.
// Если они лежат в одной 64-байтной линии кэша, линия пинг-понгует между
// ядрами (cache line bouncing) и масштабируемость падает в разы — при том,
// что данные логически независимы.
//
// ЗАДАЧА: развести два счётчика по РАЗНЫМ линиям кэша.
// Сейчас Cell не выровнен → c_[0] и c_[1] попадают в одну линию.
// Добавь выравнивание/padding, чтобы каждый Cell занимал отдельную линию
// (gap() обязан вернуть >= 64).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog

class Counters {
public:
    // Атомарно увеличить счётчик idx (0 или 1). relaxed: важен лишь итог.
    void inc(int idx) {
        c_[idx].v.fetch_add(1, std::memory_order_relaxed);
    }

    long get(int idx) const {
        return c_[idx].v.load(std::memory_order_relaxed);
    }

    // Расстояние в байтах между счётчиком 0 и счётчиком 1.
    // Должно быть >= 64, чтобы они гарантированно лежали в разных линиях кэша.
    std::size_t gap() const {
        return static_cast<std::size_t>(
            reinterpret_cast<const char*>(&c_[1]) -
            reinterpret_cast<const char*>(&c_[0]));
    }

private:
    struct Cell {
        std::atomic<long> v{0};
        // TODO: добавь выравнивание (alignas) или padding, чтобы sizeof(Cell)
        //       был не меньше линии кэша (64 байта) и Cell начинался с её границы.
    };
    Cell c_[2];
};

// C-совместимые обёртки для теста.
extern "C" {
    Counters*   counters_create()                 { return new Counters(); }
    void        counters_destroy(Counters* c)     { delete c; }
    void        counters_inc(Counters* c, int i)  { c->inc(i); }
    long        counters_get(Counters* c, int i)  { return c->get(i); }
    std::size_t counters_gap(Counters* c)         { return c->gap(); }
}
