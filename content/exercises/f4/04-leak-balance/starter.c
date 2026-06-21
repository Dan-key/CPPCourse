#include <stddef.h>
#include <stdio.h>

/*
 * Упражнение 04 — утечки памяти и баланс аллокаций.
 * Вместо malloc/free используется инструментированный аллокатор из test.c:
 * каждый dbg_alloc увеличивает счётчик живых блоков, dbg_free уменьшает.
 * Это ровно та техника подсчёта аллокаций из §13 лекции. Тест в конце
 * проверяет, что после освобождения счётчик вернулся в 0.
 */

extern void *dbg_alloc(size_t n);   /* как malloc, но считает живые блоки */
extern void  dbg_free(void *p);     /* как free, но уменьшает счётчик */

/* Строит массив из n строк вида "row0".."row{n-1}" (плюс сам массив). */
char **make_table(int n)
{
    char **t = dbg_alloc((size_t)n * sizeof(char *));
    for (int i = 0; i < n; i++) {
        t[i] = dbg_alloc(16);
        snprintf(t[i], 16, "row%d", i);
    }
    return t;
}

/*
 * BUG: освобождается только сам массив, но НЕ строки t[i] —
 * утечка n блоков. Освободи всё, что выделил make_table.
 */
void free_table(char **t, int n)
{
    (void)n;
    dbg_free(t);            /* TODO: строки t[0..n-1] остаются висеть */
}
