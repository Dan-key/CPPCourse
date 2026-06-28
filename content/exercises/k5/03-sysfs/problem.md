# Задание: атрибут устройства в sysfs

sysfs (sysfs = system file system, `/sys`) — это проекция объектов ядра (`kobject`) в
дерево каталогов. В отличие от свалки `/proc`, у sysfs есть **жёсткое правило: один
файл — одно значение**. Это позволяет управлять драйвером простыми `cat`/`echo` без
парсеров, а ядру — не форматировать таблицы.

Здесь ты создаёшь `kobject` `k5_device` в `/sys/kernel/` и атрибут `mode_level`:

- **`read` (`show`)** — отдать текущее `int`-значение (`k5_mode`);
- **`write` (`store`)** — разобрать целое из строки; на мусор вернуть ошибку.

## Что реализовать

- **`mode_level_show`** — `return sysfs_emit(buf, "%d\n", k5_mode);` (а **не** `sprintf`).
- **`mode_level_store`** — `int ret = kstrtoint(buf, 10, &k5_mode); if (ret < 0) return ret;`
  затем `return count;`.
- В `init`/`exit` обвязка (`kobject_create_and_add`, `sysfs_create_file`,
  `kobject_put`) уже дана — разберись, как она работает.

## Ключевые API

- **`kobject_create_and_add(name, parent)`** — создать `kobject` (= каталог в sysfs).
  `kernel_kobj` — родитель для `/sys/kernel/`.
- **`__ATTR(name, mode, show, store)`** — собрать `struct kobj_attribute` (макрос задаёт
  имя файла и коллбеки).
- **`sysfs_create_file(kobj, &attr.attr)`** — повесить атрибут (файл) на `kobject`.
- **`sysfs_emit(buf, fmt, ...)`** — безопасная печать значения в буфер sysfs.
- **`kstrtoint(buf, base, &val)`** — разбор строки в `int` с проверкой переполнения и
  мусора (в ядре **нет** `atoi`).
- **`kobject_put(kobj)`** — уменьшить счётчик ссылок; на нуле ядро само освобождает
  объект и убирает каталог.

## Почему `sysfs_emit`, а не `sprintf`

Буфер, который ядро даёт в `show`, — ровно одна страница (`PAGE_SIZE`). `sysfs_emit`
знает про эту границу и не даст переполнить буфер; `sprintf` слепо пишет сколько
попросили. Поэтому в sysfs-методах канон — `sysfs_emit`.

## Почему `kobject_put`, а не `kfree`

`kobject` живёт по **счётчику ссылок** (kref). Пока кто-то держит файл открытым, объект
не должен исчезнуть. Прямой `kfree` мимо счётчика — это use-after-free, если параллельно
идёт чтение sysfs. Правильно — `kobject_put`: память освободится, когда последняя
ссылка уйдёт.

## Проверка

QEMU: тест грузит модуль, проверяет `/sys/kernel/k5_device/mode_level`, пишет `777` и
читает обратно, затем пишет нечисло (`bad_value`) и ждёт **ошибку записи** (драйвер
не должен молча проглотить мусор и не должен упасть). После `rmmod` каталог исчезает,
`dmesg` чист.
