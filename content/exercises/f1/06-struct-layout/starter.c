/*
 * Упражнение F1-06: Struct Layout и container_of
 *
 * Темы: выравнивание, padding, offsetof, container_of.
 *
 * Компиляция:
 *   gcc -std=c17 -Wall -Wextra -fsanitize=address,undefined \
 *       -O1 -g starter.c test.c -o prog
 *
 * Разбор bad_layout:
 *   struct bad_layout { char a; int b; char c; double d; short e; }
 *     offset 0:  char  a        (1 байт)
 *     offset 1:  pad   3 байта  (выравнивание int под 4)
 *     offset 4:  int   b        (4 байта)
 *     offset 8:  char  c        (1 байт)
 *     offset 9:  pad   7 байт   (выравнивание double под 8)
 *     offset 16: double d       (8 байт)
 *     offset 24: short  e       (2 байта)
 *     offset 26: pad   6 байт   (выравнивание struct под 8 — max member = double)
 *   sizeof(bad_layout) = 32
 *
 * Оптимальная упаковка — сортировка по убыванию выравнивания:
 *   double(8) → int(4) → short(2) → char(1), char(1)
 *   struct good_layout { double d; int b; short e; char a; char c; }
 *     offset 0: double d  (8)
 *     offset 8: int    b  (4)
 *     offset 12: short e  (2)
 *     offset 14: char  a  (1)
 *     offset 15: char  c  (1)
 *     trailing pad: 0  (sizeof = 16, выравнивание по double = 8, 16%8=0)
 *   sizeof(good_layout) = 16
 */
#include <stddef.h>   /* offsetof, size_t */
#include <stdint.h>

/* Структура с плохой упаковкой (намеренно оставлена как есть) */
struct bad_layout {
    char     a;
    int      b;
    char     c;
    double   d;
    short    e;
};

/*
 * Оптимально упакованная структура.
 * TODO: переставь поля bad_layout так, чтобы sizeof был минимальным.
 * Правило: поля с большим выравниванием ставь раньше.
 * Ожидаемый результат: sizeof(good_layout) == 16.
 */
struct good_layout {
    /* TODO: перегруппируй поля */
    char a;      /* неоптимальный порядок — исправь */
    char c;
    short e;
    int b;
    double d;
};

/* Верни sizeof(struct bad_layout) */
size_t bad_layout_size(void)  { return sizeof(struct bad_layout); }

/* Верни sizeof(struct good_layout) */
size_t good_layout_size(void) { return sizeof(struct good_layout); }

/* Верни offset поля b в bad_layout */
size_t bad_b_offset(void)  { return offsetof(struct bad_layout, b); }

/* Верни offset поля d в good_layout */
size_t good_d_offset(void) { return offsetof(struct good_layout, d); }

/* -----------------------------------------------------------------------
 * container_of — извлечение внешней структуры по указателю на её поле.
 *
 * Макрос ядра Linux (include/linux/container_of.h):
 *   #define container_of(ptr, type, member) \
 *       ((type *)((char *)(ptr) - offsetof(type, member)))
 *
 * Работает так: адрес поля member минус его смещение внутри type
 * = адрес начала объекта type.
 * ----------------------------------------------------------------------- */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* Пример структуры для связного списка ядра */
struct node {
    int          value;
    struct node *next;
};

struct list_item {
    char        name[16];
    struct node node;       /* встроенный узел списка */
    int         priority;
};

/*
 * Дан указатель на node внутри list_item,
 * верни указатель на сам list_item.
 * Используй макрос container_of.
 */
struct list_item *item_from_node(struct node *n) {
    return NULL; /* TODO: return container_of(n, struct list_item, node); */
}

/* Статическая проверка ABI (компилируется = проходит) */
_Static_assert(sizeof(struct bad_layout)  >= sizeof(struct good_layout),
               "bad должна быть >= good");
_Static_assert(sizeof(struct good_layout) >= 16,
               "good_layout должна быть минимум 16 байт (из-за double)");
