#pragma once
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>

/*
 * Потокобезопасный счётчик.
 * Студент добавляет нужные поля в структуру.
 */
typedef struct {
    long            value;   /* текущее значение */
    pthread_mutex_t mu;      /* мьютекс для защиты value */
} counter_t;
