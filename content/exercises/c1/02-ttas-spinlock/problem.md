# Задание: TTAS-спинлок на std::atomic

Реализуй спинлок (занятое ожидание) по схеме **Test-and-Test-And-Set**. Это
закрепляет acquire/release и показывает, почему «двойная проверка» снижает
трафик когерентности кэша.

## Что реализовать

В `solution.cpp` дописать методы класса `Spinlock` (поле
`std::atomic<bool> locked_` уже объявлено):

```cpp
void Spinlock::lock() {
    for (;;) {
        while (locked_.load(std::memory_order_relaxed))   // дешёвое ожидание
            cpu_pause();                                  // на ЧТЕНИИ (линия в S)
        if (!locked_.exchange(true, std::memory_order_acquire))
            return;                                        // успели занять
    }
}
void Spinlock::unlock() {
    locked_.store(false, std::memory_order_release);
}
```

## Почему TTAS, а не простой TAS

Наивный спинлок крутит дорогой `exchange` в цикле — каждая попытка делает
Read-For-Ownership и инвалидирует линию у всех ждущих ядер (cache line
bouncing, см. §4.2/§10 лекции). TTAS сначала **читает** (relaxed) в цикле: пока
замок занят, линия остаётся в состоянии Shared и не пинг-понгует. Дорогой
`exchange` дёргаем, только когда чтение показало «свободно».

## Почему acquire на захвате и release на освобождении

`exchange(..., acquire)` гарантирует, что всё, что критическая секция прочитает
после захвата, увидит записи **предыдущего** держателя (его `unlock` сделал
release). Пара release→acquire на `locked_` строит happens-before между
держателями — ровно как мьютекс. `cpu_pause()` (инструкция PAUSE на x86 /
YIELD на ARM) экономит энергию и ускоряет выход из spin-wait.

## C-эквивалент (реальность ядра)

На C11: `atomic_flag` с `atomic_flag_test_and_set_explicit(&f, memory_order_acquire)`
и `atomic_flag_clear_explicit(&f, memory_order_release)`. В самом ядре Linux
спинлоки устроены сложнее (тикетные/MCS-локи против несправедливости), но
базовый принцип acquire/release тот же — это разберём в модуле K2.

## Проверка

Серверный автопрогон (ASan/UBSan): `THREADS` потоков по `ITERS` инкрементов
**обычной** переменной под локом. При корректном locking итог обязан быть ровно
`THREADS*ITERS`, и тест следит, что внутри секции никогда не оказывается больше
одного потока. Сломанный лок → потерянные инкременты и/или зафиксированный
overlap.

Гонки проверяй локально TSan:

```sh
g++ -std=c++20 -fsanitize=thread -O1 -g -pthread solution.cpp test.cpp -o prog && ./prog
```
