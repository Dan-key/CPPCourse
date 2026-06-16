#include "counter.h"
#include <stdio.h>
#include <string.h>

/*
 * Примечание: компилировать вместе с starter.c:
 *   gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
 *       -fsanitize=address,undefined -O1 -g \
 *       starter.c test.c -o prog -lpthread
 *
 * Для проверки гонок данных (вместо ASan):
 *   gcc -fsanitize=thread -O1 -g starter.c test.c -o prog -lpthread
 */

int  counter_init(counter_t *c);
int  counter_destroy(counter_t *c);
long counter_add(counter_t *c, long delta);
long counter_get(counter_t *c);
long counter_reset(counter_t *c);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

/* ---- Параметры конкурентного теста ---- */
#define NTHREADS    4
#define ITERS_EACH  10000

typedef struct {
    counter_t *c;
    int        iters;
    long       delta;
} thread_arg_t;

static void *increment_worker(void *arg)
{
    thread_arg_t *a = (thread_arg_t *)arg;
    for (int i = 0; i < a->iters; i++) {
        counter_add(a->c, a->delta);
    }
    return NULL;
}

int main(void)
{
    counter_t c;
    memset(&c, 0, sizeof(c));

    printf("=== Базовые операции ===\n");

    /* Тест 1: counter_init возвращает 0 */
    {
        int rc = counter_init(&c);
        CHECK(rc == 0, "counter_init возвращает 0");
    }

    /* Тест 2: после init значение == 0 */
    {
        long v = counter_get(&c);
        CHECK(v == 0, "counter_get после init == 0");
    }

    /* Тест 3: counter_add(+5) возвращает 5 */
    {
        long v = counter_add(&c, 5);
        CHECK(v == 5, "counter_add(+5): возвращает новое значение 5");
    }

    /* Тест 4: counter_add(-3) возвращает 2 */
    {
        long v = counter_add(&c, -3);
        CHECK(v == 2, "counter_add(-3): возвращает новое значение 2");
    }

    /* Тест 5: counter_get после предыдущих операций == 2 */
    {
        long v = counter_get(&c);
        CHECK(v == 2, "counter_get после add(5) add(-3) == 2");
    }

    /* Тест 6: counter_reset возвращает предыдущее значение (2) */
    {
        long prev = counter_reset(&c);
        CHECK(prev == 2, "counter_reset возвращает предыдущее значение 2");
    }

    /* Тест 7: после reset counter_get == 0 */
    {
        long v = counter_get(&c);
        CHECK(v == 0, "counter_get после reset == 0");
    }

    /* Тест 8: counter_add большого значения */
    {
        long v = counter_add(&c, 1000000L);
        CHECK(v == 1000000L, "counter_add(1000000): возвращает 1000000");
        counter_reset(&c);
    }

    printf("\n=== Конкурентный тест (%d потоков x %d итераций) ===\n",
           NTHREADS, ITERS_EACH);

    /* Тест 9: конкурентное инкрементирование — нет потерянных обновлений */
    {
        pthread_t threads[NTHREADS];
        thread_arg_t args[NTHREADS];

        for (int i = 0; i < NTHREADS; i++) {
            args[i].c     = &c;
            args[i].iters = ITERS_EACH;
            args[i].delta = 1L;
            if (pthread_create(&threads[i], NULL, increment_worker, &args[i]) != 0) {
                fprintf(stderr, "pthread_create failed\n");
                return 1;
            }
        }

        for (int i = 0; i < NTHREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        long expected = (long)NTHREADS * ITERS_EACH;
        long actual   = counter_get(&c);
        CHECK(actual == expected,
              "конкурентный counter_add: нет потерянных обновлений (4x10000=40000)");

        if (actual != expected) {
            printf("       ожидалось: %ld, получено: %ld\n", expected, actual);
        }

        counter_reset(&c);
    }

    /* Тест 10: смешанные операции (2 inc + 2 dec → итог 0) */
    {
        pthread_t threads[NTHREADS];
        thread_arg_t args[NTHREADS];

        for (int i = 0; i < NTHREADS; i++) {
            args[i].c     = &c;
            args[i].iters = ITERS_EACH;
            args[i].delta = (i < NTHREADS / 2) ? 1L : -1L;
            pthread_create(&threads[i], NULL, increment_worker, &args[i]);
        }

        for (int i = 0; i < NTHREADS; i++) {
            pthread_join(threads[i], NULL);
        }

        long v = counter_get(&c);
        CHECK(v == 0, "конкурентный: 2x(+10000) + 2x(-10000) = 0");

        counter_reset(&c);
    }

    /* Тест 11: counter_add с нулём не меняет значение */
    {
        counter_add(&c, 7);
        long before = counter_get(&c);
        long v = counter_add(&c, 0);
        CHECK(v == before && counter_get(&c) == before,
              "counter_add(0): значение не изменилось");
        counter_reset(&c);
    }

    /* Тест 12: отрицательный счётчик — reset возвращает отрицательное */
    {
        counter_add(&c, -42);
        long prev = counter_reset(&c);
        CHECK(prev == -42,
              "counter_reset: корректно возвращает отрицательное предыдущее значение");
        CHECK(counter_get(&c) == 0,
              "counter_get после reset отрицательного == 0");
    }

    /* Тест 13: counter_get потокобезопасен при конкурентном чтении */
    {
        counter_add(&c, 100);
        pthread_t threads[NTHREADS];
        thread_arg_t args[NTHREADS];
        for (int i = 0; i < NTHREADS; i++) {
            args[i].c     = &c;
            args[i].iters = 0;
            args[i].delta = 0;
            pthread_create(&threads[i], NULL, increment_worker, &args[i]);
        }
        for (int i = 0; i < NTHREADS; i++) pthread_join(threads[i], NULL);

        long v = counter_get(&c);
        CHECK(v == 100, "counter_get потокобезопасен при конкурентном чтении");
        counter_reset(&c);
    }

    /* Тест 14: counter_destroy возвращает 0 */
    {
        int rc = counter_destroy(&c);
        CHECK(rc == 0, "counter_destroy возвращает 0");
    }

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
