/* Это упражнение для kernel module.
 * Автотест (userspace compile) невозможен — нужен Linux kernel.
 *
 * Для проверки:
 * 1. Собери kernel module: make -C /path/to/kernel M=$(pwd) modules
 * 2. Загрузи в QEMU с нужным DTS узлом: insmod dt_parse.ko
 * 3. Проверь вывод: dmesg | grep dtparse
 *
 * Ожидаемый вывод после insmod:
 *   dtparse ff000000.mydev: clock-frequency = 1000000
 *   dtparse ff000000.mydev: label = my-test-device
 *   dtparse ff000000.mydev: irq = 5
 *
 * Этот файл компилируется как заглушка для UI-отображения в курсе.
 */
#include <stdio.h>
int main(void) {
    printf("Это упражнение требует kernel окружения.\n");
    printf("Смотри комментарии в starter.c для инструкций.\n");
    return 0;
}
