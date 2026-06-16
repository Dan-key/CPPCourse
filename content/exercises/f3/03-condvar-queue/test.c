#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stddef.h>

/* ---- объявления из starter.c ---- */
#define QUEUE_CAP 8

typedef struct {
    int    buf[QUEUE_CAP];
    size_t head, tail, count;
    pthread_mutex_t mu;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} bqueue_t;

int    bqueue_init(bqueue_t *q);
int    bqueue_destroy(bqueue_t *q);
int    bqueue_put(bqueue_t *q, int val);
int    bqueue_get(bqueue_t *q, int *val);
size_t bqueue_size(bqueue_t *q);

/* ---- CHECK macro ---- */
static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { g_run++; if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } else { printf("  [FAIL] %s\n", msg); } } while(0)

/* ---- вспомогательные структуры для тестов с потоками ---- */

/* Тест 9: 1 producer, 1 consumer, 100 элементов */
typedef struct {
    bqueue_t *q;
    int       n;
} prod_arg_t;

static void *producer_thread(void *arg)
{
    prod_arg_t *a = (prod_arg_t *)arg;
    for (int i = 0; i < a->n; i++) {
        bqueue_put(a->q, i);
    }
    return NULL;
}

typedef struct {
    bqueue_t *q;
    int       n;
    int      *out;   /* массив для записи принятых значений */
} cons_arg_t;

static void *consumer_thread(void *arg)
{
    cons_arg_t *a = (cons_arg_t *)arg;
    for (int i = 0; i < a->n; i++) {
        bqueue_get(a->q, &a->out[i]);
    }
    return NULL;
}

/* Тест 13–14: 2 producers по 50 элементов, 1 consumer 100 элементов */
typedef struct {
    bqueue_t *q;
    int       start; /* первое значение */
    int       n;
    long long sum;   /* сумма отправленных */
} prod2_arg_t;

static void *producer2_thread(void *arg)
{
    prod2_arg_t *a = (prod2_arg_t *)arg;
    a->sum = 0;
    for (int i = 0; i < a->n; i++) {
        int v = a->start + i;
        a->sum += v;
        bqueue_put(a->q, v);
    }
    return NULL;
}

typedef struct {
    bqueue_t  *q;
    int        n;
    long long  sum;  /* сумма полученных */
    int        received;
} cons2_arg_t;

static void *consumer2_thread(void *arg)
{
    cons2_arg_t *a = (cons2_arg_t *)arg;
    a->sum = 0;
    a->received = 0;
    for (int i = 0; i < a->n; i++) {
        int v = 0;
        bqueue_get(a->q, &v);
        a->sum += v;
        a->received++;
    }
    return NULL;
}

int main(void)
{
    printf("=== Тест: bqueue (bounded queue с condvar) ===\n");

    bqueue_t q;

    /* 1. bqueue_init returns 0 */
    CHECK(bqueue_init(&q) == 0, "bqueue_init returns 0");

    /* 2. bqueue_size after init == 0 */
    CHECK(bqueue_size(&q) == 0, "bqueue_size after init == 0");

    /* 3. bqueue_put single element returns 0 */
    CHECK(bqueue_put(&q, 42) == 0, "bqueue_put single element returns 0");

    /* 4. bqueue_size after 1 put == 1 */
    CHECK(bqueue_size(&q) == 1, "bqueue_size after 1 put == 1");

    /* 5. bqueue_get returns 0, val == what was put */
    {
        int val = -1;
        int rc = bqueue_get(&q, &val);
        CHECK(rc == 0 && val == 42, "bqueue_get returns 0 и val == 42");
    }

    /* 6. bqueue_size after get == 0 */
    CHECK(bqueue_size(&q) == 0, "bqueue_size after get == 0");

    /* 7. Put QUEUE_CAP elements, size == QUEUE_CAP */
    for (int i = 0; i < QUEUE_CAP; i++) {
        bqueue_put(&q, i * 10);
    }
    CHECK(bqueue_size(&q) == QUEUE_CAP, "after QUEUE_CAP puts, size == QUEUE_CAP");

    /* 8. Get all QUEUE_CAP elements — FIFO order preserved */
    {
        int fifo_ok = 1;
        for (int i = 0; i < QUEUE_CAP; i++) {
            int val = -1;
            bqueue_get(&q, &val);
            if (val != i * 10) fifo_ok = 0;
        }
        CHECK(fifo_ok, "FIFO order preserved for QUEUE_CAP elements");
    }

    /* 9. Producer thread puts 100 items, consumer thread gets 100 items */
    {
        int received[100];
        memset(received, 0, sizeof(received));

        prod_arg_t pa = { &q, 100 };
        cons_arg_t ca = { &q, 100, received };

        pthread_t pt, ct;
        pthread_create(&pt, NULL, producer_thread, &pa);
        pthread_create(&ct, NULL, consumer_thread, &ca);
        pthread_join(pt, NULL);
        pthread_join(ct, NULL);

        int order_ok = 1;
        for (int i = 0; i < 100; i++) {
            if (received[i] != i) { order_ok = 0; break; }
        }
        CHECK(order_ok, "1 producer + 1 consumer: 100 items received in order");
    }

    /* 10. Interleaved put/get — no deadlock (finite work, joined threads) */
    {
        /* Запускаем 4 раунда producer+consumer по 50 элементов */
        int ok = 1;
        for (int round = 0; round < 4 && ok; round++) {
            int received[50];
            memset(received, 0, sizeof(received));
            prod_arg_t pa = { &q, 50 };
            cons_arg_t ca = { &q, 50, received };
            pthread_t pt, ct;
            pthread_create(&pt, NULL, producer_thread, &pa);
            pthread_create(&ct, NULL, consumer_thread, &ca);
            pthread_join(pt, NULL);
            pthread_join(ct, NULL);
            for (int i = 0; i < 50; i++) {
                if (received[i] != i) { ok = 0; break; }
            }
        }
        CHECK(ok, "Interleaved put/get rounds: no deadlock, correct values");
    }

    /* 11. bqueue_destroy returns 0 */
    CHECK(bqueue_destroy(&q) == 0, "bqueue_destroy returns 0");

    /* 12. After destroy + re-init, queue works again */
    {
        int reinit_ok = 0;
        if (bqueue_init(&q) == 0) {
            if (bqueue_size(&q) == 0) {
                bqueue_put(&q, 99);
                int v = -1;
                bqueue_get(&q, &v);
                if (v == 99 && bqueue_size(&q) == 0) {
                    reinit_ok = 1;
                }
            }
            bqueue_destroy(&q);
        }
        CHECK(reinit_ok, "destroy + re-init: queue works correctly");
    }

    /* 13–14. Concurrent: 2 producers put 50 each, 1 consumer gets 100 */
    {
        bqueue_init(&q);

        prod2_arg_t pa1 = { &q, 0,  50, 0 };   /* значения 0..49 */
        prod2_arg_t pa2 = { &q, 50, 50, 0 };   /* значения 50..99 */
        cons2_arg_t ca  = { &q, 100, 0, 0 };

        pthread_t pt1, pt2, ct;
        pthread_create(&pt1, NULL, producer2_thread, &pa1);
        pthread_create(&pt2, NULL, producer2_thread, &pa2);
        pthread_create(&ct,  NULL, consumer2_thread, &ca);

        pthread_join(pt1, NULL);
        pthread_join(pt2, NULL);
        pthread_join(ct,  NULL);

        long long expected_sum = 0;
        for (int i = 0; i < 100; i++) expected_sum += i;

        CHECK(ca.received == 100, "2 producers + 1 consumer: total received == 100");
        CHECK(ca.sum == expected_sum, "2 producers + 1 consumer: sum of values не повреждена");

        bqueue_destroy(&q);
    }

    printf("\nРезультат: %d/%d тестов прошли\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
