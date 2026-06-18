# Задание: read-only устройство (read отдаёт инфо, write запрещён)

Многие драйверы — **только для чтения**: устройство выдаёт данные (версия, статус,
сенсор), а писать в него бессмысленно. Здесь ты делаешь misc-устройство
`/dev/cppinfo`, которое на `read` отдаёт фиксированную строку, а на `write`
возвращает **`-EACCES`** (как и положено read-only). Это учит **правильным кодам
возврата** из драйвера и идиоме `simple_read_from_buffer`.

## Что реализовать

```c
static const char INFO[] = "CPPK1-INFO-OK";   // что отдаём на чтение (без '\0' в выводе)

// read: отдать INFO, корректно обработав *ppos (повторный read → EOF).
// write: вернуть -EACCES (устройство read-only).
```

Семантика:
- **`read`** — вернуть содержимое `INFO` (длиной `sizeof(INFO)-1`, без терминатора),
  двигая `*ppos`; на повторном чтении (`*ppos >= len`) — вернуть `0` (EOF), чтобы
  `cat` завершился.
- **`write`** — **всегда** вернуть `-EACCES`.

## Идиома: `simple_read_from_buffer`

Ядро даёт готовый помощник для «отдать кусок статического буфера с учётом `*ppos`»:

```c
static ssize_t info_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos){
    return simple_read_from_buffer(ubuf, count, ppos, INFO, sizeof(INFO) - 1);
    // сам обрежет по count, обработает *ppos, вернёт 0 на EOF, скопирует copy_to_user
}
static ssize_t info_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos){
    return -EACCES;                              // read-only
}
```

`simple_read_from_buffer` инкапсулирует то, что ты руками писал в `01-char-dev`
(границы, `*ppos`, `copy_to_user`, EOF). Знать его полезно — это стандартный способ
отдавать статические/небольшие данные (часто в debugfs/procfs-обработчиках).

## Коды возврата драйвера — это контракт с userspace

`read`/`write` возвращают `ssize_t`: **`>0`** — число байт, **`0`** — EOF (для read),
**отрицательное `-errno`** — ошибка (превращается в `-1` + `errno` у вызывающего, Ф2).
- `-EACCES` — нет прав/операция запрещена (наш write);
- `-EFAULT` — плохой userspace-указатель (`copy_*_user` не смог);
- `-EINVAL` — неверный аргумент;
- `-ENOMEM` — нет памяти;
- `-EAGAIN` — нет данных сейчас (неблокирующий режим).

Возвращать **правильный** код важно: userspace по нему принимает решения (например,
`echo > /dev/cppinfo` напечатает «Permission denied» именно из-за `-EACCES`).

## Проверка

Автопрогон (QEMU): `insmod cppmod.ko` → `/dev/cppinfo`; `cat /dev/cppinfo` →
`"CPPK1-INFO-OK"`; повторный `cat` снова даёт то же (EOF корректен, не зацикливается);
`echo -n x > /dev/cppinfo` **завершается с ошибкой** (write → -EACCES). Реализуй read
(через `simple_read_from_buffer`) и write (`-EACCES`) — все пройдут.
