# Задание: lock-free Treiber stack (CAS + ordering)

Реализуй классический lock-free стек Трайбера на `compare_exchange`. Это первая
структура **на узлах** (а не на массиве), и она обнажает главную боль lock-free —
безопасное освобождение памяти (reclamation, ABA).

## Что реализовать

В `solution.cpp` — методы `push` и `pop` класса `TreiberStack` (поля `head_`,
`retired_`, helper `retire()` и деструктор уже даны):

```cpp
void TreiberStack::push(int v) {
    Node* n = new Node{v, nullptr, nullptr};
    n->next = head_.load(std::memory_order_relaxed);
    while (!head_.compare_exchange_weak(
               n->next, n,
               std::memory_order_release,     // успех: публикуем узел и его поля
               std::memory_order_relaxed))    // провал: n->next уже перечитан
        ;
}

bool TreiberStack::pop(int& out) {
    Node* old = head_.load(std::memory_order_acquire);   // видеть поля узла
    while (old && !head_.compare_exchange_weak(
                      old, old->next,
                      std::memory_order_acquire,
                      std::memory_order_acquire))
        ;
    if (!old) return false;
    out = old->value;
    retire(old);                              // НЕ delete! паркуем (см. ниже)
    return true;
}
```

## Ordering — что доказываем

- `push` делает **release**-CAS: публикует новый узел вместе с его `value`/`next`.
- `pop` читает `head_` с **acquire**: видит опубликованные поля снимаемого узла.
- При провале CAS `old`/`n->next` уже содержит актуальное значение — просто
  повторяем итерацию (потому и `compare_exchange_weak`).

## Про reclamation (важно понять, а не просто сдать)

Наивный `delete old` в `pop` **некорректен**: другой поток мог прочитать `old`
как вершину **до** твоего CAS и всё ещё держит на него указатель → ABA и
use-after-free (лекция §15.2). Полное решение — hazard pointers / RCU / epoch.

Здесь мы **обходим** проблему учебно-честно: снятые узлы не освобождаются сразу,
а складываются в retire-список по **отдельному** полю `rnext` (основной `next`
никогда не переиспользуется и не освобождается во время работы), а весь список
чистится в деструкторе — уже однопоточно. Поэтому в течение прогона **ни один
адрес не переиспользуется** → ABA не возникает, и утечек нет. `retire()` и
деструктор уже написаны — не трогай их.

## Проверка

Серверный автопрогон (ASan/UBSan/LeakSanitizer): однопоточный LIFO-порядок +
конкурентный прогон (4 продюсера кладут `0..K-1`, потребители снимают всё) —
проверяется, что снято ровно `PRODUCERS*K` элементов и сумма значений совпала
(никаких потерь и дублей), а в конце нет утечек.

Гонки/ordering проверяй TSan:

```sh
g++ -std=c++20 -fsanitize=thread -O1 -g -pthread solution.cpp test.cpp -o prog && ./prog
```
