# Задание: счётчик открытий через open/release и atomic_t

Драйверы часто ведут **состояние на открытие**: кто открыл, сколько раз, контекст
сессии. Здесь ты делаешь misc-устройство `/dev/cppcount`, которое считает **каждое
открытие** (`open`) атомарным счётчиком и на `read` возвращает **номер этого
открытия**. Это учит обработчики `open`/`release`, **`atomic_t`** (потокобезопасный
счётчик без мьютекса) и хранение per-open состояния в `file->private_data`.

## Что реализовать

```c
#include <linux/atomic.h>

static atomic_t open_count = ATOMIC_INIT(0);

// open:    n = atomic_inc_return(&open_count);  запомнить n в file->private_data
// read:    вернуть n (из private_data) как ДЕСЯТИЧНЫЙ текст ("1", "2", ...), с учётом *ppos
// release: ничего (или лог)
```

Семантика:
- **`open`** — увеличить `open_count` атомарно, **запомнить полученный номер** в
  `file->private_data` (приведение через `(void *)(long)n`);
- **`read`** — отформатировать сохранённый номер в строку и отдать её (с учётом
  `*ppos`, EOF при повторном чтении);
- каждый новый `open` (новый `cat`) → следующий номер: 1, 2, 3, …

## Подсказки

```c
static int cnt_open(struct inode *inode, struct file *f){
    long n = atomic_inc_return(&open_count);     // атомарно ++ и вернуть новое значение
    f->private_data = (void *)n;                 // снимок номера для ЭТОГО открытия
    return 0;
}
static ssize_t cnt_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos){
    char tmp[32];
    long n = (long)f->private_data;
    int len = scnprintf(tmp, sizeof tmp, "%ld", n);   // снимок → текст
    return simple_read_from_buffer(ubuf, count, ppos, tmp, len);
}
```

## Почему `atomic_t`, а не мьютекс

Счётчик `open` могут дёргать **параллельно** несколько процессов. `atomic_t` даёт
**потокобезопасный** инкремент **без блокировки** (`atomic_inc_return` — одна
атомарная инструкция, C1): дёшево и не нужен мьютекс ради одного `++`. Мьютекс брали
бы, если бы защищали **составную** структуру (буфер + длина, как в `01-char-dev`).
Правило: один счётчик/флаг → `atomic_t`; несколько связанных полей → мьютекс/спинлок.

## Модульный refcount: почему важен `.owner`

`.owner = THIS_MODULE` в `file_operations` поднимает **счётчик ссылок модуля**, пока
устройство открыто: `rmmod` не выгрузит модуль, у которого открыт `/dev/cppcount`
(иначе при чтении был бы вызов кода уже выгруженного модуля → oops). Это базовая
защита времени жизни в ядре (аналог refcount из C1/C3, но для модуля).

## Проверка

Автопрогон (QEMU): `insmod cppmod.ko` → `/dev/cppcount`; первый `cat` → `"1"`,
второй → `"2"`, третий → `"3"` (каждое открытие = следующий номер); номер
ненулевой и растёт строго. Реализуй `open` (atomic_inc_return + private_data) и
`read` (снимок → текст) — все пройдут.
