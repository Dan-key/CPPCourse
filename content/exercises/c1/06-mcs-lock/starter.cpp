#include <atomic>

// MCS lock (Mellor-Crummey & Scott) — масштабируемый справедливый спинлок.
// Ждущие выстраиваются в очередь узлов и крутятся каждый на СВОЁМ флаге —
// нет cache-line bouncing на общем адресе (в отличие от TTAS, §16). FIFO.
//
// Узел очереди здесь — thread_local (один на поток; этого хватает, пока поток
// держит не более одного MCS-лока одновременно — как в тесте).
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

class McsLock {
public:
    struct Node {
        std::atomic<Node*> next{nullptr};
        std::atomic<bool>  locked{false};
    };

    // Захват. self — персональный узел потока (thread_local).
    //   pred = tail.exchange(self, acq_rel);    // встаю в хвост, получаю предшественника
    //   if (pred) { self->locked = true; pred->next = self (release);
    //               пока self->locked (acquire) — cpu_pause(); }
    void lock(Node* self) {
        (void)self;
        // TODO
    }

    // Освобождение. Разбудить преемника (или снять tail, если я последний).
    //   succ = self->next (acquire)
    //   if (!succ) { если CAS(tail, self -> nullptr) — я был последним, выходим;
    //                иначе ждём, пока преемник допишет self->next }
    //   succ->locked = false (release)
    void unlock(Node* self) {
        (void)self;
        // TODO
    }

private:
    std::atomic<Node*> tail_{nullptr};
};

// Один узел на поток.
static thread_local McsLock::Node g_mcs_node;

// C-совместимые обёртки для теста (узел берётся из thread_local).
extern "C" {
    McsLock* mcs_create()              { return new McsLock(); }
    void     mcs_destroy(McsLock* m)   { delete m; }
    void     mcs_lock(McsLock* m)      { m->lock(&g_mcs_node); }
    void     mcs_unlock(McsLock* m)    { m->unlock(&g_mcs_node); }
}
