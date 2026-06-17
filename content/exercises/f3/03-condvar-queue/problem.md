# Задание: bounded producer-consumer очередь на condvar

Реализуй потокобезопасную очередь фиксированной ёмкости (`QUEUE_CAP = 8`) —
классический producer-consumer. Производители блокируются, когда очередь полна;
потребители блокируются, когда она пуста.

## Структура

```c
typedef struct {
    int    buf[QUEUE_CAP];
    size_t head, tail, count;
    pthread_mutex_t mu;
    pthread_cond_t  not_empty;  // сигналим, когда положили элемент
    pthread_cond_t  not_full;   // сигналим, когда забрали элемент
} bqueue_t;
```

## Интерфейс

```c
int    bqueue_init(bqueue_t *q);              // 0
int    bqueue_destroy(bqueue_t *q);           // 0
int    bqueue_put(bqueue_t *q, int val);      // блокирует, если полна; 0
int    bqueue_get(bqueue_t *q, int *val);     // блокирует, если пуста; 0
size_t bqueue_size(bqueue_t *q);              // count под mutex
```

## Ключевая идея: ждать в цикле `while`

```c
int bqueue_put(bqueue_t *q, int val) {
    pthread_mutex_lock(&q->mu);
    while (q->count == QUEUE_CAP)               // именно while, не if!
        pthread_cond_wait(&q->not_full, &q->mu);
    q->buf[q->tail] = val;
    q->tail = (q->tail + 1) % QUEUE_CAP;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mu);
    return 0;
}
```

- **`while`, а не `if`**: `pthread_cond_wait` подвержен ложным пробуждениям
  (spurious wakeups), и условие нужно перепроверять после возврата.
- `pthread_cond_wait` атомарно отпускает mutex и засыпает, а при пробуждении
  снова захватывает его.
- Кольцевой буфер: `head`/`tail` по модулю `QUEUE_CAP`, `count` — число элементов.

## Тесты

- Один producer + один consumer прокачивают N значений в правильном порядке (FIFO)
- Несколько producers/consumers: ни один элемент не потерян и не задвоен
- `put` в полную очередь блокируется до `get`; `get` из пустой — до `put`
