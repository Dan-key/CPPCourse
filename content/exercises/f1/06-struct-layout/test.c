/*
 * Тест-харнес для F1-06: Struct Layout и container_of.
 * Не изменяй этот файл.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -fsanitize=address,undefined \
 *       -O1 -g starter.c test.c -o prog && ./prog
 */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>   /* memset, strcmp */

/* Объявления из решения студента */
size_t bad_layout_size(void);
size_t good_layout_size(void);
size_t bad_b_offset(void);
size_t good_d_offset(void);

/* Для container_of нужны полные типы — повторяем объявления */
struct node {
    int          value;
    struct node *next;
};

struct list_item {
    char        name[16];
    struct node node;
    int         priority;
};

struct list_item *item_from_node(struct node *n);

static int g_run = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    g_run++; \
    if (cond) { g_pass++; printf("  [OK]   %s\n", msg); } \
    else       { printf("  [FAIL] %s\n", msg); } \
} while(0)

int main(void) {
    printf("=== Размеры структур ===\n");
    CHECK(bad_layout_size() == 32,
          "sizeof(bad_layout) == 32  (с padding)");
    CHECK(good_layout_size() == 16,
          "sizeof(good_layout) == 16  (оптимальная упаковка)");
    CHECK(bad_layout_size() > good_layout_size(),
          "bad_layout больше good_layout");

    printf("=== Смещения (offsetof) ===\n");
    /*
     * bad_layout: char a (0), pad 3, int b (4), ...
     * Поле b начинается на offset 4.
     */
    CHECK(bad_b_offset() == 4,
          "offsetof(bad_layout, b) == 4  (после char a + 3 байта padding)");
    /*
     * good_layout (после перестановки): double d стоит первым → offset 0.
     * Если студент правильно поставил double первым: good_d_offset() == 0.
     */
    CHECK(good_d_offset() == 0,
          "offsetof(good_layout, d) == 0  (double стоит первым)");

    printf("=== container_of ===\n");
    {
        struct list_item item;
        memset(&item, 0, sizeof item);
        /* Инициализируем поля */
        (void)memcpy(item.name, "test_item", 10);
        item.node.value = 42;
        item.node.next  = NULL;
        item.priority   = 7;

        /* Передаём указатель на встроенный node, должны получить &item */
        struct list_item *recovered = item_from_node(&item.node);

        CHECK(recovered == &item,
              "container_of: item_from_node(&item.node) == &item");
        CHECK(recovered != NULL,
              "container_of: результат не NULL");
        if (recovered != NULL) {
            CHECK(recovered->priority == 7,
                  "container_of: priority восстановлен корректно");
            CHECK(strcmp(recovered->name, "test_item") == 0,
                  "container_of: name восстановлен корректно");
        } else {
            /* Засчитываем эти тесты как провал */
            g_run += 2;
            printf("  [FAIL] priority — не проверяем (ptr == NULL)\n");
            printf("  [FAIL] name — не проверяем (ptr == NULL)\n");
        }
    }

    printf("=== Экономия памяти ===\n");
    {
        size_t saved = bad_layout_size() - good_layout_size();
        CHECK(saved >= 8,
              "Оптимизация структуры экономит >= 8 байт на объект");
    }

    printf("\n%d/%d тестов пройдено\n", g_pass, g_run);
    return (g_pass == g_run) ? 0 : 1;
}
