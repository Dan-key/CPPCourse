/*
 * EL1-02: Cross-Hello — checkpoint, не автотестируемое.
 *
 * Это задание проверяется вручную:
 *   1. Скомпилировать starter.c для AArch64
 *   2. Проверить вывод file(1) и readelf(1)
 *   3. Запустить через qemu-aarch64-static
 *
 * Этот файл компилируется только как smoke-test что starter.c валиден C.
 *
 * Компиляция (host):
 *   gcc -std=c17 -Wall starter.c test.c -o prog_host && ./prog_host
 *
 * Кросс-компиляция (проверить вручную):
 *   aarch64-linux-gnu-gcc -std=c17 -Wall starter.c -o hello_arm
 *   file hello_arm | grep -q "ARM aarch64" && echo "OK: ELF для AArch64"
 */
#include <stdio.h>

/* main определён в starter.c — этот файл только печатает инструкцию */
int main(void);

/* Пустой тест — компилируемость starter.c проверена самим компилятором */
/* Реальная проверка: запустить команды из комментариев в starter.c */
