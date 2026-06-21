#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Инструментированный аллокатор: считает живые блоки. */
long g_live = 0;
void *dbg_alloc(size_t n) { void *p = malloc(n); if (p) g_live++; return p; }
void  dbg_free(void *p)   { if (p) { free(p); g_live--; } }

/* Реализует студент в solution.c */
char **make_table(int n);
void   free_table(char **t, int n);

static int run = 0, pass = 0;
#define CHECK(cond, msg) do { run++; if (cond) { pass++; printf("  [OK]   %s\n", msg); } \
                              else printf("  [FAIL] %s\n", msg); } while (0)

int main(void)
{
    printf("=== 04-leak-balance: тесты ===\n\n");

    const int N = 5;
    char **t = make_table(N);

    /* Содержимое построено корректно? */
    CHECK(strcmp(t[0], "row0") == 0, "make_table[0] == \"row0\"");
    CHECK(strcmp(t[4], "row4") == 0, "make_table[4] == \"row4\"");
    CHECK(g_live == N + 1,           "выделено N строк + массив");

    free_table(t, N);

    /* Главная проверка: после освобождения утечек нет. */
    CHECK(g_live == 0, "после free_table баланс аллокаций == 0 (нет утечки)");

    /* Несколько циклов build/free не должны накапливать живые блоки. */
    for (int k = 0; k < 10; k++) {
        char **x = make_table(3);
        free_table(x, 3);
    }
    CHECK(g_live == 0, "10 циклов build/free не накапливают утечку");

    printf("\n%d/%d пройдено (живых блоков: %ld)\n", pass, run, g_live);
    return (pass == run) ? 0 : 1;
}
