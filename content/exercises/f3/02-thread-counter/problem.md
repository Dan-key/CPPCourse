# Задание: потокобезопасный счётчик на mutex

Реализуй счётчик, который можно безопасно изменять из нескольких потоков
одновременно. Тип `counter_t` объявлен в `counter.h` (содержит `value` и
`pthread_mutex_t`).

## Интерфейс

```c
int  counter_init(counter_t *c);          // value = 0, mutex готов; 0 при успехе
int  counter_destroy(counter_t *c);       // pthread_mutex_destroy; 0 при успехе
long counter_add(counter_t *c, long d);   // атомарно += d, вернуть НОВОЕ значение
long counter_get(counter_t *c);           // вернуть текущее значение
long counter_reset(counter_t *c);         // обнулить, вернуть ПРЕДЫДУЩЕЕ значение
```

`delta` может быть отрицательным (вычитание).

## Ключевая идея

Каждая операция, читающая или меняющая `value`, должна происходить
под захваченным mutex — иначе гонка данных (data race), которую поймает
ThreadSanitizer/ASan.

```c
long counter_add(counter_t *c, long delta) {
    pthread_mutex_lock(&c->mu);
    c->value += delta;
    long now = c->value;
    pthread_mutex_unlock(&c->mu);
    return now;
}
```

- Чтение `counter_get` тоже под mutex — без него возможно «рваное» чтение
  на некоторых платформах и UB по модели памяти C.
- Всегда снимай блокировку на всех путях выхода (включая early-return).

## Компиляция

Тесты собираются с `-lpthread` и запускают много потоков, конкурентно
вызывающих `counter_add`. Если хоть одна операция вне mutex — суммарное
значение не сойдётся.

## Тесты

- Однопоточная корректность: `add`, `get`, `reset` возвращают верные значения
- N потоков по K инкрементов → итог ровно `N*K`
- `counter_reset` возвращает предыдущее значение и обнуляет счётчик
