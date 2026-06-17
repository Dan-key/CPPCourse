#include <atomic>

// Treiber stack — простейший lock-free стек на CAS.
//
// ВНИМАНИЕ к reclamation: безопасно освобождать узлы в lock-free стеке трудно
// (ABA + use-after-free, см. лекцию §15.2). В этом упражнении мы СОЗНАТЕЛЬНО
// уходим от проблемы: снятые узлы не освобождаются сразу, а «паркуются» в
// retire-список по ОТДЕЛЬНОМУ полю rnext (основной указатель next при этом
// никогда не переиспользуется), и всё освобождается разом в деструкторе —
// уже однопоточно. Так нет ни ABA, ни утечек. Твоя задача — корректный
// ORDERING в push/pop.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//   g++ -std=c++20 -fsanitize=thread             -O1 -g -pthread solution.cpp test.cpp -o prog

class TreiberStack {
public:
    // Положить значение. Выдели узел (new Node{v, nullptr, nullptr}), затем
    // CAS публикует его вместе с полями → release; при провале n->next уже
    // перечитан актуальным head_, повторяем цикл.
    void push(int v) {
        (void)v;
        // TODO
    }

    // Снять вершину в out. true при успехе, false если стек пуст.
    // head_ читаем acquire (видеть поля узла); CAS двигает head_ на old->next;
    // снятый узел отправь в retire(old).
    bool pop(int& out) {
        (void)out;
        return false; // TODO
    }

    ~TreiberStack() {                          // освобождение — однопоточно
        for (Node* n = head_.load();    n; ) { Node* nx = n->next;  delete n; n = nx; }
        for (Node* n = retired_.load(); n; ) { Node* nx = n->rnext; delete n; n = nx; }
    }

private:
    struct Node { int value; Node* next; Node* rnext; };
    std::atomic<Node*> head_{nullptr};
    std::atomic<Node*> retired_{nullptr};      // снятые узлы; их next НЕ трогаем

    // Готовая «парковка» снятого узла (CAS-push по rnext). Не переписывай.
    void retire(Node* n) {
        n->rnext = retired_.load(std::memory_order_relaxed);
        while (!retired_.compare_exchange_weak(
                   n->rnext, n,
                   std::memory_order_release,
                   std::memory_order_relaxed))
            ;
    }
};

// C-совместимые обёртки для теста.
extern "C" {
    TreiberStack* ts_create()                   { return new TreiberStack(); }
    void          ts_destroy(TreiberStack* s)   { delete s; }
    void          ts_push(TreiberStack* s, int v){ s->push(v); }
    int           ts_pop(TreiberStack* s, int* o){ return s->pop(*o) ? 1 : 0; }
}
