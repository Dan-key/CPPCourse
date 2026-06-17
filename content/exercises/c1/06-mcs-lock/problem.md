# Задание: MCS lock — масштабируемый справедливый спинлок

Реализуй MCS-лок (Mellor-Crummey & Scott). В отличие от TTAS-спинлока (упражнение
02), где все ждущие крутятся на **одной** общей переменной и инвалидируют её
линию друг у друга (cache-line bouncing + несправедливость), MCS выстраивает
ждущих в **очередь узлов**, и каждый крутится на **своём** локальном флаге.
Результат — почти линейная масштабируемость и строгий FIFO-порядок.

## Что реализовать

В `solution.cpp` — методы `lock`/`unlock` класса `McsLock`. Узел `self` —
персональный для потока (`thread_local`, уже подключён в обёртках):

```cpp
void McsLock::lock(Node* self) {
    self->next.store(nullptr, std::memory_order_relaxed);
    Node* pred = tail_.exchange(self, std::memory_order_acq_rel);  // встал в хвост
    if (pred) {                                  // очередь была не пуста
        self->locked.store(true, std::memory_order_relaxed);
        pred->next.store(self, std::memory_order_release);         // «разбуди меня»
        while (self->locked.load(std::memory_order_acquire))       // кручусь на СВОЁМ флаге
            cpu_pause();
    }
}

void McsLock::unlock(Node* self) {
    Node* succ = self->next.load(std::memory_order_acquire);
    if (!succ) {                                 // вроде я последний
        Node* expected = self;
        if (tail_.compare_exchange_strong(expected, nullptr,
                std::memory_order_release))
            return;                              // действительно последний — всё
        while (!(succ = self->next.load(std::memory_order_acquire)))
            cpu_pause();                         // ждём, пока преемник допишет next
    }
    succ->locked.store(false, std::memory_order_release);          // бужу преемника
}
```

## Почему это масштабируется

- `tail_.exchange(self)` атомарно ставит меня в конец очереди и отдаёт
  предшественника. Дальше я прошу его (`pred->next = self`) разбудить меня и кручусь
  на **локальном** `self->locked` — эта линия кэша приватная, её никто не дёргает,
  пока предшественник не отпустит лок. Нет общего spin-адреса → нет bouncing.
- Освобождающий будит **ровно одного** преемника, меняя **его** флаг (FIFO,
  без thundering herd).
- Тонкое место — «я последний»: `CAS(tail_, self → nullptr)`. Если он провалился,
  значит уже встал новый ждущий, но он мог ещё не дописать `pred->next`; поэтому
  крутимся, пока `self->next` не появится.

## C-эквивалент и связь с ядром

На C11 — то же самое через `_Atomic` и `atomic_*_explicit`. Ядро Linux использует
**qspinlock** — гибрид: быстрый путь это простой TAS, а под контеншном
включается MCS-очередь. CLH-lock — близкий родственник (крутятся на флаге
предшественника).

## Проверка

Серверный автопрогон (ASan/UBSan): `THREADS` потоков по `ITERS` инкрементов общей
переменной под локом. Итог обязан быть ровно `THREADS*ITERS`, и тест следит, что
внутри секции никогда не более одного потока. Зависание (неверное ожидание
преемника) поймается таймаутом.

Гонки/ordering — TSan:

```sh
g++ -std=c++20 -fsanitize=thread -O1 -g -pthread solution.cpp test.cpp -o prog && ./prog
```
