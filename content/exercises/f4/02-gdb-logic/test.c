#include <stdio.h>
#include <stddef.h>

/* Объявления функций из starter.c */
int binary_search(const int *arr, int n, int val);
int fib(int n);
int str_find_char(const char *s, size_t len, char c);

static int g_run = 0, g_pass = 0;
#define CHECK(cond, msg) do { g_run++; if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } else { printf("  [FAIL] %s\n", msg); } } while(0)

int main(void)
{
    printf("=== 02-gdb-logic: тесты ===\n\n");

    /* --- binary_search --- */
    printf("-- binary_search --\n");
    {
        int arr[] = {1, 3, 5, 7, 9};
        CHECK(binary_search(arr, 5, 1) == 0,  "binary_search: val=1 -> idx 0");
        CHECK(binary_search(arr, 5, 5) == 2,  "binary_search: val=5 -> idx 2");
        CHECK(binary_search(arr, 5, 9) == 4,  "binary_search: val=9 -> idx 4");
        CHECK(binary_search(arr, 5, 4) == -1, "binary_search: val=4 -> -1 (не найден)");
    }
    {
        int arr[] = {42};
        CHECK(binary_search(arr, 1, 42) == 0, "binary_search: одноэлементный массив {42}, val=42 -> 0");
    }

    /* --- fib --- */
    printf("-- fib --\n");
    CHECK(fib(0)  == 0,  "fib(0) == 0");
    CHECK(fib(1)  == 1,  "fib(1) == 1");
    CHECK(fib(2)  == 1,  "fib(2) == 1");
    CHECK(fib(6)  == 8,  "fib(6) == 8");
    CHECK(fib(-1) == -1, "fib(-1) == -1");

    /* --- str_find_char --- */
    printf("-- str_find_char --\n");
    CHECK(str_find_char("hello", 5, 'h') == 0,  "str_find_char(\"hello\", 5, 'h') == 0");
    CHECK(str_find_char("hello", 5, 'x') == -1, "str_find_char(\"hello\", 5, 'x') == -1");

    printf("\nРезультат: %d / %d тестов прошли.\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
