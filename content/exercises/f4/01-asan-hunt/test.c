#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Объявления функций из starter.c */
char       *str_upper(const char *s);
int         longest_word(const char *s);
long        fill_squares(int *out, int n);
const int  *find_max_ptr(const int *arr, int n);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { g_run++; if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } else { printf("  [FAIL] %s\n", msg); } } while(0)

int main(void)
{
    printf("=== 01-asan-hunt: тесты ===\n\n");

    /* --- str_upper --- */
    printf("-- str_upper --\n");
    {
        char *r = str_upper("hello");
        CHECK(strcmp(r, "HELLO") == 0, "str_upper(\"hello\") == \"HELLO\"");
        free(r);
    }
    {
        char *r = str_upper("");
        CHECK(strcmp(r, "") == 0, "str_upper(\"\") == \"\"");
        free(r);
    }
    {
        char *r = str_upper("Hello World");
        CHECK(strcmp(r, "HELLO WORLD") == 0, "str_upper(\"Hello World\") == \"HELLO WORLD\"");
        free(r);
    }

    /* --- longest_word --- */
    printf("-- longest_word --\n");
    CHECK(longest_word("the quick brown fox") == 5, "longest_word(\"the quick brown fox\") == 5");
    CHECK(longest_word("a b c") == 1,              "longest_word(\"a b c\") == 1");
    CHECK(longest_word("hello") == 5,              "longest_word(\"hello\") == 5");

    /* --- fill_squares --- */
    printf("-- fill_squares --\n");
    {
        /* n=0: записей нет, результат 0.
           Передаём адрес dummy — при баге (i<=n) запись в dummy[0] поймает ASan,
           т.к. dummy выделен ровно под 1 элемент (на стеке, сразу за guard-зоной). */
        int dummy[1];
        long s0 = fill_squares(dummy, 0);
        CHECK(s0 == 0L, "fill_squares(out, 0) == 0");
    }
    {
        /* Массив ровно на 5 элементов: запись в out[5] немедленно поймает ASan */
        int out[5];
        long s = fill_squares(out, 5);
        CHECK(out[0] == 0,  "fill_squares: out[0] == 0");
        CHECK(out[1] == 1,  "fill_squares: out[1] == 1");
        CHECK(out[2] == 4,  "fill_squares: out[2] == 4");
        CHECK(out[3] == 9,  "fill_squares: out[3] == 9");
        CHECK(out[4] == 16, "fill_squares: out[4] == 16");
        CHECK(s == 30L,     "fill_squares(out, 5) == 30");
    }

    /* --- find_max_ptr --- */
    printf("-- find_max_ptr --\n");
    {
        CHECK(find_max_ptr(NULL, 0) == NULL, "find_max_ptr(NULL, 0) == NULL");
    }
    {
        int arr[] = {3, 1, 4, 1, 5, 9, 2, 6};
        const int *p = find_max_ptr(arr, 8);
        /* Разыменование p при use-after-free вызовет ASan-ошибку */
        CHECK(p != NULL && *p == 9, "find_max_ptr({3,1,4,1,5,9,2,6}, 8): *result == 9");
    }
    {
        int arr[] = {42};
        const int *p = find_max_ptr(arr, 1);
        CHECK(p != NULL && *p == 42, "find_max_ptr({42}, 1): *result == 42");
    }

    printf("\nРезультат: %d / %d тестов прошли.\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
