# Задание: Type Punning

Переинтерпретируй биты `float` как `uint32_t` тремя способами и сравни их корректность: union, `memcpy` и каст указателя.

## Интерфейс

```c
#include <stdint.h>

uint32_t float_bits_union(float f);   // union — легально в C, UB в C++
uint32_t float_bits_memcpy(float f);  // memcpy — переносимо везде
uint32_t float_bits_cast(float f);    // *(uint32_t*)&f — UB (strict aliasing)
float    bits_to_float(uint32_t bits);
```

## Что нужно реализовать

- **`float_bits_union`** — через `union { float f; uint32_t u; }`. Чтение «не того» члена union корректно в C (§6.5.2.3), но UB в C++.
- **`float_bits_memcpy`** — через `memcpy(&u, &f, sizeof u)`. Официально рекомендованный переносимый способ; при `-O2` компилятор превращает в один `mov`.
- **`float_bits_cast`** — через `*(uint32_t*)&f`. **Нарушает strict aliasing (§6.5p7) — это UB.** Реализуй именно так с комментарием: тест показывает, что на x86 результат совпадает, но полагаться на это нельзя.
- **`bits_to_float`** — обратное преобразование через `memcpy`.

## IEEE 754 single precision

```
знак(1) | экспонента(8) | мантисса(23)
  0.0f  = 0x00000000
  1.0f  = 0x3F800000
 -0.0f  = 0x80000000   (только знаковый бит)
  2.0f  = 0x40000000
```

## Что проверяется

- Все три метода дают `1.0f → 0x3F800000`, `-0.0f → 0x80000000` и т.д.
- Round-trip: `float → bits → float` восстанавливает значение побитово (сравнение через `memcmp`).

## Компиляция и запуск

```bash
gcc -std=c17 -Wall -Wextra -fstrict-aliasing \
    -fsanitize=address,undefined -O1 -g starter.c test.c -o prog && ./prog
```
