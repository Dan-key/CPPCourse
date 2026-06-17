# Задание: seqlock — один писатель, много читателей

Seqlock даёт читателям **согласованный снимок** данных без их блокировки:
читатели никогда не тормозят писателя, а вместо этого **перечитывают** данные,
если во время чтения случилась запись. Идеально для «часто читаем, редко пишем»
(системное время, статистика, конфиг).

Защищаемые данные здесь — пара `(x, y)` с инвариантом **`x == y` всегда**.
Писатель ставит `x = y = v`; читатель обязан видеть либо старую пару, либо
новую, но **никогда** `x != y`.

## Что реализовать

В `solution.cpp` дописать `write` и `read` класса `Seqlock`.

**Писатель** (один поток) обрамляет запись нечётным/чётным счётчиком:

```cpp
void Seqlock::write(long v) {
    unsigned s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);            // → НЕЧЁТНЫЙ: «идёт запись»
    std::atomic_thread_fence(std::memory_order_release);
    x_.store(v, std::memory_order_relaxed);
    y_.store(v, std::memory_order_relaxed);
    seq_.store(s + 2, std::memory_order_release);            // → ЧЁТНЫЙ: «готово»
}
```

**Читатель** (много потоков) повторяет, пока счётчик не совпал и чётный:

```cpp
void Seqlock::read(long& a, long& b) const {
    for (;;) {
        unsigned s1 = seq_.load(std::memory_order_acquire);
        if (s1 & 1u) continue;                               // писатель в процессе
        a = x_.load(std::memory_order_relaxed);
        b = y_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        unsigned s2 = seq_.load(std::memory_order_relaxed);
        if (s1 == s2) return;                                // снимок целостный
    }                                                        // иначе — была запись, повтор
}
```

## Почему это работает

- **Нечётный счётчик** = «писатель внутри»: читатель, увидев нечётное, сразу
  уходит на повтор и не берёт заведомо рваные данные.
- **s1 == s2** на выходе означает, что между двумя чтениями счётчика записи не
  было → `a` и `b` относятся к одной версии (`x == y`).
- `release` у писателя на финальном `seq_` и `acquire` у читателя строят
  happens-before: данные, записанные **до** финального bump'а, видны читателю,
  увидевшему новое значение счётчика.
- Данные — `std::atomic<long>` с `relaxed`: seqlock сам по себе допускает
  «гонку» на данных, поэтому корректно делать это атомиками (иначе формально
  UB и TSan ругается).

## C-эквивалент (реальность ядра)

Seqlock — **родной примитив ядра Linux** (`seqlock_t`, `read_seqbegin` /
`read_seqretry`, `write_seqlock`). Так защищён, например, `jiffies` и монотонные
часы. На C11 это те же `atomic_*_explicit` с теми же fence'ами. Разберём в K2.

## Проверка

Серверный автопрогон (ASan/UBSan): один писатель делает `WRITES` записей
`v = 0..WRITES-1` (всегда `x = y = v`), четыре читателя крутятся параллельно и
на **каждом** успешном чтении проверяют `a == b`. Любой рваный снимок → провал.
Также проверяется, что финальное значение дошло.

Гонки/ordering проверяй TSan:

```sh
g++ -std=c++20 -fsanitize=thread -O1 -g -pthread solution.cpp test.cpp -o prog && ./prog
```
