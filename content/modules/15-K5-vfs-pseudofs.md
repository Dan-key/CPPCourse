# Модуль K5 — VFS и псевдо-файловые системы

> Этап 2B, Сторона ядра. Метка трека: *(новое)*. В K1 ты дал устройству интерфейс
> `read`/`write`/`ioctl` через `file_operations`, в K2–K3 защитил его состояние и
> вынес тяжёлую работу из прерывания, в K4 научился правильно просить память. Но всё
> это ты вешал на **одно символьное устройство в `/dev`**. А ядро Linux держится на
> более широкой идее — **«всё есть файл»**: процессы (`/proc`), параметры драйверов
> (`/sys`), отладочные дампы (`/sys/kernel/debug`), сокеты, каналы — всё это
> выглядит как файлы и читается теми же `open`/`read`/`write`. Подсистема, которая
> делает это возможным, называется **VFS** (Virtual File System — виртуальная
> файловая система): тонкий слой абстракции между системными вызовами и конкретной
> реализацией (ext4, tmpfs, твой драйвер). В этом модуле ты разберёшь четыре кита VFS
> (`super_block`, `inode`, `dentry`, `file`), научишься выставлять состояние драйвера
> наружу через **debugfs / procfs / sysfs**, безопасно отдавать **коллекции** через
> `seq_file`, и — амбициозная вершина — зарегистрируешь **собственную файловую
> систему в памяти**. Если ты уверенно объясняешь, чем `inode` отличается от
> `dentry`, почему в sysfs «один файл — одно значение», и где брать блокировку при
> итерации списка через `seq_file`, — проматывай к самопроверке.
>
> **Язык — только C.** Ядро на C (GNU-C). Здесь — интерфейсы ядра наружу и внутреннее
> устройство файловых систем.
>
> **Опирается на K1–K4, F2/F3.** `file_operations` и `copy_to/from_user` — из K1.
> Мьютексы/RCU при защите коллекций — из K2 (теперь в разрезе итерации `seq_file`).
> `kmalloc`/`kfree`, slab, GFP — из K4. Системные вызовы `open`/`read`/`stat` со
> стороны userspace — из F2; виртуальная память и страницы — из F3 (всплывут в page
> cache).

**Читать к модулю:**

- **«Linux Kernel Development» (Robert Love)** — гл. 13 «The Virtual Filesystem» и
  гл. 12 (Page Cache). Лучшее концептуальное введение в VFS.
- **«Linux Device Drivers, 3rd» (LDD3)** — гл. 4 (debugfs и техники отладки), гл. 14
  (The Linux Device Model: kobject, kset, sysfs). Учти: LDD3 написана до перехода на
  `proc_ops`/`fs_context`, синтаксис местами устарел — сверяйся с актуальными доками.
- **`docs.kernel.org`** — `filesystems/vfs.rst` (главный справочник по объектам и
  операциям), `filesystems/seq_file.rst`, `filesystems/sysfs.rst`,
  `filesystems/debugfs.rst`, `filesystems/path-lookup.rst`, `core-api/kobject.rst`.
- **Исходники ядра:** `include/linux/fs.h` (все структуры VFS), `fs/libfs.c` (готовые
  кирпичи для простых ФС), `fs/seq_file.c`, `fs/debugfs/`, `fs/proc/`, `fs/sysfs/`,
  `fs/ramfs/` (эталон in-memory ФС), `lib/kobject.c`.

---

## 0. Карта модуля

| Раздел | О чём | Зачем системщику |
|--------|-------|------------------|
| 1 | Зачем нужен VFS; «всё есть файл» | Понять, что за абстракция стоит за `open`/`read` |
| 2 | Четыре объекта VFS: `super_block`, `inode`, `dentry`, `file` | Базовый словарь — без него дальше никак |
| 3 | Псевдо-ФС: procfs / sysfs / debugfs — что, зачем, чем отличаются | Выбрать правильный интерфейс наружу |
| 4 | debugfs — отладочный интерфейс драйвера | Упражнение 01; самый быстрый способ выставить состояние |
| 5 | `seq_file` — отдавать коллекции без потерь и гонок | Упражнение 02; идиома вывода списков |
| 6 | procfs — историческое окно в ядро; `proc_ops` | Когда уместно `/proc`, а когда нет |
| 7 | sysfs и модель устройств: `kobject`, атрибуты, `kref` | Упражнение 03; интеграция в `/sys` |
| 8 | Своя ФС: `file_system_type`, `fs_context`, `fill_super` | Упражнение 04; устройство ФС изнутри |
| 9 | Page cache и `address_space_operations` | Как ядро кэширует содержимое файлов |
| 10 | Path lookup и dcache | Как путь превращается в `inode` |
| 11 | Галерея типичных ошибок | Узнавать баги по `dmesg` |
| 12 | Сборка и практика в QEMU | Как гонять упражнения |
| 13–16 | Практика, банк вопросов, глоссарий, что дальше | Закрепление |

**Время на модуль:** 25–40 часов (с QEMU и упражнениями).

**Что значит «освоено» (из трека):** *сделай debugfs-интерфейс к своему драйверу;
(амбициозно) простейшую in-memory ФС.* Не «слышал про VFS», а понимаешь связь
`super_block ↔ inode ↔ dentry ↔ file`, объясняешь различие procfs/sysfs/debugfs,
правильно итерируешь коллекцию через `seq_file` (с блокировкой от `start` до `stop`),
и можешь зарегистрировать и смонтировать собственную ФС в памяти.

---

## 1. Зачем нужен VFS: «всё есть файл»

### 1.1 Проблема, которую решает VFS

В F2 ты вызывал `open("/home/file", ...)`, `read(fd, ...)`, `write(fd, ...)` — и они
работали одинаково для файла на диске, для пайпа, для устройства в `/dev`. Но за этими
тремя вызовами стоят **совершенно разные** реализации: ext4 читает блоки с SSD, tmpfs
держит данные в RAM, твой драйвер из K1 гонял байты в устройство, а `/proc/cpuinfo`
вообще не существует как файл — он генерируется на лету.

Если бы каждая подсистема имела свой собственный API, userspace утонул бы. Вместо этого
ядро вводит **общий слой абстракции** — VFS (Virtual File System). Идея ровно как у
таблицы функций-указателей из F1 (`struct file_operations` — это и есть dispatch table):
VFS определяет **набор операций** («прочитать», «записать», «найти по имени»), а каждая
конкретная ФС **подставляет свои реализации**. Системный вызов попадает в VFS, VFS
смотрит, какой ФС принадлежит файл, и вызывает нужную функцию через указатель.

```text
            userspace
   open()   read()   write()   stat()   mkdir()
      │        │        │         │        │
══════╪════════╪════════╪═════════╪════════╪══════  граница ядра (syscall)
      ▼        ▼        ▼         ▼        ▼
   ┌─────────────────────────────────────────┐
   │                 VFS                       │  единый интерфейс
   │  (struct file_operations / inode_ops /…) │
   └───┬───────────┬───────────┬───────────┬──┘
       ▼           ▼           ▼           ▼
     ext4        tmpfs       procfs     твой драйвер
   (диск)        (RAM)     (на лету)    (устройство)
```

Это и есть смысл фразы «в Unix всё есть файл»: не то, что всё лежит на диске, а то, что
к **очень разным** сущностям применяется **один** интерфейс. VFS — место, где этот
единый интерфейс физически реализован.

### 1.2 Что VFS даёт тебе как автору драйвера

Когда в K1 ты заполнял `struct file_operations` и звал `misc_register`, ты уже
программировал VFS — просто с одной стороны. VFS брал твою таблицу `read`/`write` и
вызывал её, когда userspace обращался к `/dev/...`. В этом модуле ты используешь те же
механизмы, чтобы:

- выставить **отладочное** состояние через debugfs (раздел 4);
- отдать **список** объектов через `seq_file` в `/proc` (раздел 5);
- завести **настраиваемый параметр** через sysfs-атрибут (раздел 7);
- и даже создать **целую файловую систему** (раздел 8).

Везде принцип один: ты заполняешь таблицу операций и регистрируешь объект в VFS.

### 1.3 Путь системного вызова: от `open()` до драйвера

Разберём, что происходит при `fd = open("/sys/kernel/debug/k5_debug/status", O_RDONLY)`:

```text
1. open() → системный вызов → do_sys_open() в ядре
2. Path lookup: ядро идёт по компонентам пути "sys" → "kernel" → "debug" → ...
   Для каждого компонента ищет dentry в dcache (кэш имён). Если нет —
   просит родительский inode через inode_operations->lookup найти ребёнка.
3. Найден целевой dentry → за ним inode (метаданные файла) → за ним super_block.
4. Ядро выделяет struct file, копирует в file->f_op указатель из inode->i_fop
   (это твои file_operations).
5. Вызывает f_op->open(inode, file) — твой коллбек (если есть).
6. Возвращает в userspace файловый дескриптор (целое число fd).

Затем read(fd, buf, n):
7. fd → struct file → f_op->read (или read_iter).
8. Вызывается твоя функция чтения; она копирует данные в userspace
   (copy_to_user / simple_read_from_buffer) и двигает f_pos.
```

Каждый из выделенных объектов (`dentry`, `inode`, `super_block`, `file`) — отдельная
структура VFS со своей ролью и временем жизни. Разберём их по очереди.

---

## 2. Четыре объекта VFS

VFS держится на четырёх главных структурах. Запомни их роли — это словарь всего модуля.

| Объект | Что представляет | Сколько штук | Где определён |
|--------|------------------|--------------|---------------|
| `struct super_block` | смонтированную ФС целиком | один на монтирование | `include/linux/fs.h` |
| `struct inode` | сам файл (метаданные, без имени) | один на файл | `include/linux/fs.h` |
| `struct dentry` | имя в дереве каталогов | один на (имя в каталоге) | `include/linux/dcache.h` |
| `struct file` | открытый файловый дескриптор | один на каждый `open()` | `include/linux/fs.h` |

### 2.1 `super_block` — смонтированная файловая система

`super_block` описывает **примонтированный экземпляр** ФС: размер блока, магическое
число, флаги монтирования (`MS_RDONLY` и т.п.), указатель на корневой `dentry`
(`s_root`) и таблицу операций суперблока (`s_op`). Один `mount` → один `super_block`.
Если ты смонтировал один и тот же tmpfs дважды, будет два разных `super_block`.

Главные поля (упрощённо):

```c
struct super_block {
    unsigned long          s_magic;        // магическое число ФС (опознать тип)
    unsigned long          s_blocksize;    // размер блока в байтах
    unsigned char          s_blocksize_bits; // log2(s_blocksize)
    const struct super_operations *s_op;   // операции: statfs, drop_inode, ...
    struct dentry         *s_root;         // корневой каталог ФС
    struct list_head       s_inodes;       // все inode этой ФС
    void                  *s_fs_info;      // приватные данные ФС
    /* ... десятки других полей ... */
};
```

`super_operations` — таблица функций уровня всей ФС: как записать `inode` на носитель
(`write_inode`), как освободить `inode` (`drop_inode`/`evict_inode`), как отдать
статистику для `df` (`statfs`). Для in-memory ФС большинство берётся готовым из `libfs`
(`simple_statfs`).

### 2.2 `inode` — это и есть файл

`inode` (index node — индексный узел) представляет **сам файл**: его метаданные и связь
с данными, но **не имя**. Одно из главных прозрений модуля: **имя файла не хранится в
inode**. В inode лежит тип (файл/каталог/symlink), права, владелец, размер, времена,
счётчик жёстких ссылок и указатели на операции:

```c
struct inode {
    umode_t           i_mode;     // тип + права (S_IFDIR | 0755 и т.п.)
    unsigned long     i_ino;      // номер inode (уникален в рамках ФС)
    kuid_t            i_uid;      // владелец
    kgid_t            i_gid;
    loff_t            i_size;     // размер файла в байтах
    const struct inode_operations *i_op;   // операции над именами/метаданными
    const struct file_operations  *i_fop;  // операции над открытым файлом
    struct address_space *i_mapping;       // page cache этого файла (раздел 9)
    struct super_block   *i_sb;            // к какой ФС принадлежит
    /* времена доступа/изменения — только через accessor-ы (см. 8.5) */
    /* ... */
};
```

Два набора операций у inode:

- **`i_op` (`inode_operations`)** — про **имена и метаданные**: `lookup` (найти ребёнка
  по имени в каталоге), `create`, `mkdir`, `unlink`, `rename`, `setattr`. Это операции
  «над файловой системой как деревом».
- **`i_fop` (`file_operations`)** — про **содержимое открытого файла**: `read`, `write`,
  `open`, `release`, `llseek`. Те самые, что ты заполнял в K1.

`i_ino` — номер inode; именно его показывает `ls -i`. Тип файла определяется битами
`S_IFMT` в `i_mode`: `S_IFREG` (обычный), `S_IFDIR` (каталог), `S_IFLNK` (симлинк),
`S_IFCHR` (символьное устройство) и т.д.

### 2.3 `dentry` — имя в дереве

`dentry` (directory entry — запись каталога) связывает **строковое имя** с `inode` и
формирует **дерево** каталогов. Именно `dentry`, а не `inode`, знает, что файл
называется `status` и лежит в каталоге `k5_debug`.

```c
struct dentry {
    struct dentry      *d_parent;   // родительский каталог
    struct qstr         d_name;     // имя (строка + хэш)
    struct inode       *d_inode;    // на какой inode указывает (может быть NULL!)
    struct super_block *d_sb;       // к какой ФС
    const struct dentry_operations *d_op;
    /* ... поля для хэш-таблицы dcache, RCU, refcount ... */
};
```

Зачем отдельная структура для имени? Две причины:

1. **Жёсткие ссылки (hard links).** Один файл (один `inode`) может иметь несколько
   имён. `ln a b` создаёт второй `dentry` (`b`), указывающий на **тот же** `inode`, что
   и `a`. Счётчик `i_nlink` в inode при этом = 2. Удаление одного имени (`unlink`)
   уменьшает счётчик; файл физически исчезает, когда `i_nlink` дойдёт до 0 (и никто его
   не держит открытым). Если бы имя жило в inode, hard links были бы невозможны.
2. **Кэш путей (dcache).** Разбор пути — дорогая операция. `dentry` кэшируются в
   хэш-таблице (dentry cache, dcache), чтобы повторный `open("/usr/bin/gcc")` не
   перечитывал каталоги с диска (раздел 10).

`dentry` может быть **отрицательным** (negative dentry): `d_inode == NULL`. Это значит
«мы точно знаем, что файла с таким именем тут нет» — кэшируется, чтобы повторные
обращения к несуществующему файлу не били по ФС.

### 2.4 `file` — открытый дескриптор

`struct file` представляет **конкретное открытие** файла процессом. Создаётся при
`open()`, уничтожается при последнем `close()`. Главное его поле — **позиция чтения**
`f_pos`:

```c
struct file {
    struct path        f_path;     // dentry + vfsmount (где файл в дереве)
    struct inode      *f_inode;    // быстрый доступ к inode
    const struct file_operations *f_op;  // копия inode->i_fop
    fmode_t            f_mode;      // режим (чтение/запись)
    loff_t             f_pos;       // ТЕКУЩАЯ позиция в файле
    void              *private_data; // приватные данные открытия (часто — твои)
    /* ... */
};
```

Ключевой момент: **`f_pos` уникален для каждого `open()`**. Два процесса, открывшие один
файл, имеют **два разных** `struct file` с независимыми позициями, но указывают на
**один** `inode`. Поэтому `f_pos` — в `file`, а не в `inode`. Поле `private_data` —
твоё: туда драйвер кладёт состояние, относящееся к конкретному открытию (`seq_file`,
например, использует его под свой буфер).

### 2.5 Как всё связано

```text
   Процесс A: fd=3              Процесс B: fd=5
       │                            │
       ▼                            ▼
  ┌───────────┐               ┌───────────┐
  │struct file│ f_pos=100     │struct file│ f_pos=0     ← у каждого open() свой f_pos
  └─────┬─────┘               └─────┬─────┘
        │   f_path.dentry           │
        └───────────┬───────────────┘
                    ▼
            ┌───────────────┐
            │ struct dentry │  d_name = "status"        ← имя в дереве (dcache)
            │  ("status")   │
            └───────┬───────┘ d_inode
                    ▼
            ┌───────────────┐
            │ struct inode  │  i_ino=42, i_mode=S_IFREG ← сам файл (метаданные)
            │   (ino 42)    │  i_fop = &status_fops
            └───────┬───────┘ i_sb
                    ▼
            ┌───────────────┐
            │struct super_  │  s_magic, s_root          ← смонтированная ФС
            │   block       │
            └───────────────┘
```

Связи: `file → dentry → inode → super_block`. Несколько `file` могут указывать на один
`dentry`; несколько `dentry` (hard links) — на один `inode`; все `inode` одной ФС — на
один `super_block`.

### 2.6 Таблицы операций — везде одна идея

VFS — это набор **таблиц функций-указателей**. Ты уже знаешь приём из F1: структура с
указателями на функции = виртуальный интерфейс. Все «*_operations» устроены так:

| Таблица | На что вешается | Примеры методов |
|---------|-----------------|-----------------|
| `file_operations` | `inode->i_fop`, `file->f_op` | `read`, `write`, `open`, `release`, `llseek`, `mmap` |
| `inode_operations` | `inode->i_op` | `lookup`, `create`, `mkdir`, `unlink`, `rename` |
| `super_operations` | `super_block->s_op` | `statfs`, `drop_inode`, `evict_inode`, `put_super` |
| `dentry_operations` | `dentry->d_op` | `d_revalidate`, `d_hash`, `d_compare`, `d_delete` |
| `address_space_operations` | `inode->i_mapping->a_ops` | `read_folio`, `writepage`, `write_begin` (раздел 9) |

Заполнять надо только то, что нужно: незаданные методы VFS либо считает «не
поддерживается», либо подставляет дефолт. В упражнениях ты заполнишь `file_operations`
(01), `seq_operations` + `proc_ops` (02), `kobj_attribute` (03) и `super_operations` +
`file_system_type` (04).

### 2.7 Жизненный цикл: `open()` → `read()` → `close()` глазами VFS

```text
open("/mnt/k5fs/data", O_RDONLY):
  1. path lookup → dentry("data") → inode
  2. alloc struct file; file->f_op = inode->i_fop
  3. f_op->open(inode, file)   // твой коллбек, если задан
  4. вернуть fd

read(fd, buf, 4096):
  5. fd → struct file
  6. f_op->read_iter(file, ...) // твой коллбек
  7. данные → copy_to_user; f_pos += прочитано
  8. вернуть число байт

close(fd):
  9. если это последняя ссылка на file → f_op->release(inode, file)
 10. освободить struct file
```

`open`/`release` зовутся на **каждое** открытие/закрытие, `read`/`write` — на каждую
операцию. `release` — место для очистки `private_data`. Это та же модель, что в K1, —
теперь ты видишь её целиком.

### 2.8 Типы файлов в `i_mode`

Тип файла закодирован старшими битами `i_mode` (маска `S_IFMT`). Один inode — один тип;
поведение VFS зависит от него:

| Константа | Тип | Что значит |
|-----------|-----|------------|
| `S_IFREG` | обычный файл | данные через page cache |
| `S_IFDIR` | каталог | в нём `lookup`/`create`, итерация `readdir` |
| `S_IFLNK` | символическая ссылка | `i_op->get_link` отдаёт цель |
| `S_IFCHR` | символьное устройство | open перенаправляется драйверу (K1) |
| `S_IFBLK` | блочное устройство | I/O блоками |
| `S_IFIFO` | именованный канал (FIFO) | pipe-семантика |
| `S_IFSOCK`| сокет | сетевой/локальный сокет |

Проверки: `S_ISDIR(m)`, `S_ISREG(m)`, `S_ISLNK(m)` и т.д. В упражнении 04 мы ставим
`S_IFDIR | 0755`: старшие биты — «это каталог», младшие `0755` — права. Именно из-за
типа `S_IFDIR` ядро применяет к корню `simple_dir_operations` (итерация), а не операции
обычного файла.

### 2.9 Монтирование: `mount`, `vfsmount` и namespaces

Между «ФС» и «местом в дереве» есть ещё один объект — **`struct vfsmount`** (точка
монтирования). Один `super_block` может быть смонтирован в **несколько** мест (bind
mount) — каждому соответствует свой `vfsmount`, но `super_block` (а значит, и данные)
общий. Поэтому путь в ядре носят парой `struct path { vfsmount, dentry }`: dentry говорит
«какой узел дерева», vfsmount — «в каком монтировании» (важно при пересечении границ ФС).

Поверх этого работают **mount namespaces**: у контейнера может быть своё дерево
монтирований, не видимое хосту. Это база изоляции (Docker и пр.): тот же механизм VFS,
но с приватным набором `vfsmount`. Для драйвера это фон, но он объясняет, почему «путь» —
это не просто dentry, а пара с монтированием.

---

## 3. Псевдо-файловые системы

### 3.1 Что значит «псевдо»

Псевдо-ФС (pseudo filesystem) — это файловая система, у которой **нет носителя**: её
файлы не лежат на диске и не занимают блоки. Содержимое **генерируется ядром на лету**
при чтении и интерпретируется при записи. `cat /proc/uptime` не читает файл — он зовёт
функцию ядра, которая печатает текущий uptime в буфер. Запись `echo 1 >
/proc/sys/net/ipv4/ip_forward` не пишет на диск — она дёргает обработчик, меняющий
переменную ядра.

Зачем это нужно: псевдо-ФС — **универсальный способ говорить с ядром текстом**, без
новых системных вызовов. Хочешь выставить счётчик наружу — заведи файл, а не syscall.
Это и дёшево (нет нового ABI), и удобно (`cat`/`echo`/`grep` работают сразу).

Три главные псевдо-ФС, которые должен знать драйверописатель: **procfs**, **sysfs**,
**debugfs**. Есть и другие (tmpfs — в памяти, но с реальными данными; configfs — как
sysfs, но объекты создаёт userspace; tracefs — для ftrace), но эти три — основные.

### 3.2 procfs vs sysfs vs debugfs — сравнение

| Свойство | **procfs** (`/proc`) | **sysfs** (`/sys`) | **debugfs** (`/sys/kernel/debug`) |
|----------|----------------------|--------------------|-----------------------------------|
| Назначение | процессы (`/proc/PID`), легаси-параметры, `/proc/sys` (sysctl) | модель устройств: драйверы, шины, питание | **отладка**: любые дампы для разработчика |
| Правило формата | исторически свалка; формат произвольный | строго **«один файл — одно значение»** (ASCII) | правил нет: текст, бинарь, что угодно |
| Стабильность ABI | для старых файлов — стабильна (ломать нельзя) | стабильна (userspace на неё завязан) | **нестабилен**: можно менять/удалять между версиями |
| Доступ | часто читаемо всем | чтение всем, запись — настройки | по умолчанию только **root** (mode 0700 на корне) |
| Главный API | `proc_create` + `proc_ops` | `kobject` + атрибуты | `debugfs_create_*` |
| Создавать новое? | **не рекомендуется** (кроме `/proc/sys`) | да, для параметров устройств | да, для отладки |

Коротко, как выбирать:

- **debugfs** — «мне, разработчику, посмотреть внутренности». Дамп регистров, счётчики,
  флаг включить трассировку. Никто в проде на это не завязывается → можно ломать.
- **sysfs** — «штатный параметр устройства, на который завязан userspace» (яркость
  экрана, скорость вентилятора). Один файл — одно значение, стабильно навсегда.
- **procfs** — «легаси и информация о процессах». Новое сюда почти не добавляют; для
  настроек ядра есть `/proc/sys` (sysctl), но это отдельная инфраструктура.

> **Историческая справка.** debugfs появилась в ядре **2.6.10–2.6.11** (2005), автор —
> Greg Kroah-Hartman, ровно как «место без правил» для отладки, чтобы разработчики
> перестали засорять `/proc` своими времянками. [ПРОВЕРИТЬ при ревью точную минорную
> версию: 2.6.10 vs 2.6.11.]

### 3.3 Почему в sysfs «один файл — одно значение»

Это не каприз, а контракт. sysfs читается **скриптами и программами**, а не только
глазами. Если файл содержит одно значение, парсер тривиален: `cat`/`echo`, и всё.
Как только в один файл напихали таблицу — каждый потребитель пишет свой парсер, формат
застывает как ABI, и любое изменение ломает userspace. Поэтому ядро требует: атрибут
sysfs = одно скалярное значение (или простой однотипный массив), помещающееся в одну
страницу памяти (`PAGE_SIZE`). Нужна таблица — это debugfs или `seq_file` в procfs, но
не sysfs.

### 3.4 Остальные псевдо-ФС (для кругозора)

Тройка procfs/sysfs/debugfs — основная, но в системе живут и другие. Полезно узнавать:

| ФС | Точка монтирования | Назначение |
|----|--------------------|------------|
| **tmpfs** | `/tmp`, `/run`, `/dev/shm` | ФС в памяти с **реальными** данными (page cache + swap); не «псевдо» в смысле генерации, но без диска |
| **ramfs** | (вручную) | предок tmpfs; данные в page cache, без ограничения размера и без swap |
| **tracefs** | `/sys/kernel/tracing` | интерфейс ftrace; родственница debugfs (раньше жила внутри неё) — мост в K7 |
| **configfs** | `/sys/kernel/config` | «обратная sysfs»: объекты ядра **создаёт userspace** через `mkdir` |
| **cgroupfs** | `/sys/fs/cgroup` | управление контрольными группами (лимиты CPU/памяти) |
| **bpffs** | `/sys/fs/bpf` | хранение pinned eBPF-объектов |

`k5fs` из упражнения 04 ближе всего к **ramfs** — корень в памяти на готовых кирпичах
`libfs`. tmpfs — это «ramfs + учёт размера + swap», эталон того, как in-memory ФС
дорастает до продакшена (`fs/ramfs/` и `mm/shmem.c` в исходниках).

### 3.5 Дерево выбора интерфейса

Короткий алгоритм «куда выставлять» для автора драйвера:

```text
Что выставляем наружу?
│
├─ Отладочное/временное (дамп, счётчик «для меня»)?
│     → debugfs  (правил нет, root-only, можно ломать между версиями)
│
├─ Штатный параметр устройства, на который завяжется userspace?
│     → sysfs-атрибут  (одно значение/файл, стабильный ABI)
│        много атрибутов → attribute_group / .default_groups
│        двоичный дамп   → bin_attribute
│
├─ Список/таблица переменной длины?
│     → seq_file  (в debugfs или, для легаси-совместимости, в /proc)
│        держать лок от start до stop
│
├─ Настройка ядра «как sysctl»?
│     → register_sysctl  (поддерево /proc/sys)
│
└─ Полноценная иерархия файлов/каталогов с данными?
      → своя ФС (file_system_type, fs_context, fill_super) или tmpfs/ramfs
```

Большинство драйверов используют **debugfs для отладки** и **sysfs для параметров**;
своя ФС — редкость (специальные подсистемы вроде cgroup, pstore), но понимать её
устройство обязан каждый, кто хочет читать ядро.

---

## 4. debugfs — отладочный интерфейс драйвера

debugfs — самый быстрый способ выставить состояние драйвера наружу. Это и есть основное
задание модуля по треку («сделай debugfs-интерфейс к своему драйверу»), поэтому
разберём его подробно — это **упражнение 01**.

### 4.1 Где живёт и как смонтирована

debugfs обычно монтируется ядром автоматически в `/sys/kernel/debug`. Если нет —
вручную: `mount -t debugfs none /sys/kernel/debug`. Корень имеет права 0700 — заглянуть
может только root. Для сборки модуля нужен `CONFIG_DEBUG_FS=y` (в дистрибутивных и в
нашем QEMU-ядре он включён).

### 4.2 Базовый API: каталоги и типизированные файлы

```c
#include <linux/debugfs.h>

struct dentry *debugfs_create_dir(const char *name, struct dentry *parent);
```

`parent == NULL` → каталог создаётся в корне debugfs. Возвращается `struct dentry *`.

Для простых значений есть **типизированные хелперы** — они сами реализуют чтение/запись,
свой код писать не нужно:

```c
void debugfs_create_u8 (const char *name, umode_t mode, struct dentry *parent, u8  *value);
void debugfs_create_u32(const char *name, umode_t mode, struct dentry *parent, u32 *value);
void debugfs_create_u64(const char *name, umode_t mode, struct dentry *parent, u64 *value);
void debugfs_create_x32(const char *name, umode_t mode, struct dentry *parent, u32 *value); // hex
void debugfs_create_bool(const char *name, umode_t mode, struct dentry *parent, bool *value);
```

`cat` такого файла печатает текущее значение переменной, `echo N >` — записывает в неё.
Идеально для счётчиков и флагов отладки.

Для произвольного формата — файл со своими `file_operations`:

```c
struct dentry *debugfs_create_file(const char *name, umode_t mode,
                                   struct dentry *parent, void *data,
                                   const struct file_operations *fops);
```

`data` будет доступен как `inode->i_private` / `file->private_data` внутри коллбеков.

### 4.3 Проверяй `IS_ERR`, а не `NULL`

Тонкость, на которой спотыкаются: современный debugfs при ошибке возвращает
**`ERR_PTR(-код)`**, а не `NULL`. Проверять надо `IS_ERR()`:

```c
dir = debugfs_create_dir("k5_debug", NULL);
if (IS_ERR(dir))
    return PTR_ERR(dir);
```

Важная философия debugfs: ошибку создания **файла можно игнорировать**. Если debugfs
выключен или файл не создался — это не повод проваливать загрузку драйвера (debugfs
не критичен для его работы). Хелперы `debugfs_create_*` спокойно принимают `ERR_PTR`
как parent и ничего не делают. Поэтому в упражнении мы проверяем только каталог.

### 4.4 Свои `file_operations` и `simple_read_from_buffer`

Файлу `status` нужен свой `read`, отдающий строку. Канон — `simple_read_from_buffer`:
он сам обрабатывает частичные чтения и EOF (конец файла), двигая `*ppos`:

```c
ssize_t simple_read_from_buffer(void __user *to, size_t count, loff_t *ppos,
                                const void *from, size_t available);
```

`to`/`count` — буфер пользователя; `from`/`available` — твой буфер в ядре; `ppos` —
позиция. Возвращает число скопированных байт; когда `*ppos >= available`, вернёт 0 (EOF).
Это избавляет от ручной возни с `copy_to_user` и смещением.

```c
static ssize_t status_read(struct file *file, char __user *ubuf,
                           size_t count, loff_t *ppos)
{
    static const char msg[] = "Driver is active\n";
    return simple_read_from_buffer(ubuf, count, ppos, msg, sizeof(msg) - 1);
}

static const struct file_operations status_fops = {
    .owner  = THIS_MODULE,
    .read   = status_read,
    .llseek = default_llseek,
};
```

`sizeof(msg) - 1` — длина без терминирующего `'\0'` (его в файл выводить не нужно).
`.owner = THIS_MODULE` обязателен: пока файл открыт, счётчик ссылок модуля не даст его
выгрузить.

### 4.5 Очистка — `debugfs_remove_recursive` (и почему без неё oops)

Самая частая и опасная ошибка с debugfs — **забыть удалить файлы при выгрузке**.
Последствие фатально: после `rmmod` файлы **остаются** в debugfs, но их `file_operations`
указывают в **уже освобождённый** код модуля. Первый же `cat` такого файла прыгнет по
невалидному адресу → **kernel oops** (по сути use-after-free на уровне исполняемого
кода функции).

Лечение — снести всё поддерево одним вызовом в `exit`:

```c
void debugfs_remove_recursive(struct dentry *dentry);
```

Передаёшь корневой `dentry`, полученный от `debugfs_create_dir`, — удаляются и каталог,
и все файлы внутри. Не нужно вести список и удалять по одному.

### 4.6 Worked: упражнение `01-debugfs` целиком

Собираем интерфейс `/sys/kernel/debug/k5_debug` с `counter` (u32, rw) и `status` (ro):

```c
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

static struct dentry *k5_dir;
static u32 k5_counter;

static ssize_t status_read(struct file *file, char __user *ubuf,
                           size_t count, loff_t *ppos)
{
    static const char msg[] = "Driver is active\n";
    return simple_read_from_buffer(ubuf, count, ppos, msg, sizeof(msg) - 1);
}

static const struct file_operations status_fops = {
    .owner  = THIS_MODULE,
    .read   = status_read,
    .llseek = default_llseek,
};

static int __init k5_init(void)
{
    k5_dir = debugfs_create_dir("k5_debug", NULL);
    if (IS_ERR(k5_dir))
        return PTR_ERR(k5_dir);

    debugfs_create_u32("counter", 0644, k5_dir, &k5_counter);
    debugfs_create_file("status", 0444, k5_dir, NULL, &status_fops);
    return 0;
}

static void __exit k5_exit(void)
{
    debugfs_remove_recursive(k5_dir);
}

module_init(k5_init);
module_exit(k5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K5: 01-debugfs");
```

Проверка в QEMU (что делает `qemu_test.sh`):

```sh
mount -t debugfs none /sys/kernel/debug
insmod cppmod.ko
test -d /sys/kernel/debug/k5_debug      # каталог есть
echo 42 > /sys/kernel/debug/k5_debug/counter
cat /sys/kernel/debug/k5_debug/counter  # → 42  (хелпер u32 сам читает/пишет)
cat /sys/kernel/debug/k5_debug/status   # → Driver is active
rmmod cppmod
test ! -d /sys/kernel/debug/k5_debug    # каталог исчез (remove_recursive)
dmesg | grep -E 'BUG|Oops|WARNING'      # пусто
```

Что внутри ты реализуешь в стартере: тело `status_read` (через `simple_read_from_buffer`),
три вызова создания в `init` и один `debugfs_remove_recursive` в `exit`. Стартер
компилируется, но `init` пустой → каталога нет → тест падает на первой проверке.

### 4.7 Передача данных в коллбеки: `i_private`

Глобальные переменные годятся для учебного примера, но реальный драйвер обслуживает
**несколько** устройств, и коллбеку нужно знать, **с каким именно** он работает.
Для этого у `debugfs_create_file` есть параметр `data` — он сохраняется в
`inode->i_private` и доступен внутри коллбеков:

```c
struct k5_dev { u32 regs[16]; const char *name; };

static ssize_t regs_read(struct file *file, char __user *ubuf,
                         size_t count, loff_t *ppos)
{
    struct k5_dev *dev = file->private_data;   // debugfs кладёт data сюда
    char buf[128];
    int len = scnprintf(buf, sizeof(buf), "%s: %u\n", dev->name, dev->regs[0]);
    return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}
/* ... */
debugfs_create_file("regs", 0444, dev_dir, dev, &regs_fops);  // data = dev
```

debugfs сам реализует `open`, который кладёт `inode->i_private` в `file->private_data`,
поэтому в коллбеке можно сразу читать `file->private_data`. `scnprintf` (в отличие от
`snprintf`) возвращает **фактически записанное** число байт — это удобно для длины.

### 4.8 Прочие хелперы debugfs

Кроме типизированных чисел и файла с `fops`, debugfs предлагает готовые формы под
частые задачи отладки:

| Хелпер | Что даёт |
|--------|----------|
| `debugfs_create_blob` | дамп фиксированного буфера (бинарь) на чтение |
| `debugfs_create_regset32` | таблица 32-битных регистров (имя=значение) |
| `debugfs_create_devm_seqfile` | seq_file-файл с managed-временем жизни (`devm_`) |
| `debugfs_create_atomic_t` | значение `atomic_t` на чтение/запись |
| `debugfs_create_symlink` | символическая ссылка внутри debugfs |

Идея debugfs — чтобы рутинные дампы не требовали ни строчки своего `read`/`write`.
Свои `file_operations` пиши лишь там, где нужен нестандартный формат.

---

## 5. seq_file — отдавать коллекции без потерь и гонок

### 5.1 Проблема сырого `.read`

Пока файл отдаёт одну короткую строку (как `status` выше), `simple_read_from_buffer`
хватает. Но как только нужно вывести **список** переменной длины (все объекты драйвера,
таблицу маршрутов, очередь), сырой `.read` превращается в боль. Причина — контракт
`read(2)`: пользователь волен вызвать `read(fd, buf, 10)` с буфером **меньше**, чем твои
данные. Тогда ты обязан:

- отдать ровно сколько влезло;
- запомнить в `*ppos`, где остановился;
- при следующем `read` продолжить с того же места;
- не разорвать запись посередине и не потерять/задвоить элемент, если коллекция
  меняется между вызовами.

Писать это руками для каждого файла — источник тонких багов. Поэтому в ядре есть
**`seq_file`** (sequence file — «последовательный файл»): инфраструктура, которая берёт
всю буферизацию и работу с `*ppos` на себя.

### 5.2 Машина состояний: `start` / `next` / `stop` / `show`

С `seq_file` ты описываешь коллекцию четырьмя функциями (`struct seq_operations`):

| Метод | Когда зовётся | Что делает | Где блокировка |
|-------|---------------|------------|----------------|
| `start(s, pos)` | в начале каждой сессии чтения | вернуть итератор на элемент `*pos` | **взять** лок |
| `next(s, v, pos)` | после каждого `show` | сдвинуть итератор, `++*pos` | — |
| `show(s, v)` | для каждого элемента | напечатать **один** элемент в `s` | — |
| `stop(s, v)` | в конце сессии | освободить ресурсы | **отпустить** лок |

`seq_file` сам копит вывод в свой буфер (растит при нужде до размера, вмещающего хотя бы
один полный элемент) и сам отдаёт его пользователю кусками нужного размера. Ты больше не
думаешь про `count` и `*ppos` — только «как пройти коллекцию» и «как напечатать
элемент».

Печать внутри `show` — через специальные функции, пишущие в `seq_file`:

```c
int  seq_printf(struct seq_file *s, const char *fmt, ...);
int  seq_puts(struct seq_file *s, const char *str);
int  seq_putc(struct seq_file *s, char c);
```

### 5.3 Готовые итераторы по спискам

Для коллекции на `list_head` (двусвязный список ядра, см. K2) есть готовые итераторы —
не нужно вручную считать позиции:

```c
struct list_head *seq_list_start(struct list_head *head, loff_t pos);
struct list_head *seq_list_next(void *v, struct list_head *head, loff_t *pos);
```

`seq_list_start` отматывает `pos` элементов от головы; `seq_list_next` отдаёт следующий
и инкрементит `*pos`. В `show` из полученного `list_head *` достаёшь свой узел через
`list_entry`.

### 5.4 Блокировка — от `start` до `stop` (иначе use-after-free)

**Самое важное правило раздела.** Коллекцию надо защитить от изменения **на всё время
итерации**: лок берётся в `start` и отпускается в `stop`. Это не интуитивно — хочется
взять лок прямо в `show` (где ты трогаешь элемент) и сразу отпустить. **Так нельзя.**

Почему: `seq_file` зовёт `start` → `show` → `next` → `show` → `next` → ... → `stop`.
Если лок брать только в `show`, то в окне **между** `show` одного элемента и `next`
другой поток успеет `kfree` следующий узел. `next`/`show` шагнут в освобождённую память
→ **use-after-free**. Держа лок от `start` до `stop`, ты гарантируешь, что коллекция не
изменится за всю сессию чтения.

Отсюда же — ограничение: раз в `start..stop` держится лок, внутри `show`/`next`
**нельзя долго спать**. Поэтому мьютекс (можно спать в `start`, пока ждём лок, но не
внутри секции под нагрузкой) допустим, а вот вызывать в `show` что-то, что блокируется
надолго или ждёт того же лока, — путь к дедлоку. Для коротких текстовых выводов это не
проблема: `seq_printf` копирует в буфер ядра, в userspace ничего не уходит до `stop`.

### 5.5 `single_open` — когда значение одно

Если файл отдаёт **не список, а один блок** текста (как `/proc/meminfo` — большой, но
формируется за один проход), не нужны четыре метода. Есть упрощённый путь `single_open`:
ты пишешь одну функцию `show`, которая печатает всё сразу, а `single_open` оборачивает
её в `seq_file` с одним «элементом»:

```c
static int info_show(struct seq_file *s, void *v) {
    seq_printf(s, "requests: %lu\n", atomic_long_read(&reqs));
    return 0;
}
static int info_open(struct inode *i, struct file *f) {
    return single_open(f, info_show, NULL);  // парный single_release
}
```

Для коллекции переменной длины (упражнение 02) используем полноценные четыре метода и
`seq_open`/`seq_release`.

### 5.6 Worked: упражнение `02-procfs` (seq_file + список под мьютексом)

`/proc/k5_list`: запись добавляет строку в список, чтение перечисляет строки. Это
объединяет seq_file (раздел 5) и procfs (раздел 6). Полное решение:

```c
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>

struct k5_node { struct list_head list; char msg[64]; };

static LIST_HEAD(k5_list);
static DEFINE_MUTEX(k5_lock);

static void *k5_seq_start(struct seq_file *s, loff_t *pos)
{
    mutex_lock(&k5_lock);                  // лок берём здесь
    return seq_list_start(&k5_list, *pos);
}
static void *k5_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    return seq_list_next(v, &k5_list, pos);
}
static void k5_seq_stop(struct seq_file *s, void *v)
{
    mutex_unlock(&k5_lock);                // и отпускаем здесь
}
static int k5_seq_show(struct seq_file *s, void *v)
{
    struct k5_node *n = list_entry(v, struct k5_node, list);
    seq_printf(s, "%s\n", n->msg);
    return 0;
}
static const struct seq_operations k5_seq_ops = {
    .start = k5_seq_start, .next = k5_seq_next,
    .stop  = k5_seq_stop,  .show = k5_seq_show,
};

static int k5_proc_open(struct inode *inode, struct file *file)
{
    return seq_open(file, &k5_seq_ops);
}

static ssize_t k5_proc_write(struct file *file, const char __user *ubuf,
                             size_t count, loff_t *ppos)
{
    struct k5_node *n;
    size_t len = min(count, sizeof(n->msg) - 1);

    n = kmalloc(sizeof(*n), GFP_KERNEL);
    if (!n)
        return -ENOMEM;
    if (copy_from_user(n->msg, ubuf, len)) {
        kfree(n);
        return -EFAULT;
    }
    n->msg[len] = '\0';
    if (len && n->msg[len - 1] == '\n')   // срезать перевод строки от echo
        n->msg[len - 1] = '\0';
    mutex_lock(&k5_lock);
    list_add_tail(&n->list, &k5_list);
    mutex_unlock(&k5_lock);
    return count;
}

static const struct proc_ops k5_proc_ops = {
    .proc_open    = k5_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = seq_release,
    .proc_write   = k5_proc_write,
};

static int __init k5_init(void)
{
    if (!proc_create("k5_list", 0666, NULL, &k5_proc_ops))
        return -ENOMEM;
    return 0;
}
static void __exit k5_exit(void)
{
    struct k5_node *n, *tmp;
    remove_proc_entry("k5_list", NULL);          // сначала убрать файл
    list_for_each_entry_safe(n, tmp, &k5_list, list) {  // потом освободить узлы
        list_del(&n->list);
        kfree(n);
    }
}
module_init(k5_init);
module_exit(k5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K5: 02-procfs");
```

Разбор узких мест:

- **`open` оборачивает `seq_operations`**, а `read`/`lseek`/`release` берутся
  библиотечные (`seq_read`/`seq_lseek`/`seq_release`) — это стандартная связка.
- **Порядок в `exit`:** сначала `remove_proc_entry` (новых читателей не будет), потом
  освобождение узлов. Наоборот — гонка: читатель мог бы войти в `seq_start` уже после
  того, как мы начали `kfree`.
- **Срезаем `'\n'`:** `echo item1` пишет `"item1\n"`; без среза в списке окажется строка
  с переводом, и `seq_printf("%s\n")` выдаст двойной перенос.
- **Блокировка** — ровно по правилу 5.4: `start` лочит, `stop` анлочит, `write` берёт
  тот же мьютекс на `list_add_tail`.

> **Грабли с ядром 5.6+:** для procfs здесь используется `struct proc_ops` (поля
> `proc_open`, `proc_read`, ...), **не** `file_operations`. До 5.6 это была
> `file_operations`; код из старых книг (LDD3) на новом ядре не соберётся. Разделение
> сделали, чтобы proc-файлы не тащили весь интерфейс `file_operations`.

### 5.7 Как `seq_file` работает изнутри

Понимание механики снимает большинство вопросов. При `open` (`seq_open`) выделяется
`struct seq_file` с буфером в ядре и кладётся в `file->private_data`. При `read`
(`seq_read`) происходит примерно следующее:

```text
seq_read():
  если буфер пуст или исчерпан:
    s->op->start(s, &pos)        // взять итератор (и лок)
    цикл:
      s->op->show(s, v)          // напечатать элемент в буфер s
      если буфер ПЕРЕПОЛНИЛСЯ:   // элемент не влез целиком
        увеличить буфер вдвое, начать сессию заново со start
      v = s->op->next(s, v, &pos)
      пока v != NULL и есть место
    s->op->stop(s, v)            // отпустить лок
  copy_to_user(нужный кусок буфера)
```

Два важных следствия:

- **`show` может быть вызвана повторно** для одного элемента, если буфер пришлось
  растить. Поэтому `show` обязана быть **идемпотентной** и без побочных эффектов — только
  печать. Не меняй состояние в `show`.
- Если `show` обнаруживает, что место кончилось, она может вернуть `-1`/использовать
  `seq_has_overflowed(s)` — но обычно об этом заботится сам `seq_printf`, и ручной
  проверки не нужно.

`seq_printf` не отправляет ничего в userspace немедленно — он пишет в буфер ядра;
в userspace данные уходят только в конце, после `stop`. Это и делает безопасным
удержание лока на всю сессию (5.4): пока лок держится, мы не спим на `copy_to_user`.

### 5.8 Worked: разбор реального бага — лок не там

Сравним правильный и сломанный варианты, чтобы баг 5.4 стал осязаемым.

**Неправильно** (лок в `show`):

```c
static void *bad_start(struct seq_file *s, loff_t *pos)
{
    return seq_list_start(&k5_list, *pos);   // лока НЕТ
}
static int bad_show(struct seq_file *s, void *v)
{
    struct k5_node *n = list_entry(v, struct k5_node, list);
    mutex_lock(&k5_lock);                     // взяли...
    seq_printf(s, "%s\n", n->msg);
    mutex_unlock(&k5_lock);                   // ...и сразу отпустили
    return 0;
}
```

Окно гонки: между `bad_show` (отпустил лок) и `bad_next` другой поток вызывает
`k5_proc_write`/удаление и делает `kfree(n_next)`. Затем `seq_list_next` идёт по
`n->list.next`, который указывает в **освобождённую** память. Под KASAN это выглядит так:

```text
BUG: KASAN: slab-use-after-free in seq_list_next+0x.../0x...
Read of size 8 at addr ffff8881... by task cat/123
 Call Trace:
  seq_list_next
  k5_seq_next
  seq_read_iter
  ...
Freed by task 124:
  kfree
  k5_proc_write
```

**Правильно** — лок в `start`/`stop` (как в 5.6): коллекция заморожена на всю сессию,
гонки нет. Это и есть причина, почему `seq_operations` разделены на `start`/`stop`:
чтобы было **куда** повесить взятие и снятие блокировки вокруг всей итерации.

---

## 6. procfs — историческое окно в ядро

### 6.1 Что живёт в `/proc`

`/proc` исторически — первое «окно» в ядро. Там два рода содержимого:

- **Информация о процессах:** `/proc/<PID>/` — по каталогу на процесс (`status`, `maps`,
  `fd/`, `cmdline`). Отсюда `ps`, `top`, отладчики берут данные.
- **Системная информация и настройки:** `/proc/cpuinfo`, `/proc/meminfo`,
  `/proc/filesystems`, и поддерево `/proc/sys` — это **sysctl** (настройки ядра,
  `sysctl net.ipv4.ip_forward=1` ≡ `echo 1 > /proc/sys/net/ipv4/ip_forward`).

### 6.2 `proc_create` и `proc_ops`

Создать запись в `/proc`:

```c
struct proc_dir_entry *proc_create(const char *name, umode_t mode,
                                   struct proc_dir_entry *parent,
                                   const struct proc_ops *proc_ops);
struct proc_dir_entry *proc_mkdir(const char *name, struct proc_dir_entry *parent);
void remove_proc_entry(const char *name, struct proc_dir_entry *parent);
```

`parent == NULL` → корень `/proc`. Возвращает `NULL` при ошибке (в отличие от debugfs с
его `ERR_PTR`!). `proc_ops` — таблица, аналогичная `file_operations`, но с префиксами
`proc_` и только нужными procfs полями (см. грабли в 5.6).

### 6.3 Когда НЕ нужно лезть в `/proc`

Правило простое: **новые файлы в корень `/proc` добавлять не стоит**. Это легаси-зона,
исторически ставшая свалкой; именно поэтому когда-то и появилась sysfs (структурировать
параметры устройств) и debugfs (для отладки). Уместные сегодня поводы тронуть procfs:

- информация о процессах (но это инфраструктура ядра, не драйвера);
- настройки через `/proc/sys` — но для них есть отдельный механизм `register_sysctl`;
- учебные примеры (как наше упражнение 02) и драйверы с исторически закреплённым
  `/proc`-интерфейсом, который нельзя сломать.

Для нового параметра устройства бери **sysfs** (раздел 7), для отладки — **debugfs**
(раздел 4). procfs изучаем, потому что его много в существующем коде и потому что на нём
удобно показать `seq_file`.

### 6.4 `/proc/sys` — это sysctl, отдельная инфраструктура

Поддерево `/proc/sys` выглядит как обычные proc-файлы, но за ним стоит **отдельный**
механизм — **sysctl**. Настройки ядра (сетевой стек, VM, планировщик) регистрируются
таблицами `struct ctl_table` через `register_sysctl`, а не `proc_create`:

```c
static struct ctl_table k5_sysctl[] = {
    {
        .procname     = "k5_threshold",
        .data         = &k5_threshold,        // переменная ядра
        .maxlen       = sizeof(int),
        .mode         = 0644,
        .proc_handler = proc_dointvec,        // готовый обработчик int
    },
};
/* в init: */
k5_hdr = register_sysctl("k5", k5_sysctl);    // → /proc/sys/k5/k5_threshold
/* в exit: */
unregister_sysctl_table(k5_hdr);
```

Готовые обработчики (`proc_dointvec`, `proc_dointvec_minmax`, `proc_dostring`) сами
парсят значение, проверяют границы и обновляют переменную. То есть для **настроек**
ядра нужен `register_sysctl`, для **информации** — `proc_create`+`seq_file`, и оба
живут в `/proc`, но по разным API. Это частый источник путаницы — держи различие в
голове.

---

## 7. sysfs и модель устройств ядра

### 7.1 `kobject` — атом модели устройств

sysfs (`/sys`) — это проекция **объектов ядра** (`struct kobject`) в дерево каталогов.
**Каждый `kobject` — это каталог в sysfs.** `kobject` — базовый «класс» (в смысле ООП
на C): он несёт имя, ссылку на родителя (что задаёт положение в дереве `/sys`), счётчик
ссылок и тип. Драйверы редко используют `kobject` напрямую — обычно он встроен в
`struct device`, `struct kobj_attribute` и т.п. Но понимать его надо, потому что вся
модель устройств стоит на нём.

```c
struct kobject {
    const char            *name;     // имя = имя каталога в sysfs
    struct kobject        *parent;   // родитель = родительский каталог
    struct kset           *kset;     // группа однотипных объектов
    const struct kobj_type *ktype;   // тип: операции show/store + release
    struct kref            kref;     // счётчик ссылок
    /* ... */
};
```

### 7.2 `kset`, `ktype` и иерархия

- **`ktype` (`kobj_type`)** — «класс» объекта: задаёт `sysfs_ops` (как читать/писать
  атрибуты), список атрибутов по умолчанию и, **критично, `release`** — функцию, которую
  ядро зовёт, когда счётчик ссылок дойдёт до нуля, чтобы освободить память объекта.
- **`kset`** — коллекция родственных `kobject` (например, все блочные устройства). Даёт
  им общий каталог и участвует в рассылке uevent (события в udev).

Иерархия `/sys` (`/sys/devices`, `/sys/class`, `/sys/bus`) построена целиком из
`kobject`, связанных через `parent`/`kset`.

### 7.3 Счётчик ссылок: `kref` и почему `kobject_put`, а не `kfree`

`kobject` живёт по **счётчику ссылок** (`kref`). При создании счётчик = 1. Пока кто-то
держит ссылку (например, открыт файл атрибута), объект не должен исчезнуть. Поэтому
освобождать `kobject` **прямым `kfree` нельзя** — это use-after-free, если параллельно
идёт чтение sysfs. Правильно:

```c
void kobject_put(struct kobject *kobj);  // --kref; на нуле зовёт ktype->release
```

`kobject_put` уменьшает счётчик; когда он достигает нуля, ядро само вызывает
`ktype->release` (освобождение памяти) и убирает каталог. Симметрично `kobject_get`
увеличивает счётчик. Это та же дисциплина refcounting, что у `struct file` и `dentry`.

### 7.4 Атрибуты: `show` / `store`

Файлы внутри каталога `kobject` — это **атрибуты**. У атрибута два коллбека:

- **`show`** (чтение) — печатает значение в буфер;
- **`store`** (запись) — разбирает значение из буфера.

Для атрибутов, висящих прямо на `kobject`, тип коллбеков — `kobj_attribute`:

```c
static ssize_t mode_level_show(struct kobject *kobj, struct kobj_attribute *attr,
                               char *buf);
static ssize_t mode_level_store(struct kobject *kobj, struct kobj_attribute *attr,
                                const char *buf, size_t count);
```

Собирается макросом `__ATTR(имя, права, show, store)` (или `__ATTR_RW(имя)` —
сокращение, требующее функций `имя_show`/`имя_store`):

```c
static struct kobj_attribute mode_level_attr =
    __ATTR(mode_level, 0664, mode_level_show, mode_level_store);
```

> Для устройств (`struct device`) используется родственный макрос `DEVICE_ATTR_RW(name)`
> и тип `device_attribute` — та же идея, другой контейнер. В упражнении мы работаем с
> «голым» `kobject`, поэтому `kobj_attribute`/`__ATTR`.

### 7.5 Правило «один файл — одно значение»

Повторим контракт sysfs (см. 3.3): один атрибут = одно скалярное значение, влезающее в
**одну страницу** (`PAGE_SIZE`). Не выводи таблицы и многострочные дампы — для этого есть
debugfs. `show` обязан вернуть ровно текущее значение, `store` — принять ровно одно.

### 7.6 `sysfs_emit` вместо `sprintf`

Буфер, который ядро даёт в `show`, — ровно `PAGE_SIZE`. Чтобы не переполнить его, в
sysfs-методах используют **`sysfs_emit`** (и `sysfs_emit_at` для дописывания):

```c
int sysfs_emit(char *buf, const char *fmt, ...);
```

`sysfs_emit` знает про границу страницы и про то, что вывод начинается с начала буфера;
он безопаснее голого `sprintf`, который слепо пишет сколько попросили. Для разбора
ввода в `store` — семейство `kstrtoX` (в ядре **нет** `atoi`):

```c
int kstrtoint(const char *s, unsigned int base, int *res);   // 0 = ок, <0 = ошибка
int kstrtoul (const char *s, unsigned int base, unsigned long *res);
int kstrtobool(const char *s, bool *res);
```

`kstrtoint` сам отрезает хвостовой `'\n'`, ловит переполнение и мусор, возвращая
`-EINVAL`/`-ERANGE`. На ошибку драйвер должен вернуть её из `store` — тогда `write(2)`
из userspace получит ошибку, а не молча проглотит мусор.

### 7.7 Worked: упражнение `03-sysfs`

`/sys/kernel/k5_device/mode_level` — целое на чтение/запись по правилам sysfs:

```c
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/kernel.h>
#include <linux/string.h>

static struct kobject *k5_kobj;
static int k5_mode;

static ssize_t mode_level_show(struct kobject *kobj, struct kobj_attribute *attr,
                               char *buf)
{
    return sysfs_emit(buf, "%d\n", k5_mode);     // безопасная печать
}
static ssize_t mode_level_store(struct kobject *kobj, struct kobj_attribute *attr,
                                const char *buf, size_t count)
{
    int ret = kstrtoint(buf, 10, &k5_mode);      // разбор с проверкой
    if (ret < 0)
        return ret;                              // вернуть ошибку наверх
    return count;
}
static struct kobj_attribute mode_level_attr =
    __ATTR(mode_level, 0664, mode_level_show, mode_level_store);

static int __init k5_init(void)
{
    int ret;

    k5_kobj = kobject_create_and_add("k5_device", kernel_kobj);  // /sys/kernel/k5_device
    if (!k5_kobj)
        return -ENOMEM;
    ret = sysfs_create_file(k5_kobj, &mode_level_attr.attr);
    if (ret) {
        kobject_put(k5_kobj);                    // откат: refcount, не kfree
        return ret;
    }
    return 0;
}
static void __exit k5_exit(void)
{
    kobject_put(k5_kobj);                         // освобождение по refcount
}
module_init(k5_init);
module_exit(k5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K5: 03-sysfs");
```

Ключевые места: `kobject_create_and_add(name, kernel_kobj)` создаёт каталог в
`/sys/kernel/`; `sysfs_create_file` вешает атрибут; в `exit` — `kobject_put` (refcount),
**не** `kfree`. Тест пишет `777`, читает обратно, затем пишет нечисло и ждёт **ошибку**
записи (её обеспечивает `kstrtoint`), после `rmmod` проверяет, что каталог исчез.

### 7.8 Группы атрибутов

Когда атрибутов много, их регистрируют не по одному, а **группой**
(`struct attribute_group` + `sysfs_create_group`), или декларативно через
`.default_groups` у `ktype`/`device`. Это снимает ручной откат при ошибке и гарантирует
атомарность (все появились/исчезли вместе). В упражнении один атрибут, поэтому
`sysfs_create_file` достаточно; в реальном драйвере предпочитай группы.

Декларативно это выглядит так — и снимает ручной откат при ошибке:

```c
static struct attribute *k5_attrs[] = {
    &mode_level_attr.attr,
    &threshold_attr.attr,
    NULL,                          // массив завершается NULL
};
static const struct attribute_group k5_group = {
    .attrs = k5_attrs,
};
/* в init: */
ret = sysfs_create_group(k5_kobj, &k5_group);   // все атрибуты разом
/* в exit: */
sysfs_remove_group(k5_kobj, &k5_group);
```

Либо ещё лаконичнее — через `.default_groups` у `ktype`/`device`: тогда ядро создаёт и
удаляет атрибуты автоматически вместе с самим объектом, и явные create/remove не нужны.

### 7.9 uevent — события для userspace

Когда `kobject` появляется или исчезает, ядро может отправить **uevent** —
сообщение в userspace (его слушает `udev`/`systemd-udevd`), на основе которого создаются
ноды в `/dev`, применяются правила и т.д. `kobject_create_and_add` уже шлёт `KOBJ_ADD`.
Драйвер может слать свои события вручную:

```c
kobject_uevent(k5_kobj, KOBJ_CHANGE);   // «что-то изменилось» — userspace перечитает
```

Это объясняет, почему sysfs — не просто «файлики»: это ещё и канал нотификаций о
горячем подключении/изменении устройств. Для учебного `k5_device` uevent не нужен, но в
реальном драйвере именно так userspace узнаёт о новом устройстве.

### 7.10 Двоичные атрибуты

Правило «один файл — одно значение» — про **текстовые** атрибуты. Для сырых двоичных
данных (прошивки, EEPROM-дампы, регионы памяти устройства) есть отдельный тип —
`bin_attribute` с методами `read`/`write`, принимающими смещение и размер, и
`sysfs_create_bin_file`. Это законное исключение: бинарь не парсят как «значение»,
его читают как поток байт. Не путай с попыткой запихнуть текстовую таблицу в обычный
атрибут — вот это запрещено.

---

## 8. Своя файловая система

Вершина модуля — зарегистрировать **собственную ФС** и смонтировать её. Это упражнение
04 («амбициозная» часть задания трека). Сделаем in-memory ФС `k5fs` без диска.

### 8.1 `file_system_type` — паспорт типа ФС

ФС как **тип** описывается `struct file_system_type` и регистрируется один раз:

```c
int register_filesystem(struct file_system_type *fs);
int unregister_filesystem(struct file_system_type *fs);
```

После регистрации имя появляется в `/proc/filesystems`, и ядро знает, что делать на
`mount -t k5fs ...`.

```c
struct file_system_type {
    const char *name;                              // "k5fs" — для mount -t
    int         fs_flags;
    int       (*init_fs_context)(struct fs_context *);  // точка входа монтирования
    void      (*kill_sb)(struct super_block *);    // снос суперблока при umount
    struct module *owner;                          // THIS_MODULE
    /* ... */
};
```

### 8.2 Монтирование: эволюция `mount` → `fs_context`

Исторически у `file_system_type` было поле `mount` (а ещё раньше — `get_sb`), и in-memory
ФС вызывали `mount_nodev(...)`. В современных ядрах механизм монтирования переписан на
**`fs_context`** (контекст монтирования): теперь точка входа — `init_fs_context`, а сам
`mount(2)` не зовёт твою функцию напрямую. Цепочка такая:

```text
mount(2) → init_fs_context()        // ты ставишь fc->ops
         → fc->ops->get_tree()      // ты зовёшь get_tree_nodev(fc, fill_super)
         → get_tree_nodev()         // ядро выделяет анонимный super_block
         → fill_super()             // ТЫ заполняешь суперблок и создаёшь корень
```

> **Важно для современных ядер:** старый `mount_nodev` в актуальном дереве **отсутствует**
> — используется `get_tree_nodev`. Код из LDD3/старых статей с `.mount = ...` и
> `mount_nodev` **не соберётся**. Это ровно тот случай, когда «похоже на рабочий код»
> обманывает: сверяйся с `include/linux/fs_context.h` своего ядра.

Обвязка контекста минимальна:

```c
static int k5fs_get_tree(struct fs_context *fc)
{
    return get_tree_nodev(fc, k5fs_fill_super);   // nodev = ФС без блочного устройства
}
static const struct fs_context_operations k5fs_ctx_ops = {
    .get_tree = k5fs_get_tree,
};
static int k5fs_init_fs_context(struct fs_context *fc)
{
    fc->ops = &k5fs_ctx_ops;
    return 0;
}
```

### 8.3 `fill_super` — собрать суперблок и корень

Вся настоящая работа — в `fill_super`: заполнить поля суперблока, создать **корневой
inode** (каталог) и привязать его к **корневому dentry** через `d_make_root`.

```c
static int k5fs_fill_super(struct super_block *sb, struct fs_context *fc)
{
    struct inode *inode;

    sb->s_magic          = K5FS_MAGIC;     // опознавательное число ФС
    sb->s_blocksize      = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT;
    sb->s_op             = &k5fs_sops;      // операции суперблока (из libfs)
    sb->s_time_gran      = 1;

    inode = new_inode(sb);                  // выделить inode
    if (!inode)
        return -ENOMEM;
    inode->i_ino  = 1;
    inode->i_mode = S_IFDIR | 0755;         // это КАТАЛОГ
    simple_inode_init_ts(inode);            // времена (только через accessor!)
    inode->i_op  = &simple_dir_inode_operations;  // lookup и пр. — из libfs
    inode->i_fop = &simple_dir_operations;        // итерация каталога — из libfs
    set_nlink(inode, 2);                    // у каталога nlink >= 2 (. и ..)

    sb->s_root = d_make_root(inode);        // корневой dentry из корневого inode
    if (!sb->s_root)
        return -ENOMEM;                     // d_make_root сам сделает iput при ошибке
    return 0;
}
```

### 8.4 `libfs` — готовые кирпичи для простых ФС

Ты не пишешь lookup/readdir с нуля: в `fs/libfs.c` есть готовые реализации для
in-memory ФС. Используем:

- **`simple_dir_inode_operations`** — `inode_operations` каталога (lookup и т.п.);
- **`simple_dir_operations`** — `file_operations` каталога (итерация содержимого);
- **`simple_statfs`** — отдать статистику для `df`.

`super_operations` для нашей ФС сводятся к одной строке:

```c
static const struct super_operations k5fs_sops = {
    .statfs = simple_statfs,
};
```

> Реальный эталон — `fs/ramfs/`: ramfs добавляет к этому создание файлов/каталогов
> (`ramfs_create`, привязку page cache). Наша `k5fs` ограничивается монтируемым корнем —
> этого достаточно, чтобы понять механику регистрации и суперблока.

### 8.5 Времена inode — только через accessor-ы

Важная деталь современных ядер: поля времён в `struct inode` (`i_atime`, `i_mtime`,
`i_ctime`) **больше нельзя присваивать напрямую** — их представление менялось (сжатие
`ctime`, переход на accessor-ы). Прямое `inode->i_mtime = ...` на новом ядре **не
скомпилируется**. Используй функции:

```c
struct timespec64 simple_inode_init_ts(struct inode *inode);          // инициализация всех трёх
struct timespec64 inode_set_ctime_current(struct inode *inode);       // ctime = сейчас
struct timespec64 inode_set_mtime_to_ts(struct inode *, struct timespec64);
struct timespec64 inode_set_atime_to_ts(struct inode *, struct timespec64);
```

Для нашего корня хватает `simple_inode_init_ts(inode)`.

### 8.6 `kill_anon_super` — снос при размонтировании

`get_tree_nodev` выделяет **анонимный** суперблок (без блочного устройства). Парная ему
функция сноса — **`kill_anon_super`**, её и ставим в `kill_sb`:

```c
static struct file_system_type k5fs_type = {
    .owner           = THIS_MODULE,
    .name            = "k5fs",
    .init_fs_context = k5fs_init_fs_context,
    .kill_sb         = kill_anon_super,
};
```

> ramfs использует `kill_litter_super` — она дополнительно вычищает созданные в памяти
> dentry (d_genocide). Для ФС без создаваемых файлов (только корень) достаточно
> `kill_anon_super`. Имена доступных функций зависят от ядра — сверяйся с `fs.h`.

### 8.7 Worked: упражнение `04-ramfs-mini` целиком

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/pagemap.h>

#define K5FS_MAGIC 0x6b356673  /* "k5fs" */

static const struct super_operations k5fs_sops = {
    .statfs = simple_statfs,
};

static int k5fs_fill_super(struct super_block *sb, struct fs_context *fc)
{
    struct inode *inode;

    sb->s_magic          = K5FS_MAGIC;
    sb->s_blocksize      = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT;
    sb->s_op             = &k5fs_sops;
    sb->s_time_gran      = 1;

    inode = new_inode(sb);
    if (!inode)
        return -ENOMEM;
    inode->i_ino  = 1;
    inode->i_mode = S_IFDIR | 0755;
    simple_inode_init_ts(inode);
    inode->i_op  = &simple_dir_inode_operations;
    inode->i_fop = &simple_dir_operations;
    set_nlink(inode, 2);

    sb->s_root = d_make_root(inode);
    if (!sb->s_root)
        return -ENOMEM;
    return 0;
}

static int k5fs_get_tree(struct fs_context *fc)
{
    return get_tree_nodev(fc, k5fs_fill_super);
}
static const struct fs_context_operations k5fs_ctx_ops = {
    .get_tree = k5fs_get_tree,
};
static int k5fs_init_fs_context(struct fs_context *fc)
{
    fc->ops = &k5fs_ctx_ops;
    return 0;
}
static struct file_system_type k5fs_type = {
    .owner           = THIS_MODULE,
    .name            = "k5fs",
    .init_fs_context = k5fs_init_fs_context,
    .kill_sb         = kill_anon_super,
};

static int __init k5_init(void)
{
    return register_filesystem(&k5fs_type);
}
static void __exit k5_exit(void)
{
    unregister_filesystem(&k5fs_type);
}
module_init(k5_init);
module_exit(k5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K5: 04-ramfs-mini");
```

Проверка в QEMU:

```sh
insmod cppmod.ko
grep -w k5fs /proc/filesystems     # k5fs зарегистрирована
mkdir -p /mnt/k5
mount -t k5fs none /mnt/k5         # init_fs_context → get_tree_nodev → fill_super
test -d /mnt/k5                    # корень — каталог
umount /mnt/k5
rmmod cppmod
```

В стартере `fill_super` намеренно возвращает `-ENOMEM` (корень не построен), поэтому
`mount` падает **с ошибкой**, а не в oops, — пока ты не реализуешь создание inode и
`d_make_root`.

### 8.8 Как создаются файлы внутри ФС (паттерн ramfs)

Наша `k5fs` даёт только смонтированный корень. Чтобы внутри можно было **создавать**
файлы (как tmpfs/ramfs), нужно реализовать `inode_operations` каталога с методом
`create` (и `mkdir`/`symlink` по вкусу). Схема такая:

```text
touch /mnt/k5/file   →  VFS  →  dir_inode->i_op->create(dir, dentry, mode)
                                  ├── new_inode(sb)             // создать inode файла
                                  ├── inode->i_mode = S_IFREG|... // обычный файл
                                  ├── inode->i_op  = ...        // операции файла
                                  ├── inode->i_fop = ...        // read/write файла
                                  └── d_instantiate(dentry, inode) // связать имя с inode
```

Ключевая функция — **`d_instantiate(dentry, inode)`**: она привязывает только что
созданный `inode` к `dentry`, который VFS уже выделил под новое имя (до этого dentry был
отрицательным). Для данных файла in-memory ФС опираются на page cache (страницы в RAM) и
готовые операции `ramfs`-стиля. Полный разбор — в `fs/ramfs/inode.c`; для упражнения это
ориентир «куда копать», а не требование.

### 8.9 inode: счётчик ссылок и освобождение

`inode`, как `dentry` и `kobject`, живёт по счётчику ссылок (`i_count`). Ты почти никогда
не трогаешь его руками: `new_inode` создаёт inode со ссылкой, `d_make_root`/
`d_instantiate` передают владение dentry, `iput` уменьшает счётчик. При размонтировании
`kill_anon_super` проходит и освобождает inode'ы. Важно лишь не «потерять» inode при
ошибке в `fill_super`: если после `new_inode` дальше пошла ошибка до `d_make_root`,
нужен `iput(inode)`. В нашем коде единственная ошибка после `new_inode` — провал
`d_make_root`, а он **сам** делает `iput` корня, поэтому ручной `iput` не нужен (это
документированное поведение `d_make_root`).

### 8.10 Параметры монтирования (`fs_parameter`)

Реальные ФС принимают опции: `mount -t tmpfs -o size=64M ...`. В модели `fs_context` это
делается через таблицу `fs_parameter_spec` и метод `parse_param` в
`fs_context_operations`: ядро разбирает строку опций и по очереди отдаёт тебе пары
ключ-значение. Наша `k5fs` опций не принимает, поэтому `parse_param` не задан. Но знать
про него надо: именно туда придёт `size=`, `mode=`, `uid=` и прочее, если ты решишь их
поддержать. Это ещё одна причина перехода на `fs_context`: разбор опций стал
структурированным, а не «парсь строку сам».

---

## 9. Page cache и `address_space_operations`

### 9.1 Зачем кэш страниц

Когда процесс читает файл с диска, ядро **не** идёт к диску при каждом `read`. Оно
держит содержимое файлов в **page cache** (страничный кэш) — в оперативной памяти, в
виде страниц (page, обычно 4 КиБ, см. K4 §2). Первый `read` загружает страницу с
носителя в кэш; последующие `read`/`write`, а также `mmap`, работают уже с этой
страницей в RAM. Это даёт огромный выигрыш: повторное чтение — скорость памяти, а не
диска; записи можно **откладывать** (writeback) и сливать пачкой.

Page cache — мост между тремя подсистемами: **VFS** (файлы), **MM** (память, F3/K4) и
конкретной **ФС** (как взять/положить блок на носитель).

### 9.2 `struct address_space` и `i_mapping`

Связующее звено — **`struct address_space`** (его адрес — в `inode->i_mapping`). Это
«отображение файла в страницы памяти»: набор страниц page cache, принадлежащих данному
файлу, плюс таблица операций, объясняющих ядру, как заполнить отсутствующую страницу и
как сбросить изменённую. Страницы хранятся в эффективной структуре (xarray — раньше
radix tree), индексированной по смещению в файле.

```text
   struct inode
       │ i_mapping
       ▼
   struct address_space
       ├── i_pages (xarray страниц page cache этого файла)
       │       ├── [offset 0]   → page  (в RAM)
       │       ├── [offset 1]   → page
       │       └── ...
       └── a_ops (address_space_operations)
               ├── read_folio   ← как загрузить страницу с носителя
               ├── writepage(s) ← как сбросить «грязную» страницу
               └── write_begin/write_end
```

### 9.3 `address_space_operations`: чтение и запись страниц

`a_ops` — это интерфейс «ФС ↔ page cache». Главные методы:

| Метод | Когда зовётся | Что делает |
|-------|---------------|------------|
| `read_folio` | cache miss при чтении | загрузить страницу (folio) с носителя |
| `writepage` / `writepages` | writeback | сбросить «грязную» страницу на носитель |
| `write_begin` / `write_end` | при `write` | подготовить/зафиксировать страницу под запись |
| `dirty_folio` | страницу пометили грязной | учёт грязной страницы |

Поток чтения `read(fd, buf, 4096)`:

```text
1. VFS: vfs_read() → generic_file_read_iter()
2. Page cache: ищем страницу в address_space по смещению
   ├── HIT  (страница в RAM): copy_to_user из неё → готово
   └── MISS (страницы нет):
        a_ops->read_folio()    // ФС читает блок с диска (или DMA)
        страница попадает в page cache
        copy_to_user → готово
```

Для in-memory ФС (tmpfs/ramfs) данные **уже** в page cache (носителя нет), поэтому
`read_folio` тривиален. Для дисковых ФС (ext4) `read_folio` инициирует чтение блока.

### 9.4 «Грязные» страницы и writeback

Когда процесс пишет в файл (`write`), данные сначала попадают в страницу page cache, и
страница помечается **грязной** (dirty, флаг `PG_dirty`) — на носитель они **ещё не
ушли**. Сброс («writeback») происходит позже: фоновыми потоками ядра по таймеру, при
нехватке памяти, или принудительно по `sync`/`fsync`. Тогда ядро зовёт
`a_ops->writepages`. Отсюда два важных следствия:

- `write` быстрый (память), но данные не на диске, пока не прошёл writeback или `fsync`
  — это про надёжность (вспомни F2: после `write` нужен `fsync` для гарантии);
- внезапное отключение питания теряет ещё не сброшенные грязные страницы.

### 9.5 Переход со страниц на folio

В современных ядрах в API page cache страницу всё чаще заменяет **folio** —
дескриптор, описывающий один или несколько подряд идущих фреймов как единое целое.
Поэтому новый метод называется `read_folio` (вместо старого `readpage`), а сигнатуры
оперируют `struct folio *`. Это оптимизация (меньше накладных расходов на больших
блоках), но концептуально folio — «страница или группа страниц page cache». Для нашего
in-memory упражнения это не всплывает, но в чужом коде ты увидишь оба термина.

### 9.6 Связь VFS ↔ MM

Page cache — точка, где VFS встречается с памятью из K4: страницы page cache берутся у
того же аллокатора страниц, участвуют в reclaim (вытеснении под давлением памяти),
учитываются в `/proc/meminfo` (строки `Cached`, `Dirty`, `Writeback`). Когда ты
смотрел `free -m` и видел большой `buff/cache` — это и есть page cache. Под давлением
памяти **чистые** страницы page cache можно просто отбросить (они есть на диске),
**грязные** — сначала сбросить.

### 9.7 mmap и readahead

Page cache — это ещё и то, что отображается при `mmap` (F3). `mmap` файла **не**
копирует его в адресное пространство — он связывает виртуальные страницы процесса с
**теми же** страницами page cache. Поэтому два процесса, отобразившие один файл,
разделяют физические страницы; запись одного видна другому. Это объясняет, почему `mmap`
эффективнее `read` для больших файлов: нет лишнего копирования между page cache и
буфером пользователя.

**Readahead** (упреждающее чтение) — оптимизация последовательного доступа: заметив, что
процесс читает файл подряд, ядро заранее подгружает следующие страницы в page cache,
пока процесс обрабатывает текущие. Когда дойдёт `read` до них — это уже cache hit.
Отсюда совет из C6/F2: последовательное чтение большими кусками дружелюбно к readahead,
а хаотичные мелкие `pread` по случайным смещениям его обманывают.

### 9.8 `fsync`, `O_DIRECT` и обход кэша

Два краевых случая, важных для системщика:

- **`fsync(fd)`** — заставить сбросить грязные страницы файла на носитель **прямо
  сейчас** и дождаться завершения. Без него после `write` данные живут только в page
  cache и теряются при сбое питания (см. 9.4). Базы данных и журналы зовут `fsync` в
  критических точках.
- **`O_DIRECT`** — открыть файл в обход page cache: чтение/запись идут прямо между
  диском и буфером пользователя (с жёсткими требованиями к выравниванию). Нужен тем, кто
  **сам** кэширует лучше ядра (СУБД), или чтобы не вытеснять чужой полезный кэш. Для
  большинства задач page cache — благо, и `O_DIRECT` не нужен.

---

## 10. Path lookup и dcache

### 10.1 Разбор пути покомпонентно

Превращение строки `"/usr/bin/gcc"` в `inode` называется **path lookup** (path walk).
Ядро идёт **по компонентам**: от корня (или текущего каталога) берёт `usr`, ищет его в
текущем каталоге, переходит туда, берёт `bin`, и так до `gcc`. Для каждого компонента
нужно найти `dentry` ребёнка в родительском каталоге.

### 10.2 dcache: хэш, RCU-walk и ref-walk

Поиск каждого компонента шёл бы к ФС (а та — к диску) — это дорого. Поэтому есть
**dcache** (dentry cache): хэш-таблица `dentry` по (родитель, имя). Lookup сначала бьёт
в dcache:

- **попадание** — `dentry` найден, переходим к следующему компоненту, не трогая ФС;
- **промах** — зовём `parent_inode->i_op->lookup(...)`, ФС находит ребёнка (с диска),
  создаётся новый `dentry` и кладётся в dcache.

У dcache два режима обхода:

- **RCU-walk** — быстрый путь без взятия блокировок и без инкремента счётчиков ссылок,
  под защитой RCU (см. K2). Большинство lookup проходят так.
- **ref-walk** — медленный путь со счётчиками ссылок и блокировками; включается, когда
  RCU-walk не может продолжить (например, нужен `d_revalidate` у сетевой ФС).

Это объясняет, почему `open` часто открытых путей почти бесплатен: всё в dcache, RCU-walk,
ноль обращений к ФС.

### 10.3 Отрицательные dentry

dcache кэширует и **отсутствие** файла: negative dentry (`d_inode == NULL`) означает «по
этому имени тут точно ничего нет». Это ускоряет повторные обращения к несуществующим
путям (типичный сценарий — компилятор/линкер, перебирающий каталоги в поиске заголовка).
Negative dentry вычищаются под давлением памяти.

### 10.4 Worked: трасса `open("/mnt/k5/data")`

Соберём механику разделов 2 и 10 в одну картинку — что происходит покомпонентно:

```text
open("/mnt/k5/data", O_RDONLY)
│
├─ старт: dentry корня "/" (известен из task->fs->root)
│
├─ компонент "mnt":
│    dcache hit? → да → dentry("mnt"), переходим в его inode (каталог)
│
├─ компонент "k5":
│    это точка монтирования! dentry("k5") помечен DCACHE_MOUNTED
│    → follow_mount: переходим на КОРЕНЬ смонтированной k5fs
│      (s_root нашего super_block из fill_super)
│
├─ компонент "data":
│    dcache hit? → нет (negative или отсутствует)
│    → dir_inode->i_op->lookup(dir, dentry("data"), flags)
│      ФС ищет "data"; нашла → d_instantiate; не нашла → negative dentry
│
└─ финал: есть dentry+inode "data"
     alloc struct file; f_op = inode->i_fop; f_op->open(); вернуть fd
```

Здесь видно, как **точка монтирования** склеивает две ФС: dentry `k5` в родительской ФС
«перепрыгивает» на корневой dentry (`s_root`) нашей `k5fs`. Именно поэтому `fill_super`
обязан установить `sb->s_root` — без него прыгать некуда, и монтирование не имеет смысла.

---

## 11. Галерея типичных ошибок

| Симптом / `dmesg` | Причина | Лечение |
|-------------------|---------|---------|
| oops при `cat` файла **после** `rmmod` | забыт `debugfs_remove_recursive`/`remove_proc_entry` — `fops` в выгруженном коде | удалять все узлы в `exit` |
| use-after-free под KASAN при чтении списка | лок взят в `show`, а не в `start..stop` | держать лок от `seq_start` до `seq_stop` |
| утечка памяти (kmemleak) после `rmmod` | узлы списка не освобождены в `exit` | `list_for_each_entry_safe` + `kfree` |
| `mount` падает в oops | `fill_super` вернул 0 без `sb->s_root` | всегда `d_make_root`, проверять на NULL |
| не компилируется: `mount_nodev`/`i_mtime =` | старый API на новом ядре | `get_tree_nodev`, `inode_set_*`/`simple_inode_init_ts` |
| не компилируется: `proc_ops` vs `file_operations` | procfs до 5.6 vs после | `struct proc_ops` с полями `proc_*` |
| sysfs: обрезанный/битый вывод | `sprintf` вместо `sysfs_emit`, многострочный вывод | `sysfs_emit`, одно значение на файл |
| `store` молча глотает мусор | нет проверки `kstrtoX` | вернуть код ошибки из `store` |
| `IS_ERR`-указатель разыменован | проверяли debugfs на `NULL` вместо `IS_ERR` | `IS_ERR`/`PTR_ERR` для debugfs |
| `rmmod` зависает/`-EBUSY` | ФС ещё смонтирована / файл открыт | сначала `umount`/`close`, забыт `.owner` |

### 11.1 Worked: как читать oops от забытой очистки debugfs

Сценарий: модуль создал `/sys/kernel/debug/k5_debug/status`, но в `exit` забыл
`debugfs_remove_recursive`. После `rmmod` пользователь делает `cat status`:

```text
BUG: unable to handle page fault for address: ffffffffc0a1b020
#PF: supervisor instruction fetch in kernel mode
Oops: 0010 [#1] PREEMPT SMP
RIP: 0010:0xffffffffc0a1b020          ← адрес в ВЫГРУЖЕННОМ модуле
Call Trace:
 vfs_read
 ksys_read
 __x64_sys_read
```

Как читать: `unable to handle page fault` + `instruction fetch` + `RIP` в диапазоне
`0xffffffffc0......` (область загрузки модулей) = ядро попыталось **выполнить код по
адресу, где модуля уже нет**. Это сигнатура «fops указывают в выгруженный модуль» —
ровно забытая очистка debugfs/proc. Лечение — `debugfs_remove_recursive` в `exit`. После
`rmmod` каталога быть не должно: `test ! -d /sys/kernel/debug/k5_debug` — это и проверяет
тест упражнения 01.

Сравни с oops от `seq_file` без блокировки (5.8): там `KASAN: slab-use-after-free` и
`Read of size 8` — разыменование освобождённых **данных**, а не выполнение исчезнувшего
**кода**. По типу сообщения (`page fault`/instruction fetch vs KASAN/data read) сразу
видно класс бага.

---

## 12. Сборка и практика в QEMU

Модули ядра нельзя «просто запустить» — они грузятся в работающее ядро. В курсе для
этого есть QEMU-песочница (как в K1–K4). Кратко:

- Упражнение помечено `"type": "lkm"` в манифесте; рядом с кодом лежит `qemu_test.sh`.
- Прогонщик собирает твой `solution.c` как модуль (`obj-m`), собирает дописанный
  initramfs с `cppmod.ko` и тестом и грузит ядро в QEMU; внутри гостя выполняется
  `qemu_test.sh`. Успех = скрипт вернул **0**.
- Тест грузит модуль (`insmod /mnt/share/cppmod.ko`), проверяет поведение через
  `cat`/`echo`/`mount`, выгружает (`rmmod cppmod`) и проверяет `dmesg` на
  `BUG:/Oops/WARNING:`.

Полезные приёмы отладки внутри гостя:

```sh
dmesg | tail -20                 # последние сообщения ядра (твои pr_info/ошибки)
cat /proc/filesystems            # зарегистрирована ли твоя ФС
ls -l /sys/kernel/debug/k5_debug # появились ли файлы debugfs
mount -t debugfs none /sys/kernel/debug   # если debugfs не смонтирован
```

> **Замечание про окружение.** Прогонщик собирает модуль против ядра хоста, а грузит в
> ядро образа QEMU. Если версии (`vermagic`) разойдутся, `insmod` выдаст
> `invalid module format` — это **не** ошибка твоего кода, а рассинхрон песочницы;
> чинится пересборкой образа (`scripts/qemu-setup.sh`) под текущее ядро хоста.

---

## 13. Практика и самопроверка

### 13.1 Практические задания (в редакторе курса)

1. **`01-debugfs` — debugfs-интерфейс к драйверу.** Каталог `k5_debug` с `counter`
   (u32, rw) и `status` (ro, своя `read`). Освоить `debugfs_create_*`,
   `simple_read_from_buffer`, обязательный `debugfs_remove_recursive`. Это и есть
   основное задание трека.
2. **`02-procfs` — коллекция через seq_file.** `/proc/k5_list`: `write` добавляет строку
   в список, `cat` перечисляет. Освоить `seq_operations`, `proc_ops`, и главное —
   блокировку от `seq_start` до `seq_stop`.
3. **`03-sysfs` — атрибут устройства.** `/sys/kernel/k5_device/mode_level` по правилу
   «один файл — одно значение»: `sysfs_emit` на чтение, `kstrtoint` на запись,
   `kobject_put` на выгрузку.
4. **`04-ramfs-mini` — простейшая in-memory ФС** (амбициозно). Регистрация `k5fs`,
   `init_fs_context` → `get_tree_nodev` → `fill_super` с `d_make_root`. Понять
   устройство суперблока и монтирования.

### 13.2 Вопросы для самопроверки (ответь вслух)

1. Чем `inode` отличается от `dentry`? Где хранится **имя** файла и почему именно там?
2. Почему `f_pos` живёт в `struct file`, а не в `inode`? Что будет, если два процесса
   откроют один файл?
3. Могут ли два `dentry` указывать на один `inode`? Когда это происходит и что при этом
   с `i_nlink`?
4. Назови три псевдо-ФS и для чего каждая. Куда выставить отладочный счётчик, а куда —
   штатный параметр устройства?
5. Почему в sysfs действует правило «один файл — одно значение»? Что сломается, если
   вывести туда таблицу?
6. Зачем `seq_file`, если есть обычный `.read`? Какую проблему он решает?
7. Где брать и где отпускать блокировку при итерации списка через `seq_file`? Что
   произойдёт, если взять её только в `show`?
8. Что случится, если выгрузить модуль, не вызвав `debugfs_remove_recursive`? Почему
   именно oops, а не тихий мусор?
9. Почему `kobject` освобождают через `kobject_put`, а не `kfree`?
10. Опиши цепочку монтирования современной ФС: что вызывает что от `mount(2)` до
    `fill_super`? Почему `mount_nodev` больше не используют?
11. Что такое page cache и как `struct address_space` связана с `inode`? Что такое
    «грязная» страница и кто её сбрасывает?
12. Что такое dcache и зачем нужны отрицательные (negative) dentry?
13. Почему `proc_ops` отделили от `file_operations` и с какой версии ядра?
14. Чем `sysfs_emit` безопаснее `sprintf` в методе `show`?
15. Почему `kstrtoint` лучше, чем разбор строки вручную, и что вернуть из `store` при
    мусоре на входе?

---

## 14. Банк вопросов

### БАЗА (термины — отвечать мгновенно)

- Что такое VFS и зачем он нужен (единый интерфейс к разным ФС).
- Роли четырёх объектов: `super_block`, `inode`, `dentry`, `file`.
- Где хранится имя файла (в `dentry`, не в `inode`).
- Что такое `i_ino`, `i_mode` (`S_IFDIR`/`S_IFREG`), `i_nlink`.
- Что уникально на каждый `open()` (`f_pos` в `struct file`).
- Псевдо-ФС: procfs/sysfs/debugfs — назначение каждой.
- Правило sysfs «один файл — одно значение».
- Что делает `debugfs_remove_recursive` и почему он обязателен.

### МЕХАНИЗМЫ

- Жизненный цикл `open`→`read`→`close` через таблицы операций.
- `seq_file`: четыре метода и зачем; где блокировка.
- `proc_create` + `proc_ops` (смена с `file_operations` в 5.6).
- `kobject` + атрибут + `__ATTR`; `kref` и `kobject_put`.
- `sysfs_emit` vs `sprintf`; `kstrtoX` и возврат ошибки из `store`.
- `simple_read_from_buffer` — частичные чтения и EOF.
- debugfs возвращает `ERR_PTR` (проверка `IS_ERR`), а не `NULL`.

### ЭКСПЕРТ

- Своя ФС: `file_system_type`, `init_fs_context` → `get_tree_nodev` → `fill_super`.
- `fill_super`: суперблок, корневой inode, `d_make_root`; роль `libfs`.
- Page cache: `struct address_space`, `address_space_operations`
  (`read_folio`/`writepages`), грязные страницы и writeback.
- Path lookup и dcache: RCU-walk vs ref-walk, negative dentry.
- Времена inode только через accessor-ы; folio вместо page в новом API.

### Антипаттерны (узнавать и не делать)

- Лок в `show` вместо `start..stop` (UAF при итерации).
- Забытая очистка debugfs/proc → oops после `rmmod`.
- `kfree(kobject)` мимо refcount.
- `sprintf` в sysfs-`show`; таблица в одном sysfs-файле.
- Старый API на новом ядре: `mount_nodev`, прямое `inode->i_mtime =`.

### ЗАДАНИЯ

- `01-debugfs`, `02-procfs`, `03-sysfs`, `04-ramfs-mini` (см. раздел 13.1).

---

## 15. Глоссарий

- **VFS (Virtual File System)** — виртуальная файловая система; слой абстракции ядра,
  дающий единый интерфейс (`open`/`read`/...) ко всем ФС.
- **inode (index node)** — индексный узел; структура самого файла: тип, права, размер,
  времена, ссылки на данные. **Имени файла не содержит.**
- **dentry (directory entry)** — запись каталога; связывает имя с `inode`, формирует
  дерево; кэшируется в dcache.
- **super_block** — суперблок; дескриптор смонтированного экземпляра ФС.
- **file (struct file)** — открытый файловый дескриптор; несёт позицию `f_pos`
  (уникальную на каждый `open`).
- **dcache (dentry cache)** — кэш `dentry` (хэш-таблица), ускоряющий разбор путей.
- **path lookup / path walk** — разбор пути по компонентам в цепочку `dentry`/`inode`.
- **negative dentry** — отрицательный dentry (`d_inode == NULL`); кэш «файла нет».
- **pseudo filesystem** — псевдо-ФС; ФС без носителя, содержимое генерируется ядром.
- **procfs (`/proc`)** — процессы и легаси-информация ядра.
- **sysfs (`/sys`)** — модель устройств; правило «один файл — одно значение».
- **debugfs (`/sys/kernel/debug`)** — отладочная ФС; «место без правил» для разработчика.
- **seq_file (sequence file)** — инфраструктура для вывода последовательностей/коллекций
  с автоматической буферизацией и обработкой частичных чтений.
- **kobject (kernel object)** — базовый объект модели устройств; = каталог в sysfs.
- **kset / ktype** — группа `kobject` / «тип» объекта (операции + `release`).
- **kref** — счётчик ссылок ядра; освобождение по достижении нуля.
- **attribute (show/store)** — атрибут sysfs; файл с коллбеками чтения/записи.
- **page cache** — страничный кэш; содержимое файлов в RAM.
- **address_space** — отображение файла в страницы page cache (`inode->i_mapping`).
- **address_space_operations** — операции ФС над страницами (`read_folio`, `writepages`).
- **folio** — дескриптор одной или нескольких смежных страниц page cache (новый API).
- **dirty page** — грязная страница; изменена в кэше, ещё не сброшена на носитель.
- **writeback** — отложенный сброс грязных страниц на носитель.
- **fs_context** — контекст монтирования; современный механизм mount.
- **libfs** — библиотека готовых операций для простых (in-memory) ФС (`fs/libfs.c`).
- **LKM (Loadable Kernel Module)** — загружаемый модуль ядра (`.ko`).
- **EOF (End Of File)** — конец файла; `read` возвращает 0.
- **ABI (Application Binary Interface)** — двоичный интерфейс; стабильность sysfs/procfs
  как контракт с userspace.

---

## 16. Что дальше

Ты научился выставлять состояние ядра наружу и понял устройство ФС. Дальше:

- **K6 — netfilter и сетевой стек.** Там псевдо-ФС всплывёт снова: статистику
  перехваченных пакетов удобно отдавать через `/proc` (seq_file) или debugfs. А сокет
  (`struct socket`) внутри держит `struct file` — VFS дотягивается и до сети.
- **K7 — трассировка из ядра.** tracefs (`/sys/kernel/tracing`) — ещё одна псевдо-ФС,
  родственница debugfs; ftrace/eBPF выставляют интерфейсы ровно теми же механизмами.

### Мини-проект для закрепления

Возьми любой свой драйвер из K1–K4 и добавь к нему **полный набор интерфейсов**:
debugfs-каталог с дампом внутреннего состояния (счётчики, флаги), sysfs-атрибут для
одного настраиваемого параметра, и `/proc`-файл со списком объектов через `seq_file`
(с правильной блокировкой). Это ровно то, что делает реальный драйвер, и ровно критерий
«освоено» по треку.

### Что унести из K5 (одна страница)

- VFS = единый интерфейс через таблицы операций; четыре объекта:
  `super_block` (ФС) → `inode` (файл) → `dentry` (имя) → `file` (открытие, `f_pos`).
- Имя — в `dentry`, не в `inode`; отсюда hard links и dcache.
- debugfs — отладка (правил нет, root, можно ломать); sysfs — параметры устройств
  (один файл — одно значение, стабильно); procfs — легаси и процессы.
- Коллекции — через `seq_file`; лок держать от `start` до `stop`.
- sysfs: `sysfs_emit` + `kstrtoX`; освобождение `kobject` — `kobject_put`.
- Своя ФС: `init_fs_context` → `get_tree_nodev` → `fill_super` (+`d_make_root`);
  опирайся на `libfs`.
- Современные ядра: `proc_ops` (не `file_operations`), `get_tree_nodev` (не
  `mount_nodev`), времена inode — только через accessor-ы.
- Очистка в `exit` — закон: удалить узлы ФС и освободить память, иначе oops/утечка.





