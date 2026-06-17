#include <atomic>

// TTAS (Test-and-Test-And-Set) спинлок на std::atomic.
//
// Идея: пока замок занят, крутимся на ДЕШЁВОМ relaxed-чтении (линия кэша в
// состоянии Shared, не пинг-понгует). Увидев «свободно», пробуем дорогой
// exchange с acquire. Освобождение — store(release).
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//   g++ -std=c++20 -fsanitize=thread             -O1 -g -pthread solution.cpp test.cpp -o prog

static inline void cpu_pause() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}

class Spinlock {
public:
    // Захватить замок (блокирующе, занятым ожиданием). Паттерн TTAS:
    //   for (;;) {
    //       while (locked_.load(relaxed)) cpu_pause();        // дешёвое ожидание
    //       if (!locked_.exchange(true, acquire)) return;     // успели занять
    //   }
    void lock() {
        // TODO
    }

    // Освободить замок: store(false, release).
    void unlock() {
        // TODO
    }

private:
    std::atomic<bool> locked_{false};
};

// C-совместимые обёртки для теста.
extern "C" {
    Spinlock* spin_create()             { return new Spinlock(); }
    void      spin_destroy(Spinlock* s) { delete s; }
    void      spin_lock(Spinlock* s)    { s->lock(); }
    void      spin_unlock(Spinlock* s)  { s->unlock(); }
}
