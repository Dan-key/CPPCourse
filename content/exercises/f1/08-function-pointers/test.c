/*
 * Тест-харнес для F1-08: Function Pointers и Dispatch Table.
 * Не изменяй этот файл.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -fsanitize=address,undefined \
 *       -O1 -g starter.c test.c -o prog && ./prog
 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>   /* memcpy */

typedef int (*cmp_fn)(const void *a, const void *b);

/* Объявления из решения студента */
int  cmp_int_asc(const void *a, const void *b);
int  cmp_int_desc(const void *a, const void *b);
void sort_ints(int *arr, size_t n, cmp_fn cmp);
int  dispatch(int op, int arg);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

/* Проверка, что массив отсортирован по возрастанию */
static int is_sorted_asc(const int *arr, size_t n) {
    for (size_t i = 1; i < n; i++)
        if (arr[i] < arr[i-1]) return 0;
    return 1;
}

/* Проверка, что массив отсортирован по убыванию */
static int is_sorted_desc(const int *arr, size_t n) {
    for (size_t i = 1; i < n; i++)
        if (arr[i] > arr[i-1]) return 0;
    return 1;
}

int main(void) {
    printf("=== cmp_int_asc / cmp_int_desc (компараторы) ===\n");
    {
        int a = 3, b = 7, c = 3;
        CHECK(cmp_int_asc(&a, &b) < 0,  "cmp_asc(3, 7) < 0");
        CHECK(cmp_int_asc(&b, &a) > 0,  "cmp_asc(7, 3) > 0");
        CHECK(cmp_int_asc(&a, &c) == 0, "cmp_asc(3, 3) == 0");
        CHECK(cmp_int_desc(&a, &b) > 0, "cmp_desc(3, 7) > 0  (обратный)");
        CHECK(cmp_int_desc(&b, &a) < 0, "cmp_desc(7, 3) < 0  (обратный)");
    }

    printf("=== sort_ints по возрастанию ===\n");
    {
        int arr[] = {3, 1, 4, 1, 5, 9, 2, 6};
        size_t n = sizeof arr / sizeof arr[0];
        sort_ints(arr, n, cmp_int_asc);
        CHECK(is_sorted_asc(arr, n),
              "sort asc [3,1,4,1,5,9,2,6] → [1,1,2,3,4,5,6,9]");
        CHECK(arr[0] == 1 && arr[7] == 9,
              "sort asc: первый=1, последний=9");
    }

    printf("=== sort_ints по убыванию ===\n");
    {
        int arr[] = {3, 1, 4, 1, 5, 9, 2, 6};
        size_t n = sizeof arr / sizeof arr[0];
        sort_ints(arr, n, cmp_int_desc);
        CHECK(is_sorted_desc(arr, n),
              "sort desc [3,1,4,1,5,9,2,6] → [9,6,5,4,3,2,1,1]");
        CHECK(arr[0] == 9 && arr[7] == 1,
              "sort desc: первый=9, последний=1");
    }

    printf("=== sort_ints крайние случаи ===\n");
    {
        int one = 42;
        sort_ints(&one, 1, cmp_int_asc);
        CHECK(one == 42, "sort одного элемента: без изменений");

        int two[] = {7, 3};
        sort_ints(two, 2, cmp_int_asc);
        CHECK(two[0] == 3 && two[1] == 7, "sort двух элементов asc: [3, 7]");

        /* Пустой массив не должен падать (передаём не-NULL адрес, n=0) */
        int empty[1] = {0};   /* адрес не NULL, n=0 → qsort ничего не делает */
        sort_ints(empty, 0, cmp_int_asc);
        CHECK(empty[0] == 0, "sort нулевого массива (n=0) — элемент не изменён");
    }

    printf("=== dispatch table ===\n");
    CHECK(dispatch(0, 5)  == 10, "dispatch(0=double, 5) == 10");
    CHECK(dispatch(1, 5)  == 25, "dispatch(1=square, 5) == 25");
    CHECK(dispatch(2, 5)  == -5, "dispatch(2=negate, 5) == -5");
    CHECK(dispatch(3, 7)  ==  7, "dispatch(3=identity, 7) == 7");
    CHECK(dispatch(0, -3) == -6, "dispatch(0=double, -3) == -6");
    CHECK(dispatch(1, -4) == 16, "dispatch(1=square, -4) == 16");
    CHECK(dispatch(2, 0)  ==  0, "dispatch(2=negate, 0) == 0");
    CHECK(dispatch(3, -1) == -1, "dispatch(3=identity, -1) == -1");

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
