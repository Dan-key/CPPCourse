/*
 * Упражнение EL1-02: Hello ARM — кросс-компиляция и проверка ELF
 *
 * Это практический checkpoint — не автотестируемое задание.
 *
 * ЗАДАНИЕ:
 *
 * 1. Установить кросс-компилятор на Ubuntu/Debian:
 *    sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
 *
 * 2. Скомпилировать эту программу для AArch64:
 *    aarch64-linux-gnu-gcc -std=c17 -Wall -O1 starter.c -o hello_arm
 *
 * 3. Проверить что получился ARM64 ELF:
 *    file hello_arm
 *    # → ELF 64-bit LSB pie executable, ARM aarch64, ...
 *
 *    readelf -h hello_arm | grep Machine
 *    # → Machine: AArch64
 *
 * 4. Убедиться что на x86_64 хосте запуск завершается ошибкой:
 *    ./hello_arm
 *    # → bash: ./hello_arm: cannot execute binary file: Exec format error
 *
 * 5. Запустить через QEMU user-mode:
 *    sudo apt install qemu-user-static
 *    qemu-aarch64-static -L /usr/aarch64-linux-gnu ./hello_arm
 *    # → Hello from AArch64!
 *
 * 6. (Опционально) скомпилировать для 32-бит ARM hard-float:
 *    sudo apt install gcc-arm-linux-gnueabihf
 *    arm-linux-gnueabihf-gcc -std=c17 -Wall -O1 starter.c -o hello_arm32
 *    file hello_arm32
 *    # → ELF 32-bit LSB pie executable, ARM, EABI5 version 1, ..., hard-float
 *    qemu-arm-static -L /usr/arm-linux-gnueabihf ./hello_arm32
 *    # → Hello from AArch64!  (процессор на самом деле ARMv7, но строка та же)
 *
 * 7. (Опционально) статическая линковка:
 *    aarch64-linux-gnu-gcc -std=c17 -static starter.c -o hello_arm_static
 *    file hello_arm_static
 *    # → ..., statically linked  (нет interpreter)
 *    ls -lh hello_arm hello_arm_static
 *    # Сравни размеры
 *
 * 8. Посмотреть секции ELF:
 *    aarch64-linux-gnu-objdump -h hello_arm
 *    aarch64-linux-gnu-size hello_arm
 *    aarch64-linux-gnu-readelf -S hello_arm | grep -E "\.text|\.data|\.bss"
 *
 * ДОПОЛНИТЕЛЬНО:
 *   Попробуй скомпилировать с разными уровнями оптимизации и сравни размер:
 *   aarch64-linux-gnu-gcc -O0 starter.c -o hello_O0
 *   aarch64-linux-gnu-gcc -O2 starter.c -o hello_O2
 *   aarch64-linux-gnu-gcc -Os starter.c -o hello_Os
 *   ls -lh hello_O0 hello_O2 hello_Os
 */

#include <stdio.h>

int main(void)
{
    puts("Hello from AArch64!");

    /*
     * Определим архитектуру в runtime через предопределённые макросы компилятора.
     * Это не замена is_little_endian() — просто демонстрация.
     */
#if defined(__aarch64__)
    puts("Arch: AArch64 (ARM 64-bit)");
#elif defined(__arm__)
    puts("Arch: ARM 32-bit");
#elif defined(__x86_64__)
    puts("Arch: x86_64 (running via QEMU user-mode?)");
#else
    puts("Arch: unknown");
#endif

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    puts("Byte order: little-endian");
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    puts("Byte order: big-endian");
#endif

    return 0;
}
