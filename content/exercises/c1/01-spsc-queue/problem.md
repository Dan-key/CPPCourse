# Задание: lock-free SPSC ring buffer на acquire/release

Реализуй очередь без блокировок для схемы **один производитель — один
потребитель** (SPSC). Ровно один поток вызывает `push`, ровно один — `pop`.
Благодаря этому CAS не нужен: хватает двух атомарных индексов с правильным
`std::memory_order`.

## Что реализовать

В `solution.cpp` дописать методы класса `SpscQueue` (поля `buf_`, `head_`,
`tail_` уже объявлены; `head_`/`tail_` разведены `alignas` по разным линиям
кэша против false sharing):

```cpp
bool SpscQueue::push(int v) {
    auto t = tail_.load(std::memory_order_relaxed);   // свой → relaxed
    auto h = head_.load(std::memory_order_acquire);   // чужой → acquire
    if (t - h == CAP) return false;                   // полна
    buf_[t & (CAP - 1)] = v;                          // (A) пишем данные
    tail_.store(t + 1, std::memory_order_release);    // (B) публикуем
    return true;
}
```

`pop` симметричен: свой индекс (`head_`) — relaxed, чужой (`tail_`) — acquire;
данные читаются **до** сдвига `head_`, а `head_` публикуется через
`store(..., release)`.

## Почему именно такой ordering (это и есть «доказательство»)

- Запись данных (A) **sequenced-before** release-store `tail_` (B). Потребитель,
  прочитав `tail_` через **acquire** и увидев новое значение, синхронизируется с
  (B) → запись (A) **happens-before** чтение данных. Поэтому `buf_` —
  **обычный** массив, не атомарный: гонку исключил happens-before.
- Свой индекс читается **relaxed**: у каждого индекса ровно один писатель, а
  когерентность одной переменной гарантирована всегда. `acquire` нужен только на
  **чужом** индексе — забрать чужую публикацию.

## C-эквивалент (реальность ядра)

В ядре/драйверах того же добиваются на C11: `_Atomic size_t head, tail;` и
`atomic_load_explicit/atomic_store_explicit(..., memory_order_acquire/release)`.
Модель памяти C11 и C++11 — **одна и та же**; меняется только синтаксис
(`std::atomic<T>` ↔ `_Atomic T`, методы ↔ функции `atomic_*`). Ровно эта
структура лежит в основе колец SQ/CQ io_uring между userspace и ядром (модуль
C2).

## Проверка

Серверный автопрогон (ASan/UBSan) проверяет **функциональную** корректность:
producer шлёт `0..N-1`, consumer обязан получить их строго по порядку, без
потерь и дублей, очередь не должна «зависнуть».

Гонки ASan **не** ловит — обязательно прогони локально TSan:

```sh
g++ -std=c++20 -fsanitize=thread -O1 -g -pthread solution.cpp test.cpp -o prog && ./prog
```

Если перепутать relaxed/acquire/release, TSan покажет точную пару конфликтующих
доступов.
