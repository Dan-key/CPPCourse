#include <semaphore.h>
#include <cstddef>

// Ограниченная очередь «производитель-потребитель» на POSIX-семафорах —
// каноничный паттерн синхронизации. Два считающих семафора: empty (сколько
// свободных слотов) и full (сколько занятых). Для одного producer + одного
// consumer мьютекс не нужен: семафоры сами упорядочивают доступ.
//
//   g++ -std=c++20 -fsanitize=address,undefined -O1 -g -pthread solution.cpp test.cpp -o prog
//
// ВАЖНО: структура идентична в solution.cpp и test.cpp (общего заголовка нет).
struct SemQueue {
    static constexpr std::size_t CAP = 16;
    int    buf[CAP];
    sem_t  empty;          // изначально CAP: столько свободных слотов
    sem_t  full;           // изначально 0:   столько занятых слотов
    std::size_t head;      // двигает потребитель
    std::size_t tail;      // двигает производитель
};

// Инициализировать: empty=CAP, full=0, head=tail=0. Вернуть 0/-1.
//   sem_init(&q->empty, 0, CAP);  // pshared=0 (между потоками); =1 для shm/процессов
//   sem_init(&q->full,  0, 0);
int sq_init(SemQueue* q) {
    (void)q;
    return -1; // TODO
}

// Положить v: дождаться свободного слота, записать, отметить занятый.
//   sem_wait(&q->empty);  q->buf[q->tail] = v;  q->tail = (q->tail+1) % CAP;  sem_post(&q->full);
void sq_put(SemQueue* q, int v) {
    (void)q; (void)v;
    // TODO
}

// Забрать: дождаться занятого слота, прочитать, освободить слот. Вернуть значение.
//   sem_wait(&q->full);  int v = q->buf[q->head];  q->head = (q->head+1) % CAP;  sem_post(&q->empty);  return v;
int sq_get(SemQueue* q) {
    (void)q;
    return -1; // TODO
}

// Освободить семафоры.
void sq_destroy(SemQueue* q) {
    (void)q;
    // TODO
}
