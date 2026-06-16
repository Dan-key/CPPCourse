/*
 * Упражнение F1-08: Function Pointers и Dispatch Table
 *
 * Темы: указатели на функцию, typedef, qsort, dispatch table
 *       как замена switch/vtable, передача компаратора.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -fsanitize=address,undefined \
 *       -O1 -g starter.c test.c -o prog
 */
#include <stddef.h>
#include <stdlib.h>   /* qsort */

/* Тип функции-компаратора (совместим с qsort из <stdlib.h>) */
typedef int (*cmp_fn)(const void *a, const void *b);

/*
 * cmp_int_asc — компаратор для qsort по возрастанию.
 *
 * a и b — указатели на int (const void*).
 * Возвращает: отрицательное если *a < *b, 0 если равны, положительное если *a > *b.
 *
 * ВНИМАНИЕ: нельзя просто делать (*x - *y) — переполнение для INT_MIN/-1!
 * Используй: (*x > *y) - (*x < *y)  или сравнение через if.
 */
int cmp_int_asc(const void *a, const void *b) {
    return 0; /* TODO */
}

/*
 * cmp_int_desc — компаратор по убыванию.
 * Подсказка: просто поменяй порядок аргументов cmp_int_asc.
 */
int cmp_int_desc(const void *a, const void *b) {
    return 0; /* TODO */
}

/*
 * sort_ints — сортировка массива int через qsort.
 * arr  — указатель на массив
 * n    — количество элементов
 * cmp  — компаратор (передаётся в qsort напрямую)
 */
void sort_ints(int *arr, size_t n, cmp_fn cmp) {
    /* TODO: qsort(arr, n, sizeof *arr, cmp); */
    (void)arr; (void)n; (void)cmp;
}

/* -----------------------------------------------------------------------
 * Dispatch table — таблица обработчиков по коду операции.
 *
 * Это паттерн, аналогичный vtable в C++:
 * вместо switch(op) { case 0: ...; case 1: ...; } — индексируем массив.
 * Преимущества: O(1), легко расширяется, нет fall-through ошибок.
 * ----------------------------------------------------------------------- */
typedef int (*handler_fn)(int arg);

/* Реализации операций */
int op_double(int x)   { return x * 2; }
int op_square(int x)   { return x * x; }
int op_negate(int x)   { return -x; }
int op_identity(int x) { return x; }

/*
 * dispatch — выполнить операцию по коду.
 *   op == 0 → op_double
 *   op == 1 → op_square
 *   op == 2 → op_negate
 *   op == 3 → op_identity
 *
 * Требование: использовать dispatch table (массив handler_fn),
 * НЕ switch/if-else.
 *
 * Если op вне диапазона [0, 3] — вернуть -1.
 */
int dispatch(int op, int arg) {
    return 0; /* TODO: static const handler_fn table[] = {...}; */
}
