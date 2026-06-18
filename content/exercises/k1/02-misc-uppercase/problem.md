# Задание: misc-устройство, переводящее текст в верхний регистр

Второй драйвер — на **`miscdevice`**: это упрощённый способ зарегистрировать
символьное устройство (одна `misc_register()` вместо `alloc_chrdev_region` +
`cdev_add` + `class_create` + `device_create`). Ядро само выделит minor, создаст
узел `/dev/cppupper` (через misc-класс и devtmpfs) и направит `read`/`write` в твой
`file_operations`.

Поведение: что записали — то и хранится; при чтении устройство отдаёт сохранённое,
**переведённое в ВЕРХНИЙ регистр**. Это учит **обработке данных** в драйвере на
границе `copy_to_user` (не просто «отдать буфер», а преобразовать).

## Что реализовать

```c
#include <linux/miscdevice.h>

static struct miscdevice cppupper = {
    .minor = MISC_DYNAMIC_MINOR,   // ядро выберет minor само
    .name  = "cppupper",           // → /dev/cppupper
    .fops  = &fops,
};
// init:  misc_register(&cppupper);     (вернёт 0 или -errno)
// exit:  misc_deregister(&cppupper);
```

Семантика:
- **`write`** — сохранить до `BUF_SIZE` байт в буфер ядра, запомнить длину;
- **`read`** — отдать сохранённое, **каждый байт `'a'..'z'` → `'A'..'Z'`** (остальные
  без изменений); корректно обработать `*ppos` (EOF → вернуть 0).
- мьютекс вокруг буфера.

## Подсказка по `read` с преобразованием

```c
static ssize_t upper_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos){
    char tmp[BUF_SIZE];
    size_t n, i;
    mutex_lock(&lock);
    if (*ppos >= (loff_t)data_len) { mutex_unlock(&lock); return 0; }   // EOF
    n = min(count, data_len - (size_t)*ppos);
    for (i = 0; i < n; i++) {
        char c = buffer[*ppos + i];
        tmp[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;     // upper
    }
    mutex_unlock(&lock);
    if (copy_to_user(ubuf, tmp, n)) return -EFAULT;   // copy_to_user может спать — ВНЕ мьютекса
    *ppos += n;
    return n;
}
```

> **Тонкость:** `copy_to_user` может **спать** (page fault на странице userspace) —
> держать мьютекс во время копирования **можно** (мьютекс sleepable), но если бы это
> был **spinlock**, спать под ним нельзя (K2). Здесь показан безопасный приём:
> скопировать в локальный `tmp` под мьютексом, отпустить, потом `copy_to_user`.

## `cdev`+class vs `miscdevice` — когда что

- **`miscdevice`** — когда нужно **одно** простое символьное устройство с
  динамическим minor под общим misc-major (10). Минимум кода. Идеально для
  вспомогательных устройств, отладочных интерфейсов.
- **полный `cdev` + class** (упр. `01-char-dev`) — когда нужно **несколько** minor'ов,
  свой major, контроль над классом/именами, sysfs-атрибуты. Это «настоящий» путь для
  драйверов реальных устройств.

`miscdevice` внутри — обёртка над тем же `cdev`/VFS-механизмом; просто ядро делает
рутину за тебя.

## Отладка

`pr_info("cppupper: ...")` → `dmesg`. `lsmod`, `rmmod cppmod`. Узел: `ls -l
/dev/cppupper` (major 10 — misc).

## Проверка

Автопрогон (QEMU): `insmod cppmod.ko` → `/dev/cppupper` создан; `echo -n "hello
World 42" > /dev/cppupper` затем `cat` → `"HELLO WORLD 42"`; цифры/пробелы не
тронуты; перезапись новым текстом тоже апперкейзится. Реализуй misc-регистрацию и
`read`-преобразование — все пройдут.
