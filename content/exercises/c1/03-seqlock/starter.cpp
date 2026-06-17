#include <atomic>

// Seqlock — синхронизация «один писатель, много читателей» без блокировки
// читателей. Читатели НИКОГДА не блокируют писателя; вместо этого они
// перечитывают данные, если во время чтения была запись.
//
// Инвариант защищаемых данных в этом упражнении: x == y ВСЕГДА.
// Писатель ставит x = y = v атомарно «снаружи»; читатель обязан видеть
// согласованный снимок (никогда x != y).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//   g++ -std=c++20 -fsanitize=thread             -O1 -g -pthread solution.cpp test.cpp -o prog

class Seqlock {
public:
    // ПИСАТЕЛЬ (только один поток). Записать пару (v, v).
    // Протокол:
    //   seq_++  -> становится НЕЧЁТНЫМ (сигнал «идёт запись»)
    //   release-fence
    //   x_ = v; y_ = v;            (relaxed)
    //   seq_++ (release) -> ЧЁТНЫМ (запись завершена)
    void write(long v) {
        (void)v;
        // TODO
    }

    // ЧИТАТЕЛЬ (много потоков). Прочитать согласованный снимок в a, b.
    // Протокол:
    //   do {
    //     s1 = seq_.load(acquire);
    //     if (s1 нечётное) continue;     // писатель в процессе — ждём
    //     a = x_(relaxed); b = y_(relaxed);
    //     acquire-fence;
    //     s2 = seq_.load(relaxed);
    //   } while (s1 != s2);              // во время чтения была запись — повтор
    void read(long& a, long& b) const {
        a = 0; b = 0;
        // TODO
    }

private:
    std::atomic<unsigned> seq_{0};
    std::atomic<long>     x_{0};
    std::atomic<long>     y_{0};
};

// C-совместимые обёртки для теста.
extern "C" {
    Seqlock* seq_create()                          { return new Seqlock(); }
    void     seq_destroy(Seqlock* s)               { delete s; }
    void     seq_write(Seqlock* s, long v)         { s->write(v); }
    void     seq_read(Seqlock* s, long* a, long* b) { s->read(*a, *b); }
}
