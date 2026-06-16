#include "counter.h"

/*
 * Потокобезопасный счётчик на основе mutex.
 * Определение counter_t находится в counter.h.
 * Все операции должны быть атомарны с точки зрения вызывающих потоков.
 *
 * Компилировать:
 *   gcc -std=c17 -Wall -Wextra -Wconversion -Wsign-conversion \
 *       -fsanitize=address,undefined -O1 -g \
 *       starter.c test.c -o prog -lpthread
 */

/*
 * Инициализировать счётчик: value = 0, mutex инициализирован.
 * Возвращает 0 при успехе, ненулевое значение при ошибке.
 */
int counter_init(counter_t *c)
{
    (void)c;
    return -1; /* TODO */
}

/*
 * Освободить ресурсы счётчика (уничтожить mutex).
 * Возвращает 0 при успехе, ненулевое значение при ошибке.
 */
int counter_destroy(counter_t *c)
{
    (void)c;
    return -1; /* TODO */
}

/*
 * Атомарно прибавить delta к счётчику.
 * delta может быть отрицательным (вычитание).
 * Возвращает НОВОЕ значение счётчика после прибавления.
 */
long counter_add(counter_t *c, long delta)
{
    (void)c; (void)delta;
    return -1; /* TODO */
}

/*
 * Прочитать текущее значение счётчика.
 * Возвращает текущее значение.
 */
long counter_get(counter_t *c)
{
    (void)c;
    return -1; /* TODO */
}

/*
 * Сбросить счётчик в 0.
 * Возвращает ПРЕДЫДУЩЕЕ значение счётчика (до сброса).
 */
long counter_reset(counter_t *c)
{
    (void)c;
    return -1; /* TODO */
}
