#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stddef.h>
#include <string.h>

#define QUEUE_CAP 8

typedef struct {
    int    buf[QUEUE_CAP];
    size_t head, tail, count;
    pthread_mutex_t mu;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} bqueue_t;

/* Инициализировать очередь. Вернуть 0. */
int bqueue_init(bqueue_t *q)
{
    (void)q;
    return -1; /* TODO */
}

/* Уничтожить ресурсы. Вернуть 0. */
int bqueue_destroy(bqueue_t *q)
{
    (void)q;
    return -1; /* TODO */
}

/* Положить элемент в очередь.
   Если очередь полна — блокировать до появления места.
   Вернуть 0. */
int bqueue_put(bqueue_t *q, int val)
{
    (void)q; (void)val;
    return -1; /* TODO */
}

/* Забрать элемент из очереди.
   Если очередь пуста — блокировать до появления элемента.
   Записать значение в *val. Вернуть 0. */
int bqueue_get(bqueue_t *q, int *val)
{
    (void)q; (void)val;
    return -1; /* TODO */
}

/* Вернуть текущее количество элементов (под mutex). */
size_t bqueue_size(bqueue_t *q)
{
    (void)q;
    return 0; /* TODO */
}
