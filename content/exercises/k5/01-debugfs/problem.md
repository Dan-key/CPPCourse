# Задание: debugfs-интерфейс к драйверу

Главная идиома K5: когда драйверу нужно **выставить наружу своё внутреннее состояние**
(счётчики, регистры, флаги отладки), он не плодит ioctl и не засоряет `/proc` — он
заводит каталог в **debugfs** (debugfs = debug file system, отладочная ФС, обычно
смонтирована в `/sys/kernel/debug`). Это «песочница разработчика»: никаких правил
формата, доступ только root, и существует она ровно для дампа потрохов драйвера.

Здесь ты строишь каталог `/sys/kernel/debug/k5_debug` с двумя файлами:

- `counter` — `u32` на **чтение и запись** через хелпер `debugfs_create_u32` (ядро само
  свяжет файл с переменной — свой `read`/`write` писать не нужно);
- `status` — **read-only** файл со своими `file_operations`, который возвращает строку
  `"Driver is active\n"`.

## Что реализовать

- **`status_read`** — вернуть строку пользователю с учётом `count`/`ppos`. Готовый
  хелпер делает всё правильно (частичные чтения, EOF): `simple_read_from_buffer`.
- **`init`** — `debugfs_create_dir("k5_debug", NULL)` → `debugfs_create_u32(...)` →
  `debugfs_create_file("status", 0444, dir, NULL, &status_fops)`.
- **`exit`** — `debugfs_remove_recursive(k5_dir)` (удаляет каталог и всё внутри).

## Ключевые API

- **`debugfs_create_dir(name, parent)`** — создать каталог. `parent == NULL` → корень
  debugfs. Возвращает `struct dentry *`; ошибку проверяй через `IS_ERR()` (debugfs
  возвращает `ERR_PTR`, а не `NULL`).
- **`debugfs_create_u32(name, mode, parent, value)`** — файл-обёртка над `u32`: `cat`
  печатает число, `echo N >` записывает. Свой код чтения/записи не нужен.
- **`debugfs_create_file(name, mode, parent, data, fops)`** — файл с произвольными
  `file_operations` (когда нужен свой формат).
- **`simple_read_from_buffer(to, count, ppos, from, available)`** — корректно отдаёт
  кусок буфера ядра в userspace.
- **`debugfs_remove_recursive(dentry)`** — снести поддерево debugfs целиком.

## Почему `debugfs_remove_recursive` обязателен

Если забыть удалить файлы в `exit`, то после `rmmod` они **останутся** в debugfs, а их
`file_operations` указывают в **уже выгруженный** код модуля. Первый же `cat` такого
файла прыгнет по освобождённому адресу → **kernel oops** (use-after-free на уровне
кода). Поэтому очистка debugfs в `exit` — не вежливость, а обязанность.

## Проверка

QEMU: тест монтирует debugfs, грузит модуль, проверяет каталог `k5_debug`, пишет `42`
в `counter` и читает обратно, читает `status` (ждёт `"Driver is active"`), выгружает
модуль и убеждается, что каталог **исчез**, а в `dmesg` нет `BUG:/Oops/WARNING:`.
