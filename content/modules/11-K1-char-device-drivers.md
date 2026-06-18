# Модуль K1 — Драйверы символьных устройств

> Этап 2B, Сторона ядра. Метка трека: *(новое)*. Это **главный переход** курса: из
> «пользователя ядра» (ты звал сисколлы, профилировал, грузил готовые модули) ты
> становишься **разработчиком ядра** — пишешь код, который исполняется в kernel
> space, в одном адресном пространстве с планировщиком, сетевым стеком и
> файловыми системами. Символьный драйвер — каноническая первая задача: устройство
> `/dev/...`, которое userspace открывает, читает и пишет, а ты реализуешь
> обратную сторону `read`/`write`/`ioctl`/`mmap`/`poll`. Здесь ты впервые
> пересекаешь границу userspace/kernel **изнутри** (`copy_to_user`/`copy_from_user`,
> `__user`), регистрируешь устройство (`cdev`, major/minor), и защищаешь состояние
> драйвера при конкурентном доступе. Если ты уверенно объясняешь, почему нельзя
> разыменовать userspace-указатель в ядре, и можешь нарисовать путь от `read(fd)` в
> userspace до твоего `.read` в `file_operations` — проматывай к самопроверке.
>
> **Язык — только C.** Ядро Linux пишется на C (C89/GNU-C с расширениями); никакого
> C++/STL/исключений/`malloc` — у ядра свои API (`kmalloc`, `printk`, свои списки и
> примитивы синхронизации). Это смена среды: то, что ты знал из userspace-C (Ф1),
> остаётся, но стандартная библиотека — другая.
>
> **Опирается на Ф1–Ф3 и C1–C4.** `goto`-cleanup и UB из Ф1 — здесь критичны (ошибка
> в ядре = oops/паника, не segfault процесса). Граница userspace/kernel и `copy_*`
> — обратная сторона Ф2. `container_of` и layout — Ф1/Ф3. Мьютексы/спинлоки —
> синхронизация из C1, но в ядре. `poll`-совместимость драйвера — обратная сторона
> epoll из C2/C4 (`poll_wait`, wait queues).

**Читать к модулю:**

- **«Linux Device Drivers, 3rd» (LDD3)** — бесплатно (lwn.net), концепции (часть
  кода устарела — сверяйся с актуальным API). Главы 1–6 (модули, char devices,
  fops, ioctl, time, sleeping).
- **John Madieu, «Linux Device Drivers Development»** — свежее (современный API).
- **Материалы Bootlin** (bootlin.com/docs) — бесплатные слайды/лабы, отличны.
- **`docs.kernel.org`** — `driver-api/`, `core-api/`, `process/coding-style.rst`.
- **Greg KH, «Write and Submit your first Linux kernel patch»** — про upstream.
- Исходники: `fs/char_dev.c`, `drivers/char/` (примеры), `include/linux/fs.h`
  (`file_operations`), `include/linux/cdev.h`.

---

## 0. Карта модуля

| Раздел | О чём | Зачем |
|--------|-------|-------|
| 1 | От пользователя ядра к разработчику; LKM | Смена роли и среды |
| 2 | Анатомия модуля: init/exit, лицензия, Kbuild | Минимальный рабочий модуль |
| 3 | Kernel space: что можно и нельзя | Чем ядро отличается от userspace |
| 4 | Символьные устройства, major/minor, `dev_t` | Как ядро адресует устройства |
| 5 | Регистрация: `cdev`, class, device | Как появляется `/dev/...` узел |
| 6 | `file_operations` — таблица методов | Что вызывает VFS |
| 7 | Граница userspace/kernel: `__user`, `copy_*` | Безопасный обмен данными |
| 8 | `read`/`write`: `*ppos`, частичный I/O | Ядро обработчиков |
| 9 | `container_of` — связь объектов в ядре | Идиома №1 ядра |
| 10 | `ioctl`: кодирование команд, безопасность | Управляющий интерфейс |
| 11 | Блокирующий I/O и wait queues | Усыпить процесс до готовности |
| 12 | `poll`/epoll-совместимость: `poll_wait` | Обратная сторона C2/C4 |
| 13 | `mmap` в драйвере: `remap_pfn_range` | Отдать память устройства |
| 14 | Конкурентный доступ: mutex/spinlock/atomic | Защита состояния драйвера |
| 15 | Память ядра: `kmalloc`/`vmalloc`, GFP | Аллокация без `malloc` |
| 16 | Отладка: `printk`, `dmesg`, oops, KASAN | Как чинить в ядре |
| 17 | Out-of-tree сборка, Kbuild детально | Собрать свой модуль |
| 18 | Практика и самопроверка | Закрепление |
| 19–21 | Банк вопросов, глоссарий, что дальше | — |

**Время на модуль:** 25–40 часов (с QEMU и упражнением).

**Что значит «освоено» (из трека):** *пишешь драйвер, корректно работающий с
конкурентным доступом и совместимый с `poll`/epoll из userspace.* Не «скопировал
hello-модуль», а понимаешь модель: как VFS вызывает твои `fops`, почему userspace-
указатель нельзя трогать напрямую, как усыпить и разбудить процесс (wait queue), и
как защитить состояние от гонок.

---

## 1. От пользователя ядра к разработчику

### 1.1 Две стороны границы

Весь курс до этого ты был **на стороне userspace**: звал `read`/`write`/`mmap`
(Ф2), строил event-loop поверх `epoll` (C2), профилировал (C6). Каждый из этих
вызовов **уходил в ядро** — и там кто-то его обслуживал. Теперь ты пишешь **этого
кого-то**. Когда userspace делает `read(fd)`, ядро (через VFS — Virtual File System,
виртуальную файловую систему) находит твою функцию `.read` и зовёт её. Ты — по
**другую** сторону того же сисколла.

```
userspace:  fd = open("/dev/cppchar");  read(fd, buf, n);   ← Ф2, ты это писал
                       │ сисколл
                       ▼
kernel/VFS:  sys_read → file->f_op->read(...)  → ТВОЙ my_read()  ← K1, теперь это ты
```

### 1.2 LKM — loadable kernel module

**LKM (Loadable Kernel Module)** — кусок кода ядра, который можно **загрузить и
выгрузить** в работающее ядро без перезагрузки (`insmod`/`rmmod`/`modprobe`). Это и
есть форма драйвера, которую ты пишешь: не патч в дерево ядра, а **out-of-tree**
модуль (`.ko` — kernel object). Модуль исполняется **в kernel space**, с полными
привилегиями, в адресном пространстве ядра.

- `insmod module.ko` — загрузить (вызывает твой `module_init`).
- `rmmod module` — выгрузить (вызывает `module_exit`).
- `lsmod` — список загруженных; `modinfo module.ko` — метаданные.
- `dmesg` — журнал ядра (твои `printk` видны здесь, не в stdout).

### 1.3 Цена ошибки выросла

В userspace ошибка = `SIGSEGV`, процесс упал, система жива. **В ядре ошибка = oops
или паника**: разыменование NULL, выход за буфер, неосвобождённый лок — могут
подвесить или уронить **всю машину**. Поэтому дисциплина Ф1 (UB, goto-cleanup,
проверка границ) здесь не «хороший тон», а условие выживания. Отлаживают в **QEMU**
(виртуалка — упадёт она, не твой хост) — ровно так устроено упражнение модуля.

### 1.4 Почему символьный драйвер первым

Символьное устройство — простейшая и самая частая модель драйвера: устройство,
с которым работают как с **потоком байт** через `/dev/...` (`read`/`write`).
Так представлены терминалы, `/dev/null`, `/dev/random`, последовательные порты,
многие сенсоры. Освоив char-драйвер, ты понимаешь **всю модель** взаимодействия
ядра с userspace через VFS — фундамент для блочных, сетевых и прочих драйверов.

### 1.5 In-tree vs out-of-tree

Драйвер живёт в одной из двух форм:

- **In-tree** — в дереве исходников ядра (`drivers/...`), собирается **вместе** с
  ядром, попадает в апстрим. Так живут «настоящие» драйверы: ревью сообществом,
  поддержка при изменении внутренних API, доступность во всех дистрибутивах.
- **Out-of-tree** — отдельный модуль (`.ko`), собираемый против установленного ядра
  (Kbuild с `M=`). Так разрабатывают, тестируют, поставляют проприетарные/нишевые
  драйверы (NVIDIA, VirtualBox). Минус: при смене внутреннего API ядра (он
  **нестабилен**!) модуль надо чинить; DKMS пересобирает его при обновлении ядра.

Ты пишешь **out-of-tree** (быстрый цикл: правка → `make` → `insmod` в QEMU). Но
**целевая** форма «настоящего» драйвера — upstream (in-tree): Greg KH «Write and
Submit your first patch». Внутренний API ядра **намеренно нестабилен** (нет KABI-
гарантий для in-tree) — это позволяет ядру эволюционировать, но требует, чтобы
драйверы были в дереве и чинились вместе с ним.

---

## 2. Анатомия модуля

### 2.1 Минимальный модуль

```c
#include <linux/module.h>   // module_init/exit, MODULE_*
#include <linux/kernel.h>   // pr_info
#include <linux/init.h>     // __init, __exit

static int __init hello_init(void)
{
    pr_info("hello: загружен\n");   // → dmesg
    return 0;                        // 0 = успех; <0 = ошибка (модуль не загрузится)
}

static void __exit hello_exit(void)
{
    pr_info("hello: выгружен\n");
}

module_init(hello_init);             // что звать при insmod
module_exit(hello_exit);             // что звать при rmmod

MODULE_LICENSE("GPL");               // ОБЯЗАТЕЛЬНО (иначе tainted + нет GPL-символов)
MODULE_AUTHOR("you");
MODULE_DESCRIPTION("minimal module");
```

- **`module_init`/`module_exit`** регистрируют точки входа/выхода. `init` возвращает
  `int`: `0` — успех; отрицательный код (`-ENOMEM`, `-EBUSY`) — провал загрузки.
- **`__init`/`__exit`** — атрибуты секций: код `__init` ядро **выбрасывает** из
  памяти после загрузки (он больше не нужен); `__exit` не включается, если модуль
  собран встроенным (не выгружается).
- **`MODULE_LICENSE("GPL")`** — **обязательно**. Без неё ядро помечается «tainted», и
  модулю **недоступны** GPL-only символы (а их много). Лицензия — часть ABI.

### 2.2 Сборка: Kbuild (out-of-tree)

Модуль собирается **системой сборки ядра** (Kbuild), а не просто `gcc`:

```makefile
# Makefile рядом с module.c
obj-m := module.o            # собрать module.c → module.ko

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

- `-C /lib/modules/$(uname -r)/build` — каталог **заголовков и сборочной системы**
  работающего ядра (пакет `linux-headers-$(uname -r)`). Модуль собирается **против
  конкретной версии** ядра — ABI ядра нестабилен, `.ko` от другого ядра не загрузится.
- `M=$(PWD)` — где лежит твой модуль (внешний/out-of-tree).
- Результат — `module.ko` (+ `.mod.c`, `Module.symvers` и пр.).

> **Грабли:** `M=` должен быть **абсолютным** путём — относительный kbuild не найдёт
> (он `cd`-ит в каталог ядра). Версия заголовков **обязана** совпадать с
> работающим ядром (`uname -r`), иначе `insmod` даст `-ENOEXEC`/«version magic».

### 2.3 Жизненный цикл

```
insmod module.ko
   │  ядро: проверка version magic, разрешение символов, выделение памяти
   ▼
module_init() → твоя инициализация (регистрация устройства, аллокации)
   │  return 0 → модуль активен; return <0 → откат, insmod падает
   ▼
... работает, обслуживает userspace ...
   │
rmmod module   (только если refcount == 0 — никто не держит устройство)
   ▼
module_exit() → освободить ВСЁ, что захватил init (обратный порядок)
```

`module_exit` **обязан** освободить **всё** ресурсы init'а — в ядре нет «процесс
завершился, ОС подчистит». Утечка в драйвере живёт до перезагрузки.

### 2.4 Worked: собрать и загрузить hello-модуль

```sh
$ ls
Makefile  hello.c
$ make                                    # Kbuild собирает hello.ko
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
  CC [M]  hello.o
  MODPOST Module.symvers
  LD [M]  hello.ko
$ sudo insmod hello.ko                     # загрузить (зовёт hello_init)
$ dmesg | tail -1
[ 1234.5] hello: загружен
$ lsmod | grep hello
hello                  16384  0            # имя, размер, refcount=0 (никто не держит)
$ modinfo hello.ko | head -3
filename: .../hello.ko
license:  GPL
vermagic: 7.0.0-22-generic SMP ...         # version magic — против какого ядра
$ sudo rmmod hello                          # выгрузить (зовёт hello_exit)
$ dmesg | tail -1
[ 1240.1] hello: выгружен
```

Если `insmod` падает с «Invalid module format» / `-ENOEXEC` — version magic не
совпал (собран против другого ядра). «Operation not permitted» — нет прав (нужен
root/`CAP_SYS_MODULE`) или Secure Boot требует подписи модуля (§17.5).

---

## 3. Kernel space: что можно и нельзя

Ядро — **другая среда**. Привычки userspace-C тут опасны.

### 3.1 Чего НЕТ

- **Нет стандартной библиотеки C.** Нет `malloc`/`free`, `printf`, `memcpy` из
  libc, `string.h` в привычном виде. Есть **ядерные** аналоги: `kmalloc`/`kfree`,
  `printk`/`pr_*`, `memcpy` (ядерный), `kstrtoint`, и т.д. (`<linux/...>`).
- **Нет плавающей точки** (по умолчанию) — FPU/SIMD в ядре требует
  `kernel_fpu_begin/end` и редок; в драйверах — целочисленная арифметика.
- **Нет userspace-памяти напрямую** — указатель из userspace (`__user`) нельзя
  разыменовать (§7).
- **Нет исключений/C++/RAII** — только C, ручное управление, `goto`-cleanup.

### 3.2 Что ОПАСНО

- **Маленький стек.** Стек ядра — **8 КБ** (или 16 КБ) на поток, не мегабайты. Нельзя
  большие локальные массивы/глубокую рекурсию — переполнение стека ядра = повреждение
  соседних структур, паника. Большие буферы — через `kmalloc`.
- **Нельзя «спать» в атомарном контексте.** В обработчике прерывания, под спинлоком,
  в контексте softirq — **нельзя** блокироваться/спать (`mutex_lock`, `kmalloc(GFP_KERNEL)`,
  `copy_*_user`). Это правило §14/§15 — нарушение даёт зависание/«scheduling while
  atomic».
- **Конкурентность по умолчанию.** Твой код **вытесняем** и вызывается **параллельно**
  на разных ядрах (несколько процессов открыли устройство). Любое разделяемое
  состояние — под защитой (§14) с первой строки.
- **Прямой доступ к железу/всей памяти.** Ошибка адресации не ловится MMU как в
  userspace — она портит ядро. Отсюда oops/паника.

### 3.3 Контексты исполнения

Код ядра исполняется в одном из контекстов — от него зависит, что **можно**:

| Контекст | Можно спать? | Пример |
|----------|:---:|--------|
| **Process context** (от имени сисколла) | **да** | твои `read`/`write`/`ioctl` — можно `mutex`, `copy_*_user`, `kmalloc(GFP_KERNEL)` |
| **Softirq/tasklet** | нет | сетевой стек, отложенная работа |
| **Hardirq** (обработчик прерывания) | **нет** | верхняя половина драйвера прерываний (K3) |
| **Под спинлоком** | нет | критическая секция со спинлоком |

Твои `file_operations`-методы (`read`/`write`/`ioctl`) вызываются в **process
context** — поэтому в них **можно** спать (мьютекс, копирование в userspace, ожидание).
Это важное упрощение для char-драйвера.

> **SMP и preemption по умолчанию.** Ядро Linux — **вытесняемое** (preemptible) и
> **многопроцессорное** (SMP): твой `read` на ядре 0 и `write` на ядре 1 исполняются
> **одновременно**; даже на одном ядре `read` может быть **вытеснен** в любой точке
> (кроме атомарных секций). Поэтому «у меня же один поток в драйвере» — **ложное**
> допущение: драйвер параллелен с первой строки. Это причина §14 (защищай всё
> разделяемое) — не «на всякий случай», а потому что параллелизм гарантирован.

### 3.4 Коды ошибок и стиль ядра

Ядро говорит **отрицательными errno** и имеет жёсткие конвенции:

- **Возврат ошибки** — `return -ENOMEM;` (отрицательный код), не `-1`. Стандартные:
  `-ENOMEM` (нет памяти), `-EINVAL` (неверный аргумент), `-EFAULT` (плохой userspace-
  адрес), `-EBUSY` (занято), `-EAGAIN` (повторить/неблокирующий), `-ENOTTY`
  (неподдерживаемый ioctl), `-EPERM` (нет прав), `-ENODEV` (нет устройства).
- **Указатель-или-ошибка.** Функции, возвращающие указатель, кодируют ошибку **в
  самом указателе** (не NULL): проверяй `IS_ERR(p)`, извлекай `PTR_ERR(p)`, оборачивай
  `ERR_PTR(-EINVAL)`. Это потому, что NULL — валидный «нет объекта», а ошибок много.
  ```c
  void *p = some_alloc();
  if (IS_ERR(p)) return PTR_ERR(p);    // НЕ if(!p) — функция вернула -E* в указателе
  ```
- **goto-cleanup** (Ф1) — **стандарт** ядра (`Documentation/process/coding-style.rst`
  прямо рекомендует): единый выход, освобождение в обратном порядке захвата (§5.3).
- **Стиль:** табы, 8 пробелов, строки ≤ 80, имена `lower_snake_case`. **`checkpatch.pl`**
  (`scripts/checkpatch.pl`) проверяет стиль — гоняй на своём драйвере перед отправкой
  upstream. Чужой стиль в ядре не принимают.

Ядро использует **GNU C** (не строгий ANSI): `typeof`, statement expressions
(`({ ... })`), `__attribute__`, инициализаторы по полям (`.field = x`), нулевые
массивы/FAM (Ф1). Многие макросы ядра на этом построены (`container_of`, `min`/`max`
с проверкой типов, `READ_ONCE`). Это тот же GNU-C, что ты видел в Ф1 — в ядре он
повсеместен и обязателен (ядро не собирается строгим C89). Плавающую точку и
длинные `double` — не использовать.

---

## 4. Символьные устройства, major/minor, `dev_t`

### 4.1 Как ядро адресует устройства

Каждый узел устройства в `/dev` имеет **тип** (char/block), **major** и **minor**:

- **Major** — номер **драйвера** (какой драйвер обслуживает этот класс устройств).
- **Minor** — номер **экземпляра** внутри драйвера (какое конкретно устройство).

```sh
$ ls -l /dev/null /dev/cppchar
crw-rw-rw- 1 root root 1, 3 ... /dev/null      # c = char; major 1, minor 3
crw------- 1 root root 511, 0 ... /dev/cppchar # major 511 (динамический), minor 0
```

`c` в начале = **char device**. Когда userspace открывает `/dev/cppchar`, ядро по
(major, minor) находит **твой** драйвер и его `file_operations`.

### 4.2 `dev_t` — упакованные major+minor

```c
#include <linux/kdev_t.h>
dev_t dev;                 // 32 бита: major (12 бит) + minor (20 бит)
unsigned ma = MAJOR(dev);  // извлечь major
unsigned mi = MINOR(dev);  // извлечь minor
dev_t d = MKDEV(ma, mi);   // собрать
```

`dev_t` — это упакованная пара. Не разбирай биты руками — только `MAJOR`/`MINOR`/
`MKDEV`.

Где увидеть зарегистрированное устройство:
```sh
$ cat /proc/devices | grep cppchar     # major и имя зарегистрированных char/block драйверов
511 cppchar
$ ls -l /sys/dev/char/511:0            # символьные устройства по major:minor
$ ls /sys/class/cppchar/               # класс (из class_create) и его устройства
```
`/proc/devices`, `/sys/dev/char/`, `/sys/class/` — карта того, что драйвер
зарегистрировал. Полезно при отладке «почему нет /dev-узла» (зарегистрировано ли
устройство и класс).

### 4.3 Статический vs динамический major

- **Статический** (`register_chrdev_region`) — ты сам задаёшь major. Плохо: можешь
  конфликтнуть с другим драйвером.
- **Динамический** (`alloc_chrdev_region`) — ядро **выдаёт** свободный major. **Так
  правильно** в новом коде:

```c
dev_t dev_num;
// выделить 1 minor (с 0), имя "cppchar"; ядро выберет major:
int ret = alloc_chrdev_region(&dev_num, /*baseminor*/0, /*count*/1, "cppchar");
if (ret < 0) return ret;             // -ENOMEM / -EBUSY
pr_info("major=%d\n", MAJOR(dev_num));
// ... в exit: unregister_chrdev_region(dev_num, 1);
```

---

## 5. Регистрация: `cdev`, class, device

Выделить `dev_t` — мало; нужно **связать** его с твоими `file_operations` (`cdev`) и
**создать узел** `/dev/cppchar`. Это упражнение `01-char-dev`.

### 5.1 `cdev` — связь устройства с `fops`

`struct cdev` — внутренняя структура ядра, представляющая char-устройство. Она
связывает (major, minor) с твоей таблицей методов:

```c
#include <linux/cdev.h>
static struct cdev my_cdev;

cdev_init(&my_cdev, &fops);        // привязать fops к cdev
my_cdev.owner = THIS_MODULE;       // refcount: не дать rmmod, пока устройство открыто
int ret = cdev_add(&my_cdev, dev_num, /*count*/1);  // зарегистрировать в ядре
if (ret < 0) { /* откат */ }
// после cdev_add ядро МОЖЕТ начать звать твои fops в ЛЮБОЙ момент — будь готов!
// в exit: cdev_del(&my_cdev);
```

> **Важно:** после `cdev_add` устройство **активно** — ядро может вызвать твои
> `open`/`read` **немедленно** (если кто-то откроет узел). Поэтому всё состояние
> (буфер, мьютекс) должно быть инициализировано **до** `cdev_add`.

### 5.2 Создание узла `/dev/...`: class + device

`cdev_add` регистрирует устройство, но **узел** `/dev/cppchar` сам не появится.
Раньше его создавали вручную (`mknod`); теперь — через **class** + **device**, и
**devtmpfs** (виртуальная ФС на `/dev`) создаёт узел автоматически по uevent:

```c
#include <linux/device.h>
static struct class *my_class;

my_class = class_create("cppchar");         // ОДНОаргументная с ядра 6.4+!
if (IS_ERR(my_class)) { ret = PTR_ERR(my_class); goto err; }
device_create(my_class, NULL, dev_num, NULL, "cppchar");  // → /dev/cppchar
// в exit (обратный порядок!):
//   device_destroy(my_class, dev_num);
//   class_destroy(my_class);
```

> **Версия ядра:** `class_create(name)` стала одноаргументной в 6.4. На старых ядрах
> — `class_create(THIS_MODULE, name)`. Это частая причина «не собирается на другом
> ядре». `IS_ERR`/`PTR_ERR` — ядерная идиома: функции возвращают **указатель или
> закодированную ошибку** (не NULL); проверяй `IS_ERR(ptr)`, извлекай `PTR_ERR(ptr)`.

### 5.3 Полная регистрация с откатом

`init` захватывает ресурсы по очереди; при ошибке — откат в **обратном** порядке
(goto-cleanup из Ф1, но в ядре цена ошибки выше):

```c
static int __init my_init(void)
{
    int ret;
    buffer = kzalloc(BUF_SIZE, GFP_KERNEL);
    if (!buffer) return -ENOMEM;

    ret = alloc_chrdev_region(&dev_num, 0, 1, "cppchar");
    if (ret) goto err_buf;

    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;
    ret = cdev_add(&my_cdev, dev_num, 1);
    if (ret) goto err_region;

    my_class = class_create("cppchar");
    if (IS_ERR(my_class)) { ret = PTR_ERR(my_class); goto err_cdev; }

    device_create(my_class, NULL, dev_num, NULL, "cppchar");
    pr_info("cppchar: loaded, major=%d\n", MAJOR(dev_num));
    return 0;

err_cdev:    cdev_del(&my_cdev);
err_region:  unregister_chrdev_region(dev_num, 1);
err_buf:     kfree(buffer);
    return ret;          // insmod увидит ошибку, модуль не загрузится
}
```

Порядок exit — **зеркальный** init: `device_destroy` → `class_destroy` → `cdev_del`
→ `unregister_chrdev_region` → `kfree`. Перепутаешь порядок (например, `kfree` буфера
до `cdev_del`, пока устройство ещё открыто) — use-after-free в ядре.

### 5.4 Современная альтернатива: miscdevice

Для **простого** char-устройства (один minor) есть упрощённый путь — `misc_register`:
он сам выделяет minor (major фиксирован — 10, MISC), создаёт узел, и одной функцией
делает то, что выше — несколько. Хорош, когда не нужен свой major/много minor'ов:

```c
#include <linux/miscdevice.h>
static struct miscdevice my_misc = {
    .minor = MISC_DYNAMIC_MINOR, .name = "cppchar", .fops = &fops,
};
// init: misc_register(&my_misc);  exit: misc_deregister(&my_misc);
```

В упражнении используется полный путь (`cdev` + class) — чтобы понять механику
major/minor; `miscdevice` знать как короткий вариант для простых случаев.

### 5.4.1 Несколько устройств: minor 0..N

`alloc_chrdev_region(&dev, 0, N, name)` выделяет **N** minor'ов сразу. Драйвер с
несколькими устройствами держит **массив** структур и по `MINOR(inode->i_rdev)`
выбирает нужное в `open`:

```c
#define NDEV 4
static struct my_device devices[NDEV];

static int my_open(struct inode *inode, struct file *file)
{
    unsigned mi = MINOR(inode->i_rdev);          // какой экземпляр?
    if (mi >= NDEV) return -ENODEV;
    file->private_data = &devices[mi];           // состояние этого экземпляра
    return 0;
}
// init: цикл device_create(my_class, NULL, MKDEV(major, i), NULL, "cppchar%d", i);
//       cdev_add один на весь диапазон (или cdev на устройство).
```

Так один драйвер обслуживает `/dev/cppchar0..3`, у каждого — свой буфер/лок. Это и
есть смысл major (драйвер) + minor (экземпляр) из §4.1. `container_of` (§9) — более
гибкая альтернатива массиву (динамическое число устройств).

### 5.5 Refcount: не дать выгрузить занятый модуль

Опасность: модуль выгружают (`rmmod`), пока userspace **держит открытым** устройство
— тогда `read`/`write` вызовут код **выгруженного** модуля → oops. Защита —
**счётчик ссылок** модуля:

- **`.owner = THIS_MODULE`** в `file_operations` — ядро **автоматически** инкрементит
  refcount модуля при `open` и декрементит при `release`. Пока устройство открыто,
  `rmmod` вернёт `-EBUSY` («Module is in use»). Это **обязательное** поле — без него
  модуль можно выгрузить из-под открытого fd.
- `my_cdev.owner = THIS_MODULE` — то же для cdev.

```sh
$ exec 3< /dev/cppchar        # держим открытым (fd 3)
$ sudo rmmod cppchar
rmmod: ERROR: Module cppchar is in use   # refcount > 0 — защита сработала
$ exec 3<&-                    # закрыли fd
$ sudo rmmod cppchar           # теперь ок
```

`lsmod` показывает этот refcount (третья колонка). `.owner = THIS_MODULE` — почему
он есть в каждом примере `fops`: без него — гонка выгрузки и use-after-free кода
модуля.

---

## 6. `file_operations` — таблица методов

### 6.1 Что это

`struct file_operations` (`<linux/fs.h>`) — **таблица указателей на функции**, по
которой VFS вызывает твой драйвер на каждый сисколл над fd. Это сердце драйвера:

```c
static const struct file_operations fops = {
    .owner          = THIS_MODULE,      // refcount модуля
    .open           = my_open,          // open(fd)  → сюда
    .release        = my_release,       // close(fd) → сюда (когда последний fd закрыт)
    .read           = my_read,          // read(fd, buf, n)
    .write          = my_write,         // write(fd, buf, n)
    .unlocked_ioctl = my_ioctl,         // ioctl(fd, cmd, arg)
    .llseek         = my_llseek,        // lseek(fd, ...)
    .poll           = my_poll,          // poll/select/epoll (§12)
    .mmap           = my_mmap,          // mmap(fd) (§13)
};
```

Не заданные поля = NULL → VFS вернёт дефолт/ошибку для этой операции. Это **массив
указателей на функции** — ровно dispatch table из C2 §8.5, но со стороны ядра.

### 6.2 Связь с C2 «всё есть fd»

В C2/C4 ты строил event-loop, где сокет/таймер/сигнал — это fd с `EPOLLIN`. **Кто**
делает fd «читаемым»? Драйвер — через `.poll` и wait queues (§12). `file_operations`
— это интерфейс, который ты в C2 **потреблял**; здесь ты его **реализуешь**. Та же
монета, другая сторона.

### 6.3 `struct file` и `struct inode`

Методам приходят два ключевых объекта:

- **`struct inode`** — представляет **файл** (узел устройства): один на устройство,
  содержит (major,minor) (`i_rdev`), указатель на `cdev`. Передаётся в `open`.
- **`struct file`** — представляет **открытый дескриптор** (open file description,
  Ф3): свой на каждый `open`, содержит позицию (`f_pos`), флаги (`f_flags`:
  `O_NONBLOCK`...), и **`private_data`** — твоё поле для контекста этого открытия.

`file->private_data` — куда драйвер кладёт указатель на своё per-open состояние
(аналог `data.ptr` в epoll, C2): в `open` выделил/назначил, в `read`/`write` забрал.

### 6.3.1 Что VFS даёт «бесплатно»

Не каждый метод обязателен. VFS подставляет разумные дефолты:
- **`.llseek`** — для устройства-потока (без позиции) ставь `noop_llseek` (lseek —
  no-op) или верни `-ESPIPE` (нельзя seek). Не задал — поведение по умолчанию может
  удивить (`default_llseek` двигает `f_pos`).
- **`.open`/`.release`** — если нечего делать, можно не задавать (VFS разрешит open).
  Но обычно `.open` нужен для `container_of` + `private_data` (§9).
- **отсутствующий метод** → соответствующий сисколл вернёт `-EINVAL`/`-ENODEV`
  (например, нет `.mmap` → `mmap` на устройстве вернёт ошибку).

Задавай **только** то, что устройство реально поддерживает — остальное VFS обработает
отказом. Минимум для char-устройства-потока: `.owner`, `.read`, `.write`, `.open`,
`.release` (+ `.llseek = noop_llseek`).

### 6.4 `open`/`release` worked

```c
static int my_open(struct inode *inode, struct file *file)
{
    // получить СВОЮ структуру по встроенному cdev (§9):
    struct my_device *dev = container_of(inode->i_cdev, struct my_device, cdev);
    file->private_data = dev;            // сохранить для read/write/ioctl/poll
    // (опц.) per-open состояние: kzalloc контекста и в private_data вместо dev
    // (опц.) учесть режим открытия: file->f_mode & FMODE_WRITE и т.п.
    pr_debug("cppchar: open by pid %d\n", current->pid);
    return 0;                            // 0 = успех; <0 = open провалится с этим errno
}

static int my_release(struct inode *inode, struct file *file)
{
    // вызывается, когда закрыт ПОСЛЕДНИЙ fd этого open file description (Ф3):
    // освободить per-open состояние, если выделяли в open.
    pr_debug("cppchar: release\n");
    return 0;
}
```

- `open` зовётся на **каждый** `open(2)`; `release` — когда закрыт **последний** fd,
  ссылающийся на это open file description (после всех `dup`/`fork`, Ф3) — не на
  каждый `close`.
- `current` — указатель на `task_struct` **текущего** процесса (`current->pid`,
  `current->comm`): из process context можно узнать, кто тебя зовёт (для логов/прав).

---

## 7. Граница userspace/kernel: `__user`, `copy_*`

### 7.1 Почему нельзя разыменовать userspace-указатель

Указатель, который userspace передал в `read(fd, buf, n)`, — это адрес в **его**
адресном пространстве, не в твоём (ядерном). Разыменовать его в ядре напрямую —
**нельзя**:

- адрес может быть **невалиден** (userspace соврал, передал мусор) → oops;
- userspace-страница может быть **не загружена** (swapped/не resident) → нужен page
  fault в правильном контексте;
- это **дыра в безопасности** — userspace мог бы заставить ядро прочитать/записать
  произвольный адрес.

### 7.2 `__user` — аннотация

Указатели из userspace помечают `__user`:

```c
ssize_t my_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos);
//                                    ^^^^^^ это userspace-адрес — не трогать напрямую!
```

`__user` — аннотация для статического анализатора **sparse** (`make C=1`): он
ругается, если ты разыменуешь `__user`-указатель напрямую или перепутаешь
userspace/kernel указатели. Это не меняет код, но ловит класс багов.

### 7.3 `copy_to_user` / `copy_from_user`

Единственный правильный способ обмена с userspace:

```c
#include <linux/uaccess.h>
// из ядра В userspace (для read):
unsigned long not_copied = copy_to_user(ubuf, kbuf, n);
// из userspace В ядро (для write):
unsigned long not_copied = copy_from_user(kbuf, ubuf, n);
```

Они: (1) **проверяют** валидность userspace-адреса; (2) безопасно копируют (с
обработкой page faults); (3) возвращают **число НЕскопированных** байт — `0` =
полный успех, `!=0` → верни `-EFAULT`:

```c
if (copy_to_user(ubuf, kbuf, n))   // не 0 → часть не скопирована
    return -EFAULT;
```

> **Можно спать.** `copy_*_user` **могут заблокироваться** (page fault userspace-
> страницы) → их **нельзя** под спинлоком/в атомарном контексте (§3.2). В `read`/
> `write` (process context) — можно. Это причина, почему буфер копируют под
> **мьютексом**, а не спинлоком (мьютекс sleepable, §14).

### 7.4 Для одиночных значений: `get_user`/`put_user`

Для копирования **одного** скалярного значения (часто в `ioctl`) — быстрее:

```c
int val;
get_user(val, (int __user *)arg);     // прочитать int из userspace
put_user(result, (int __user *)arg);  // записать int в userspace
```

### 7.5 Почему `copy_*_user` безопасны (и `access_ok`)

`copy_*_user` под капотом: (1) проверяют, что адрес **в userspace-диапазоне** и
доступен (`access_ok`); (2) копируют с установленным «обработчиком исключений» — если
страница не resident или адрес плох, происходит page fault, который ядро **ловит** и
превращает в возврат «не скопировано» (а не в oops). Поэтому даже если userspace
передал плохой указатель, драйвер **не падает** — `copy_*_user` вернёт `!=0`, ты
вернёшь `-EFAULT`. Это и есть защита границы: ядро **никогда** не доверяет userspace-
адресу. (Старый код звал `access_ok` отдельно перед `__copy_*` — в новом просто
`copy_*_user`, проверка внутри.) Никогда не «оптимизируй» это прямым разыменованием —
это и краш, и дыра.

---

## 8. `read`/`write`: `*ppos`, частичный I/O

### 8.1 Контракт `read`

```c
ssize_t my_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
```

- `ubuf` — куда писать (userspace), `count` — сколько просят, `*ppos` — текущая
  позиция (ядро её хранит, ты двигаешь).
- Вернуть: **число отданных байт** (>0); **0** — EOF (конец данных); **<0** — `-errno`.
- **`*ppos` обязателен:** двигай его на число отданных байт; верни `0`, когда дошёл
  до конца — иначе `cat` зациклится (будет звать `read` бесконечно).

```c
static ssize_t my_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    ssize_t ret;
    mutex_lock(&buf_lock);
    if (*ppos >= (loff_t)data_len) { ret = 0; goto out; }   // EOF
    if (count > data_len - *ppos) count = data_len - *ppos; // не больше, чем есть
    if (copy_to_user(ubuf, buffer + *ppos, count)) { ret = -EFAULT; goto out; }
    *ppos += count;                                          // продвинуть позицию
    ret = count;
out:
    mutex_unlock(&buf_lock);
    return ret;
}
```

### 8.2 Контракт `write`

```c
static ssize_t my_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
    ssize_t ret;
    if (count > BUF_SIZE) count = BUF_SIZE;     // не переполнить буфер ядра
    mutex_lock(&buf_lock);
    if (copy_from_user(buffer, ubuf, count)) { ret = -EFAULT; goto out; }
    data_len = count;                            // (наша семантика: перезапись)
    ret = count;
out:
    mutex_unlock(&buf_lock);
    return ret;
}
```

- Вернуть число **принятых** байт. Если принял **меньше** `count` — userspace
  **дозовёт** `write` с остатком (partial write, Ф2/C2). Вернуть `0` бесконечно —
  зависание userspace; вернуть `<count` без причины — лишние вызовы.
- **Не переполни буфер ядра** — ограничь `count` своей ёмкостью. Выход за `kmalloc`-
  буфер = повреждение кучи ядра (KASAN это ловит, §16).

### 8.3 Частичный I/O — не баг, а контракт

Как в Ф2: `read`/`write` могут отдать/принять **меньше** запрошенного — это
нормально. Userspace (или libc) дозовёт. Драйвер обязан **корректно** работать при
любом `count` (в т.ч. 0) и двигать `*ppos`. Это та же дисциплина `read_full`/
`write_full` из Ф2, но теперь ты — её **причина**.

### 8.4 Кольцевой буфер: FIFO-семантика

Простой «перезаписываемый буфер» из упражнения — учебный. Реальное char-устройство
(пайп, последовательный порт) — это **FIFO** (First In First Out): `write` добавляет
в хвост, `read` забирает из головы, данные **потребляются**. Это кольцевой буфер
(C1/C6) в ядре, защищённый локом:

```c
struct my_device {
    char buf[BUF_SIZE];
    size_t head, tail;            // позиции чтения/записи (кольцо)
    struct mutex lock;
    wait_queue_head_t read_wq, write_wq;   // §11
};

// сколько занято/свободно (кольцо):
static size_t used(struct my_device *d){ return (d->tail - d->head) % BUF_SIZE; }
static size_t space(struct my_device *d){ return BUF_SIZE - 1 - used(d); }

static ssize_t my_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    struct my_device *d = f->private_data;
    size_t n;
    mutex_lock(&d->lock);
    /* (блокирующий вариант: while(!used) wait_event...; §11) */
    n = min(count, used(d));
    for (size_t i = 0; i < n; ++i)        // упрощённо побайтно через край кольца
        if (copy_to_user(ubuf + i, &d->buf[(d->head + i) % BUF_SIZE], 1))
            { mutex_unlock(&d->lock); return -EFAULT; }
    d->head = (d->head + n) % BUF_SIZE;   // ПОТРЕБИЛИ данные
    mutex_unlock(&d->lock);
    wake_up_interruptible(&d->write_wq);  // освободилось место → разбудить писателей
    return n;
}
```

Ключевое отличие от учебного буфера: данные **уходят** при чтении (FIFO), `*ppos`
для устройства-потока обычно **игнорируется** (нет «позиции» в потоке — `.llseek =
no_llseek` или `noop_llseek`). Так устроены пайпы (C3) и tty. Кольцо + wait queue +
лок = классический драйвер символьного устройства-потока.

---

## 9. `container_of` — связь объектов в ядре

### 9.1 Идиома №1 ядра

Ядро **сплошь** построено на `container_of` (Ф1 §9.6): структуры **встраивают** друг
друга, и по адресу встроенного члена получают адрес объемлющей структуры:

```c
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
```

Зачем в драйвере: VFS даёт тебе указатель на **встроенный** объект (`struct cdev *`,
`struct inode *`), а ты хочешь **свою** объемлющую структуру устройства:

```c
struct my_device {
    struct cdev cdev;        // встроенный — за него зацепится ядро
    char *buffer;
    size_t len;
    struct mutex lock;
    // ... твоё состояние ...
};

static int my_open(struct inode *inode, struct file *file)
{
    // inode->i_cdev указывает на ВСТРОЕННЫЙ cdev; получаем СВОЮ структуру:
    struct my_device *dev = container_of(inode->i_cdev, struct my_device, cdev);
    file->private_data = dev;          // сохранить для read/write
    return 0;
}
static ssize_t my_read(struct file *file, char __user *ubuf, size_t n, loff_t *ppos)
{
    struct my_device *dev = file->private_data;   // забрать в read
    // ... работать с dev->buffer под dev->lock ...
}
```

### 9.2 Почему так, а не глобальные переменные

В упражнении (один экземпляр) можно глобальными переменными. Но **правильный** драйвер
поддерживает **много** устройств (minor 0, 1, 2...), и состояние каждого — в **своей**
`struct my_device`. `container_of` + `private_data` дают доступ к нужному экземпляру
без глобалей. Это самодельный «полиморфизм» ядра (C2 §8.5): встроенная структура +
`container_of` = объект знает свой контекст. `struct file_operations`,
`struct net_device`, списки `list_head` — всё на этом.

### 9.3 `list_head` — ядерные связные списки

Прямое применение `container_of` — **встроенные** списки ядра (`<linux/list.h>`).
Узел списка `struct list_head` **встраивается** в твою структуру; макросы обхода
дают обратно твою структуру через `container_of`:

```c
struct my_item {
    int value;
    struct list_head node;        // ВСТРОЕННЫЙ узел списка
};
static LIST_HEAD(items);          // голова списка
static DEFINE_MUTEX(items_lock);

// добавить:
struct my_item *it = kzalloc(sizeof *it, GFP_KERNEL);
it->value = 42;
mutex_lock(&items_lock);
list_add_tail(&it->node, &items);
mutex_unlock(&items_lock);

// обойти (list_for_each_entry скрывает container_of):
struct my_item *p;
list_for_each_entry(p, &items, node)        // p — СВОЯ структура, не list_head
    pr_info("value=%d\n", p->value);

// удалить безопасно при итерации:
struct my_item *tmp;
list_for_each_entry_safe(p, tmp, &items, node) {
    list_del(&p->node);
    kfree(p);
}
```

Это **тот же** приём, что `container_of` для `cdev` (§9.1): структура данных
**встроена** в объект, а не объект содержит указатель на узел. Так устроены **все**
списки ядра (процессы, файлы, страницы) — типобезопасно, без `void*`. Хеш-таблицы
(`hlist`), red-black деревья (`rb_node`) — аналогично, встраиванием.

---

## 10. `ioctl`: кодирование команд, безопасность

### 10.1 Зачем `ioctl`

`read`/`write` — поток байт. Для **управляющих** операций (настроить устройство,
узнать статус, сбросить буфер) — `ioctl` (input/output control): команда + аргумент.

```c
// userspace:
ioctl(fd, CPPCHAR_CLEAR);              // команда без аргумента
ioctl(fd, CPPCHAR_GET_LEN, &len);      // команда с аргументом (указатель)
```

### 10.2 Кодирование команд: `_IO`/`_IOR`/`_IOW`/`_IOWR`

Номер команды — **не** произвольное число. Его **кодируют** макросами, чтобы в нём
были: «magic» (уникальный байт драйвера), номер, **направление** и **размер**
аргумента:

```c
#include <linux/ioctl.h>
#define CPPCHAR_MAGIC 'C'
#define CPPCHAR_CLEAR    _IO(CPPCHAR_MAGIC, 0)              // без аргумента
#define CPPCHAR_GET_LEN  _IOR(CPPCHAR_MAGIC, 1, int)       // читать в userspace (R)
#define CPPCHAR_SET_MODE _IOW(CPPCHAR_MAGIC, 2, int)       // писать из userspace (W)
#define CPPCHAR_SWAP     _IOWR(CPPCHAR_MAGIC, 3, int)      // в обе стороны
```

Кодирование даёт **безопасность**: ядро (и драйвер) могут проверить, что команда
адресована **этому** драйверу (magic) и что направление/размер совпадают — чужой/
ошибочный `ioctl` отсекается.

### 10.3 Обработчик `unlocked_ioctl`

```c
static long my_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    struct my_device *dev = f->private_data;
    // 1) проверить magic — команда точно наша?
    if (_IOC_TYPE(cmd) != CPPCHAR_MAGIC) return -ENOTTY;
    // 2) (опц.) проверить номер в допустимом диапазоне
    if (_IOC_NR(cmd) > CPPCHAR_MAXNR)    return -ENOTTY;

    switch (cmd) {
    case CPPCHAR_CLEAR:
        mutex_lock(&dev->lock); dev->len = 0; mutex_unlock(&dev->lock);
        return 0;
    case CPPCHAR_GET_LEN: {
        int len = (int)dev->len;
        if (put_user(len, (int __user *)arg)) return -EFAULT;   // безопасно в userspace
        return 0;
    }
    case CPPCHAR_SET_MODE: {
        int mode;
        if (get_user(mode, (int __user *)arg)) return -EFAULT;
        // ... применить mode ...
        return 0;
    }
    default:
        return -ENOTTY;          // неизвестная команда → -ENOTTY (НЕ -EINVAL!)
    }
}
```

Правила безопасности `ioctl`:
- **Проверяй magic/номер** — отсекай чужие команды (`-ENOTTY`).
- **Никогда не разыменовывай `arg`** как указатель напрямую — это userspace-адрес,
  только `get_user`/`put_user`/`copy_*_user`.
- **`-ENOTTY`** для неизвестной команды (исторически «not a typewriter» — стандартный
  код «неподдерживаемый ioctl»), не `-EINVAL`.
- **`unlocked_ioctl`** (не старый `.ioctl`) — без Big Kernel Lock; синхронизацию
  делаешь сам. Для 32-битного userspace на 64-битном ядре — ещё `.compat_ioctl`.

`ioctl` — мощный, но «грязный» интерфейс (непрозрачные команды); в новом коде иногда
предпочитают `sysfs`/`configfs` для конфигурации (§17.5). Но для драйверов `ioctl`
повсеместен.

### 10.4 Передача структур и compat

Часто `ioctl` передаёт **структуру** (не скаляр) — через указатель в `arg`:

```c
struct cppchar_config { int mode; int timeout_ms; };
#define CPPCHAR_SET_CONFIG _IOW(CPPCHAR_MAGIC, 4, struct cppchar_config)

case CPPCHAR_SET_CONFIG: {
    struct cppchar_config cfg;
    if (copy_from_user(&cfg, (void __user *)arg, sizeof cfg))   // целую структуру
        return -EFAULT;
    if (cfg.timeout_ms < 0) return -EINVAL;                     // ВАЛИДИРУЙ поля!
    // ... применить ...
    return 0;
}
```

**Почему `unlocked_ioctl`, а не `.ioctl`?** Старый `.ioctl` вызывался под **BKL**
(Big Kernel Lock — глобальный лок всего ядра, исторический атавизм): он сериализовал
**все** ioctl в системе. `unlocked_ioctl` («без BKL») вызывается **без** него —
синхронизацию ты делаешь **сам** (своим локом, §14), что масштабируемо. BKL давно
удалён; в новом коде только `unlocked_ioctl`.

Два правила безопасности:
- **Валидируй каждое поле** структуры из userspace — это **недоверенный ввод** (как
  сетевой пакет, C3): диапазоны, длины, индексы. Иначе userspace заставит драйвер
  выйти за границы.
- **`.compat_ioctl`** — 32-битный userspace на 64-битном ядре передаёт структуры с
  **другим** layout (размеры/выравнивание `long`/указателей отличаются). Если драйвер
  обслуживает 32-бит процессы на 64-бит ядре — нужен `.compat_ioctl`, который
  конвертирует структуры. Для одинаковой разрядности — можно `.compat_ioctl =
  compat_ptr_ioctl`.

---

## 11. Блокирующий I/O и wait queues

### 11.1 Проблема

Что если на `read` **данных ещё нет** (устройство не готово)? Два поведения, как в
C2:

- **Блокирующее** (по умолчанию): **усыпить** процесс, пока данные не появятся.
- **Неблокирующее** (`O_NONBLOCK`): сразу вернуть `-EAGAIN`.

Усыпление в ядре делается через **wait queue** (очередь ожидания).

### 11.2 Wait queue

```c
#include <linux/wait.h>
#include <linux/sched.h>
static DECLARE_WAIT_QUEUE_HEAD(read_wq);   // очередь ожидающих чтения
static int data_ready = 0;                  // условие

// В read: усыпить, пока нет данных
static ssize_t my_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    if (!data_ready) {
        if (f->f_flags & O_NONBLOCK)
            return -EAGAIN;                  // неблокирующий — сразу отказ
        // блокирующий — уснуть до условия (прерываемо сигналом):
        if (wait_event_interruptible(read_wq, data_ready))
            return -ERESTARTSYS;             // разбужен сигналом → пусть libc перезапустит
    }
    // ... данные есть, копируем ...
}

// В write (или в обработчике прерывания, K3): разбудить ждущих
static ssize_t my_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
    // ... сохранили данные ...
    data_ready = 1;
    wake_up_interruptible(&read_wq);         // разбудить тех, кто ждёт в read_wq
    return count;
}
```

- **`wait_event_interruptible(wq, cond)`** — атомарно проверяет `cond`; если ложно —
  усыпляет в `wq`; просыпается, когда кто-то сделал `wake_up` **и** `cond` стало
  истинным (перепроверяет — как предикат-цикл condvar в C1/C2!). Возвращает
  ненулевое, если разбужен **сигналом** → верни `-ERESTARTSYS` (ядро/libc решат,
  перезапустить сисколл или отдать `EINTR`, Ф2).
- **`wake_up_interruptible(&wq)`** — разбудить ждущих в очереди.

> **Параллель с C1/C2:** wait queue = condition variable ядра. `wait_event` сам
> делает «проверка условия в цикле» (spurious wakeup невозможен — он перепроверяет
> `cond`). `wake_up` = `notify`. Та же модель, что producer-consumer в C1, но
> примитивы ядерные.

### 11.3 `O_NONBLOCK` — уважать флаг

Драйвер **обязан** проверять `file->f_flags & O_NONBLOCK`: при нём — `-EAGAIN`
вместо сна. Без этого неблокирующий userspace (event-loop из C2!) **зависнет** в
`read`. Это контракт неблокирующего I/O со стороны драйвера.

### 11.4 Варианты `wait_event` и грабли

| Вариант | Поведение |
|---------|-----------|
| `wait_event(wq, cond)` | усыпить НЕпрерываемо (нельзя убить сигналом — избегай!) |
| `wait_event_interruptible(wq, cond)` | прерываемо сигналом (возврат → `-ERESTARTSYS`) |
| `wait_event_interruptible_timeout(wq, cond, t)` | + таймаут (jiffies); 0 = таймаут |
| `wait_event_killable(wq, cond)` | прерывается только фатальными сигналами |

Грабли:
- **Условие проверяй в макросе, не до него.** `wait_event` атомарно проверяет `cond`
  под защитой очереди — нет «потерянного пробуждения» (lost wakeup, C2 §17). Своя
  ручная схема `if(!cond) sleep()` — гонка; используй `wait_event_*`.
- **`-ERESTARTSYS` при прерывании** — верни его, не игнорируй: ядро либо перезапустит
  сисколл (если `SA_RESTART`, Ф2), либо отдаст `EINTR` userspace. Проигнорируешь —
  процесс нельзя будет убить во время `read`.
- **Буди после изменения условия.** Сначала установи `cond = 1` (под локом), потом
  `wake_up` — иначе разбуженный перепроверит `cond`, увидит ложь и снова уснёт.
- **`wake_up` vs `wake_up_interruptible`** — парь с тем, как усыплял; будит **всех** в
  очереди (они перепроверяют `cond`). `wake_up_interruptible_sync` — не вытеснять
  немедленно (микрооптимизация).

`jiffies` — счётчик тиков ядра (время); `HZ` тиков в секунду; таймаут задают в
`jiffies` (`msecs_to_jiffies(100)`).

### 11.5 Задержки и сон в драйвере

Драйверу часто нужно **подождать** (железо медленное). Чем — зависит от контекста и
длительности:

| Примитив | Спит? | Когда |
|----------|:---:|--------|
| `udelay(n)`/`ndelay(n)` | **нет** (занятое ожидание) | **короткие** (мкс/нс) задержки, в т.ч. в атомарном контексте |
| `msleep(n)` | **да** | миллисекунды, process context (не точно — округляет вверх) |
| `usleep_range(min,max)` | **да** | точнее msleep, мкс-диапазон, process context |
| `schedule_timeout_interruptible(t)` | **да** | уступить CPU на t jiffies, прерываемо |

Правило: в **атомарном** контексте (под спинлоком, прерывание) — **только** busy-wait
(`udelay`), сон **запрещён** (§3.2). В process context — `msleep`/`usleep_range`
(уступают CPU, не жгут такты). Долгая `udelay` (миллисекунды) — **ошибка**: жжёт ядро.
Для «ждать события» — wait queue (§11.2), а не активная задержка.

> **Параллель с C6:** busy-wait (`udelay`) vs сон (`msleep`) — то же spin vs block из
> C6 §14.5: короткое ожидание крутиться дешевле, длинное — уступить CPU. В ядре выбор
> ещё жёстче ограничен контекстом.

---

## 12. `poll`/epoll-совместимость: `poll_wait`

Вопрос ЭКСПЕРТ трека и прямой мост в C2/C4: как сделать драйвер совместимым с
`poll`/`select`/`epoll`.

### 12.1 Что должен делать `.poll`

Чтобы userspace мог ждать твоё устройство в `epoll` (C2), драйвер реализует
`.poll`. Он делает **две** вещи:

1. **Регистрирует** wait queue(s) устройства в poll-таблице вызывающего —
   `poll_wait(file, &wq, pt)` (НЕ усыпляет, только «подписывает»);
2. **Возвращает маску** готовности (`EPOLLIN`/`EPOLLOUT`/...) — что **сейчас** готово.

```c
#include <linux/poll.h>
static __poll_t my_poll(struct file *f, struct poll_table_struct *pt)
{
    __poll_t mask = 0;
    struct my_device *dev = f->private_data;

    poll_wait(f, &dev->read_wq, pt);    // подписать на очередь чтения
    poll_wait(f, &dev->write_wq, pt);   // и записи (если нужно)

    mutex_lock(&dev->lock);
    if (dev->len > 0)            mask |= EPOLLIN | EPOLLRDNORM;   // есть что читать
    if (dev->len < BUF_SIZE)     mask |= EPOLLOUT | EPOLLWRNORM;  // есть место писать
    mutex_unlock(&dev->lock);
    return mask;
}
```

### 12.2 Как это работает с `epoll_wait`

```
userspace: epoll_wait()  ──> ядро зовёт твой .poll(file, pt)
   .poll: poll_wait(подписать на dev->read_wq) + вернуть маску (что готово сейчас)
   если ничего не готово → epoll_wait усыпляет процесс (он подписан на твою wq)
   ...устройство получило данные → ТЫ зовёшь wake_up(&dev->read_wq)...
   → ядро будит подписанных → epoll_wait снова зовёт .poll → видит EPOLLIN → возвращает
```

Это **обратная сторона** C2/C4: там ты делал `epoll_wait` и получал `EPOLLIN`;
**здесь** ты — драйвер, который через `poll_wait` + `wake_up` **делает** fd
читаемым. `signalfd`/`timerfd`/`eventfd` (C4 §7.6) устроены **ровно так** изнутри.
`poll_wait` не усыпляет — он только регистрирует очередь; усыпляет сам `epoll_wait`/
`poll`, а будит твой `wake_up`.

### 12.3 Связка: read блокирующий + poll

`.read` (с `wait_event`) и `.poll` (с `poll_wait`) используют **те же** wait
queue'и и **то же** условие готовности. `wake_up` будит **обоих** — и спящих в
блокирующем `read`, и подписанных через `epoll`. Один механизм обслуживает оба
способа ожидания. Это и есть «драйвер, совместимый с poll/epoll» из критерия трека.

### 12.4 Worked: userspace-клиент на epoll + драйвер

Так замыкается круг C2↔K1 — userspace ждёт **твоё** устройство в `epoll`:

```c
// userspace (C2): ждать данные от драйвера через epoll
int fd = open("/dev/cppchar", O_RDONLY | O_NONBLOCK);
int ep = epoll_create1(EPOLL_CLOEXEC);
struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev);
for (;;) {
    struct epoll_event out[8];
    int n = epoll_wait(ep, out, 8, -1);          // спит, пока драйвер не разбудит
    for (int i = 0; i < n; ++i) {
        char buf[256];
        ssize_t r = read(fd, buf, sizeof buf);    // данные уже есть (EPOLLIN)
        // ... обработать r байт ...
    }
}
```

Что происходит под капотом:
```
epoll_wait ──> ядро зовёт твой .poll ──> poll_wait(подписать на dev->read_wq)
            ──> .poll вернул 0 (нет данных) ──> epoll_wait усыпил процесс (он в read_wq)
... драйвер получил данные (write/прерывание) ──> wake_up(&dev->read_wq) ...
            ──> ядро будит процесс ──> epoll_wait снова зовёт .poll ──> EPOLLIN ──> вернул
            ──> userspace делает read() ──> твой .read отдаёт данные
```

Драйвер обязан: `.poll` с `poll_wait` + корректной маской; `.read` уважает
`O_NONBLOCK` (вернуть `-EAGAIN`, если пусто, — иначе epoll-клиент с неблокирующим fd
сломается); `wake_up` после появления данных. Это **тот же** механизм, что внутри
`signalfd`/`timerfd`/`eventfd` (C4) — теперь ты его реализуешь.

---

## 13. `mmap` в драйвере: `remap_pfn_range`

### 13.1 Зачем

`.mmap` позволяет userspace **отобразить** память драйвера/устройства в своё
адресное пространство и работать с ней как с обычной памятью — **без** `read`/`write`
(zero-copy, C3/C6). Так отдают буферы кадров (фреймбуфер), регистры устройств (MMIO),
большие разделяемые буферы.

### 13.2 `remap_pfn_range`

`.mmap` должен **отобразить** физические страницы драйвера в VMA (область памяти)
процесса:

```c
static int my_mmap(struct file *f, struct vm_area_struct *vma)
{
    struct my_device *dev = f->private_data;
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn  = virt_to_phys(dev->buffer) >> PAGE_SHIFT;  // page frame number

    if (size > dev->buf_size) return -EINVAL;       // не дать отобразить больше, чем есть
    // отобразить физические страницы [pfn..] в [vma->vm_start..]:
    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
        return -EAGAIN;
    return 0;
}
```

- **`remap_pfn_range(vma, addr, pfn, size, prot)`** — строит таблицы страниц,
  отображая физические фреймы (`pfn` — page frame number) в виртуальный диапазон VMA
  процесса. После этого userspace читает/пишет память драйвера напрямую.
- **`PAGE_SHIFT`/`PAGE_SIZE`** — гранулярность (4 КБ); адреса/размеры выравнивай на
  страницу.
- Память под `mmap` должна быть подходящей (физически непрерывной для простого
  `remap_pfn_range`; для разрозненной — `vm_fault`-обработчик постранично).

### 13.3 Параллель с C3/C6

Это та же `mmap`, что в C3 (shared memory) и C6 (zero-copy), но **ты — поставщик**
страниц. Userspace `mmap(fd)` → VFS зовёт твой `.mmap` → ты `remap_pfn_range` →
процесс видит память устройства. Драйверы GPU/видео/DMA так отдают огромные буферы
без копирования.

### 13.4 Разрозненная память и MMIO

`remap_pfn_range` подходит для **физически непрерывной** памяти (`kmalloc`, выделенный
DMA-буфер). Для других случаев:

- **`vmalloc`-память** (физически разрозненная) — нельзя одним `remap_pfn_range`;
  нужен обработчик **`.fault`** в `vm_operations_struct`: ядро зовёт его на page fault,
  и ты постранично возвращаешь нужную страницу (`vmf_insert_page`/`vmalloc_to_page`).
- **MMIO** (регистры устройства, физические адреса железа) — `io_remap_pfn_range`
  (учитывает некэшируемость) + `pgprot_noncached(vma->vm_page_prot)`, чтобы доступ к
  регистрам не кэшировался (как `volatile`/`readl` для MMIO, Ф1).
- **Защита размера/прав** — всегда проверяй `vma->vm_end - vma->vm_start` ≤ размера
  буфера и права (`vma->vm_flags & VM_WRITE`), иначе userspace отобразит больше/с
  лишними правами → выход за память драйвера.

`.fault`-подход также даёт **ленивую** подгрузку (страница появляется при первом
обращении, как demand paging, C6 §12) — полезно для больших/разреженных мэппингов.

### 13.5 Мостик: DMA и zero-copy железа

`mmap` отдаёт память драйвера userspace. Следующий шаг (K3) — **DMA** (Direct Memory
Access): устройство **само** пишет/читает память **без** участия CPU. Драйвер
выделяет DMA-совместимый буфер (`dma_alloc_coherent` — физически непрерывный,
когерентный), даёт его адрес устройству, и железо льёт данные напрямую. Это
zero-copy на уровне **железа** (C6): данные идут устройство ↔ RAM, минуя CPU и копии.
Сетевые карты, диски, GPU так передают гигабайты. `mmap` этого DMA-буфера в userspace
= нулевое копирование от железа до приложения. Полный разбор DMA, прерываний
(устройство сигналит «готово» через IRQ → твой обработчик → `wake_up` ждущих в `read`/
`poll`) — модуль K3. Здесь важно видеть: `mmap` + wait queue + прерывание = как
**реальное** устройство отдаёт данные в epoll-loop userspace (полный круг C2↔K1↔K3).

---

## 14. Конкурентный доступ: mutex/spinlock/atomic

Критерий трека: «корректно работающий с конкурентным доступом». В ядре конкурентность
— **по умолчанию**: несколько процессов открыли устройство, код вызывается параллельно
на разных ядрах, вытесняется. Состояние — под защитой с первой строки.

### 14.1 Какой примитив когда

| Примитив | Можно спать в секции? | Когда |
|----------|:---:|--------|
| **`mutex`** | **да** | process context, секция может спать (наш `read`/`write` с `copy_*_user`) |
| **`spinlock`** | **нет** | короткие секции, в т.ч. атомарный контекст (прерывание, softirq) |
| **`atomic_t`** | — | один счётчик/флаг без структуры (lock-free, C1) |
| **`rwlock`/`seqlock`** | зависит | много читателей, мало писателей |
| **RCU** | — | read-mostly, читатели почти бесплатны (C1 §12, K2) |

### 14.2 Mutex — для нашего драйвера

`read`/`write` зовут `copy_*_user`, которые **могут спать** (§7.3). Значит секция
**может спать** → нужен **мьютекс** (спинлок нельзя — под ним спать запрещено):

```c
#include <linux/mutex.h>
static DEFINE_MUTEX(buf_lock);

mutex_lock(&buf_lock);
// ... доступ к buffer/data_len, в т.ч. copy_*_user (можно спать) ...
mutex_unlock(&buf_lock);
```

### 14.3 Spinlock — когда спать нельзя

Если состояние трогается из **прерывания** (K3) или секция **не спит** — спинлок:

```c
#include <linux/spinlock.h>
static DEFINE_SPINLOCK(state_lock);
unsigned long flags;

spin_lock_irqsave(&state_lock, flags);   // + запретить прерывания на этом ядре
// ... КОРОТКАЯ секция без сна (нет copy_user, нет kmalloc(GFP_KERNEL)) ...
spin_unlock_irqrestore(&state_lock, flags);
```

- **`spin_lock_irqsave`** — если лок берут и из прерывания, и из process context:
  запрещает прерывания, чтобы прерывание не попыталось взять тот же спинлок (deadlock).
- **Под спинлоком НЕЛЬЗЯ спать** — ни `copy_*_user`, ни `kmalloc(GFP_KERNEL)`, ни
  `mutex_lock`. Нарушение = «scheduling while atomic», зависание.

### 14.4 atomic_t — для счётчиков

Для простого счётчика/флага без структуры — атомарные операции (C1), без лока:

```c
#include <linux/atomic.h>
static atomic_t open_count = ATOMIC_INIT(0);
atomic_inc(&open_count);                 // в open
atomic_dec(&open_count);                 // в release
int n = atomic_read(&open_count);
```

### 14.5 Дисциплина

- **Защищай ВСЁ разделяемое** с первой строки (буфер, длина, флаги, списки) — драйвер
  параллелен по умолчанию.
- **Минимизируй критическую секцию** (C6 §14): держи лок только вокруг доступа к
  данным, не вокруг всей функции.
- **Никогда не спи под спинлоком** (copy_user, kmalloc(GFP_KERNEL), mutex).
- **Единый порядок** взятия нескольких локов (C1 §9) — против deadlock.
- **Lockdep** (`CONFIG_PROVE_LOCKING`) — ядерный валидатор: ловит неверный порядок
  локов, сон под спинлоком, рекурсию — включай в отладочном ядре.

### 14.5.0 Барьеры памяти в драйвере

Когда драйвер общается с **железом** или lock-free с другим контекстом (прерывание),
порядок записей в память/регистры важен (C1 §4–5). Ядерные барьеры:

- **`smp_wmb()`/`smp_rmb()`/`smp_mb()`** — барьеры между ядрами (как
  release/acquire/seq_cst из C1), когда синхронизируешь без лока (например, флаг
  «данные готовы» + сам буфер).
- **`wmb()`/`rmb()`/`mb()`** — барьеры для **MMIO** (доступ к регистрам устройства):
  гарантируют, что запись в регистр A видна железу до записи в B.
- **`READ_ONCE`/`WRITE_ONCE`** — как `volatile`-доступ (C1 §3.2): запретить
  компилятору кэшировать/переупорядочить **одно** обращение (для флагов между
  контекстами).
- **`smp_load_acquire`/`smp_store_release`** — типизированные acquire/release (C1) —
  предпочтительнее голых барьеров.

В char-драйвере под мьютексом (§14.2) барьеры **не нужны** — лок их содержит (как в
C1: критическая секция упорядочивает). Барьеры всплывают в lock-free путях,
прерываниях (K3) и MMIO. Но знать связь надо: модель памяти C1 — **та же**, примитивы
— ядерные (`smp_*` вместо `std::atomic`).

### 14.5.1 Per-open vs разделяемое состояние

Важно различать **уровень** состояния и где его защищать:

- **Разделяемое устройством** (буфер устройства, FIFO, счётчики) — общее для **всех**
  открытий; в `struct my_device`, под локом устройства (`dev->lock`). Несколько
  процессов через разные `open` видят **одно** устройство.
- **Per-open** (позиция, режим этого открытия, временный контекст) — своё на каждый
  `open`; в `file->private_data` (выделил в `open`, освободил в `release`). Своё у
  каждого fd — лок часто не нужен (один открыватель), но если fd шарят через `fork`/
  `dup` (Ф3) — снова конкуренция.

Типовая ошибка — держать «позицию записи» в глобальной переменной: тогда два процесса
портят позицию друг друга. Решение — `*ppos` (ядро хранит per-`file`) или per-open
поле в `private_data`. Спроси про каждое поле: оно про **устройство** или про **это
открытие**?

### 14.6 Worked: deadlock и что говорит lockdep

Классическая ошибка драйвера — **сон под спинлоком** (`copy_to_user` держит спинлок):

```c
spin_lock(&dev->lock);
if (copy_to_user(ubuf, dev->buf, n)) ...   // БАГ: copy_*_user может СПАТЬ под спинлоком
spin_unlock(&dev->lock);
```

Под отладочным ядром это даёт:
```
BUG: sleeping function called from invalid context at .../uaccess.c
in_atomic(): 1, irqs_disabled(): 0, pid: 412, name: cat
Call Trace:
 __might_sleep+...
 copy_to_user+...
 my_read+0x...  [cppchar]      ← твоя строка
```
Лечение: использовать **мьютекс** (sleepable, §14.2), не спинлок, когда в секции есть
`copy_*_user`/`kmalloc(GFP_KERNEL)`/ожидание.

Lockdep ловит и **неверный порядок** двух локов:
```
======================================================
WARNING: possible circular locking dependency detected
 CPU0: lock(A); lock(B)      CPU1: lock(B); lock(A)   ← цикл (C1 §9)
```
Лечение — **единый глобальный порядок** взятия локов (C1). Lockdep находит это даже
если deadlock **ещё не случился** (как helgrind, C1/Ф4). Поэтому драйвер разрабатывают
на ядре с `CONFIG_PROVE_LOCKING` + `CONFIG_DEBUG_ATOMIC_SLEEP` — они превращают
«иногда виснет в проде» в чёткое сообщение со стеком.

---

## 15. Память ядра: `kmalloc`/`vmalloc`, GFP

### 15.1 Нет `malloc` — есть `kmalloc`

```c
#include <linux/slab.h>
char *p = kmalloc(size, GFP_KERNEL);     // как malloc, НЕ обнулён
char *z = kzalloc(size, GFP_KERNEL);     // обнулённый (как calloc)
kfree(p);                                 // освободить
```

- **`kmalloc`** — небольшие (< ~128 КБ), **физически непрерывные** блоки (важно для
  DMA). Возвращает kernel-виртуальный адрес, который и физически непрерывен.
- **`vmalloc`** — большие блоки, **виртуально** непрерывные (физически — нет);
  медленнее, не для DMA. Для больших буферов, где физнепрерывность не нужна.
- **`kzalloc`** — `kmalloc` + обнуление. Предпочитай (нет мусора).

### 15.2 GFP-флаги — контекст аллокации

Второй аргумент — **как** ядру выделять:

- **`GFP_KERNEL`** — обычная аллокация в **process context**; **может спать** (ждать
  освобождения памяти). Нельзя в атомарном контексте/под спинлоком.
- **`GFP_ATOMIC`** — **не спит**; для атомарного контекста (прерывание, под
  спинлоком). Может чаще не хватить памяти (не ждёт).
- **`GFP_DMA`** — память, пригодная для DMA (старое железо).

Выбор GFP — частая ошибка: `GFP_KERNEL` под спинлоком/в прерывании = «scheduling
while atomic». В наших `read`/`write` (process context, не под спинлоком) —
`GFP_KERNEL`.

### 15.3 Прочее

- **`devm_*`** (managed) — `devm_kzalloc(dev, ...)` автоматически освобождается при
  отвязке устройства (меньше ручного `kfree`); идиома современных драйверов.
- **slab-аллокаторы** (`kmem_cache_create`) — пул объектов фиксированного размера
  (как арена C3/C6), для частых аллокаций одного типа.

### 15.4 `devm_*` и slab-кэш на практике

**Managed-аллокации** (`devm_*`) привязывают ресурс к устройству и **сами**
освобождают его при отвязке — меньше ручного `kfree` и забытых утечек:

```c
// вместо kzalloc(...)+kfree в exit:
dev->buf = devm_kzalloc(dev, BUF_SIZE, GFP_KERNEL);   // освободится автоматически
// есть devm_* для многого: devm_request_irq, devm_ioremap, devm_kmalloc...
```

`devm_*` — идиома **современных** драйверов (особенно с device tree, EL2): захватил
ресурс через `devm_` — не думаешь о его освобождении (ядро сделает при удалении
устройства). Это «почти-RAII» ядра (как `__attribute__((cleanup))` из Ф1).

**Slab-кэш** — для **частых** аллокаций объектов **одного** типа (узлы списка,
запросы): пул переиспользуемых блоков, без фрагментации, с лучшей кэш-локальностью
(C6):

```c
static struct kmem_cache *req_cache;
req_cache = kmem_cache_create("cppchar_req", sizeof(struct req), 0, SLAB_HWCACHE_ALIGN, NULL);
struct req *r = kmem_cache_alloc(req_cache, GFP_KERNEL);   // из пула
kmem_cache_free(req_cache, r);
// exit: kmem_cache_destroy(req_cache);
```

`SLAB_HWCACHE_ALIGN` выравнивает объекты по кэш-линии (против false sharing, C6 §5).
Это тот же принцип пула/арены, что в C3/C6, но встроенный в ядро.

### 15.5 Отладка памяти ядра

В ядре нет valgrind, но есть встроенные детекторы (включаются в конфиге ядра):

- **KASAN** (`CONFIG_KASAN`) — out-of-bounds и use-after-free в ядерной памяти, с
  точным стеком (как ASan, Ф4). Главный инструмент против порчи памяти драйвером.
- **kmemleak** (`CONFIG_DEBUG_KMEMLEAK`) — детектор **утечек**: находит `kmalloc`-блоки,
  на которые не осталось ссылок (`echo scan > /sys/kernel/debug/kmemleak`). Ловит
  забытый `kfree` в драйвере.
- **SLAB poisoning** (`CONFIG_SLUB_DEBUG`, `slub_debug=`) — заполняет освобождённую
  память маркером (`0x6b`...) и проверяет «красные зоны» вокруг блоков → ловит
  use-after-free и переполнения.
- **KFENCE** — лёгкий сэмплирующий детектор для **прода** (малый оверхед).

Дисциплина: разрабатывай драйвер на ядре с KASAN + kmemleak + lockdep +
`DEBUG_ATOMIC_SLEEP`. Они превращают «редкий краш под нагрузкой» в детерминированное
сообщение со стеком — как ASan/TSan для userspace, но для ядра.

---

## 16. Отладка: `printk`, `dmesg`, oops, KASAN

### 16.1 `printk`/`pr_*` и `dmesg`

Драйвер **не пишет в stdout** — он пишет в **журнал ядра** (`printk`), читаемый
`dmesg`/`journalctl -k`:

```c
pr_info("cppchar: loaded major=%d\n", MAJOR(dev_num));   // KERN_INFO
pr_err("cppchar: alloc failed\n");                        // KERN_ERR
pr_debug("cppchar: read %zu bytes\n", count);             // только при DEBUG/dynamic debug
// dev_info(dev, ...) / dev_err(dev, ...) — с привязкой к устройству (предпочтительно)
```

- Уровни (`pr_emerg`..`pr_debug`) = syslog-уровни (как `<N>` в C5 §10). `dmesg -l err`
  фильтрует.
- **Не спамь на горячем пути** — `printk` дорог и под локом; для трассировки —
  `pr_debug` (dynamic debug) или **ftrace**/**trace_printk** (K7).

### 16.2 Oops и паника

Ошибка в ядре (NULL-дереф, выход за буфер) даёт **oops** — дамп в `dmesg`:

```
BUG: kernel NULL pointer dereference, address: 0000000000000000
Call Trace:
 my_read+0x2a/0x80 [module]      ← твоя функция и смещение
 vfs_read+0x...
 ...
```

- **Oops** — поток убит, но ядро **может** выжить (если не критичное место); устройство
  в неконсистентном состоянии.
- **Паника** — ядро остановилось (критичная ошибка); машину перезагружать.
- Читай **Call Trace**: верхняя строка с `[module]` — где упало в твоём коде
  (`addr2line`/`gdb` по `.ko` + смещение, как Ф4). Отсюда — отлаживай в **QEMU**: упадёт
  виртуалка, не хост.

### 16.3 Инструменты ядра

- **KASAN** (`CONFIG_KASAN`) — ASan для ядра: ловит out-of-bounds/use-after-free в
  ядерной памяти (как ASan в userspace, Ф4). Включай в отладочном ядре.
- **lockdep** (`CONFIG_PROVE_LOCKING`) — валидатор локов (§14.5).
- **KCSAN** — детектор гонок данных в ядре (как TSan).
- **`CONFIG_DEBUG_*`** — масса проверок (slab, объекты, стек).
- **ftrace** (`/sys/kernel/debug/tracing`) — трассировка функций ядра (K7).
- **dynamic debug** — включать `pr_debug` без пересборки: `echo 'module cppchar +p' >
  /sys/kernel/debug/dynamic_debug/control` → твои `pr_debug` начинают печататься.
  Удобно: оставляешь `pr_debug` в коде, включаешь точечно при отладке, в проде — тихо.
- **`trace_printk`** — как `printk`, но в ring buffer ftrace (быстрее, не засоряет
  dmesg) — для трассировки горячего пути драйвера без оверхеда `printk`.

Собирай отладочное ядро/QEMU с KASAN+lockdep при разработке драйвера — они ловят то,
что иначе «иногда падает в проде».

### 16.4 Разбор oops: где упал твой код

```
[ 99.1] BUG: kernel NULL pointer dereference, address: 0000000000000008
[ 99.1] RIP: 0010:my_read+0x3a/0x90 [cppchar]      ← функция+смещение в ТВОЁМ модуле
[ 99.1] Call Trace:
[ 99.1]  vfs_read+0x9d/0x190
[ 99.1]  ksys_read+0x6f/0xf0
[ 99.1]  do_syscall_64+0x59/0x90
```

Шаги локализации (как Ф4, но для `.ko`):
1. **`RIP`** — где именно: `my_read+0x3a` (смещение 0x3a в функции `my_read`).
2. **Сопоставить со строкой** — по неоптимизированному `.ko` с отладочной инфой:
   ```sh
   # точная строка по смещению в функции:
   gdb -batch -ex "info line *(my_read+0x3a)" cppchar.ko
   # или готовый ядерный скрипт, который разбирает весь oops:
   ./scripts/decode_stacktrace.sh vmlinux < oops.txt
   ```
3. **`address: ...0008`** — разыменован адрес 8 = `NULL + offsetof` поля по смещению
   8 (часто — `dev->field`, где `dev == NULL`). Здесь подсказка: забыл проверить/
   назначить `private_data`, или `container_of` от NULL.

Главное: oops содержит **точное** место (`RIP` + Call Trace с `[module]`) — не гадай,
декодируй. Отлаживай в **QEMU** (упадёт виртуалка), собранном с отладочной инфой и
KASAN.

### 16.5 Галерея типичных багов драйвера

1. **Разыменовал `__user`-указатель напрямую** → oops / дыра безопасности. *Лечение:*
   только `copy_*_user`/`get_user` (§7).
2. **Не двигаешь `*ppos` / не возвращаешь 0 на EOF** → `cat` зацикливается (§8.1).
3. **Нет `.owner = THIS_MODULE`** → `rmmod` из-под открытого fd → use-after-free кода
   модуля (§5.5).
4. **`copy_*_user`/`kmalloc(GFP_KERNEL)`/`mutex` под спинлоком** → «scheduling while
   atomic» (§14.6).
5. **`GFP_KERNEL` в атомарном контексте** (прерывание) → тот же баг (§15.2).
6. **Не защитил разделяемое состояние** → гонка при нескольких процессах (§14).
7. **Откат в init не зеркальный / забыл освободить** → утечка/частичная регистрация;
   `kfree` буфера до `cdev_del` → use-after-free (§5.3).
8. **`class_create` со старой сигнатурой** (2 арг) на новом ядре → не собирается (§5.2).
9. **Большой массив на стеке ядра** (8 КБ!) → переполнение стека, паника (§3.2).
10. **Не уважает `O_NONBLOCK`** → неблокирующий epoll-клиент (C2) зависает в `read`
    (§11.3).
11. **`-EINVAL` вместо `-ENOTTY`** на неизвестный ioctl → нарушение конвенции (§10.3).
12. **Не валидирует поля структуры из ioctl** → выход за границы по вине userspace
    (§10.4).

---

## 17. Out-of-tree сборка, Kbuild детально

### 17.1 Kbuild-файлы

```makefile
# Makefile
obj-m += module.o                  # одна цель → module.ko

# несколько файлов в один модуль:
# obj-m += mydrv.o
# mydrv-objs := main.o helpers.o

KDIR ?= /lib/modules/$(shell uname -r)/build
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
```

### 17.2 Что происходит при сборке

1. Kbuild читает `obj-m` → знает, какие `.o` собрать в `.ko`.
2. Компилирует `.c` **с флагами ядра** (специфичные `-D`, `-I`, без libc, со своим
   `-ffreestanding`-окружением).
3. **MODPOST** — проверяет символы (что модуль использует только экспортированные
   ядром), генерирует `.mod.c` с метаданными и **version magic**.
4. Линкует → `.ko`.

### 17.3 Version magic и символы

- **Version magic** — `.ko` помнит версию ядра, под которое собран. `insmod` в
  другое ядро → `-ENOEXEC` («version magic mismatch»). Собирай против **своего**
  `uname -r`.
- **Экспортированные символы** — модуль может звать только функции, которые ядро
  **экспортировало** (`EXPORT_SYMBOL`/`EXPORT_SYMBOL_GPL`). `MODULE_LICENSE("GPL")`
  открывает доступ к `_GPL`-символам.
- **`modinfo module.ko`** — version magic, лицензия, зависимости, параметры.

### 17.4.0 Модуль из нескольких файлов и зависимости

```makefile
obj-m += cppchar.o                 # имя модуля = cppchar.ko
cppchar-objs := main.o ring.o ioctl.o   # из этих .o собрать ОДИН модуль
```

- **`cppchar-objs`** — список объектных файлов, линкуемых в **один** `.ko`. Так
  драйвер бьётся на файлы (как обычный проект), но грузится одним модулем.
- **Зависимости между модулями** — если модуль A зовёт `EXPORT_SYMBOL`-функцию
  модуля B, `modprobe` (не `insmod`) загрузит B автоматически (по `depmod`-базе).
  `insmod` зависимости не разрешает — только `modprobe`.
- **`MODULE_DEVICE_TABLE`** — таблица поддерживаемых устройств (для авто-загрузки по
  hotplug/uevent; важно для PCI/USB-драйверов, K-этап).

### 17.4.1 Совместимость с версиями ядра

Внутренний API ядра меняется между версиями (`class_create` потерял аргумент в 6.4,
`file_operations`-поля приходят/уходят). Out-of-tree модуль, который должен собираться
на **разных** ядрах, использует условную компиляцию:

```c
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    my_class = class_create("cppchar");              // новая сигнатура
#else
    my_class = class_create(THIS_MODULE, "cppchar"); // старая
#endif
```

`LINUX_VERSION_CODE`/`KERNEL_VERSION(a,b,c)` — версия собираемого ядра. Это рутина
out-of-tree разработки (драйверы вроде ZFS/NVIDIA полны таких `#if`). In-tree драйверы
этого **не** делают — они правятся **вместе** с ядром, когда API меняется (поэтому
upstream предпочтительнее: чужой код чинит API-changes за тебя). В упражнении целевое
ядро одно (≥6.4), поэтому одноаргументный `class_create`.

### 17.4.2 Подпись модулей и Secure Boot

При включённом **Secure Boot** ядро грузит только **подписанные** модули (иначе
`insmod` → «Key was rejected by service» / `-EKEYREJECTED`). Для разработки: подписать
своим ключом (`scripts/sign-file`), зарегистрировать его в MOK (`mokutil --import`),
отключить Secure Boot, либо разрешить неподписанные (`module.sig_enforce=0`). Поэтому
отлаживают в **QEMU** без Secure Boot — там грузится любой `.ko`. На проде драйвер
подписывают доверенным ключом дистрибутива/вендора.

### 17.4.3 Параметры модуля

```c
#include <linux/moduleparam.h>
static int buf_size = 1024;
module_param(buf_size, int, 0644);            // /sys/module/.../parameters/buf_size
MODULE_PARM_DESC(buf_size, "размер буфера");
// insmod module.ko buf_size=4096
```

### 17.5 sysfs — современная альтернатива ioctl для конфигурации

`ioctl` — непрозрачен (бинарные команды, нужна программа). Для **конфигурации** и
**статуса** современные драйверы дают **sysfs**-атрибуты — файлы в `/sys`, читаемые/
писываемые обычным `cat`/`echo` (как `/proc`, но структурированно, один атрибут — один
файл):

```c
// атрибут /sys/class/cppchar/cppchar/data_len (только чтение):
static ssize_t data_len_show(struct device *dev, struct device_attribute *a, char *buf)
{
    struct my_device *d = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%zu\n", d->len);     // sysfs_emit — безопасно (учёт PAGE_SIZE)
}
static DEVICE_ATTR_RO(data_len);                 // _RO=read-only, _RW=read-write, _WO

// при регистрации устройства:
// device_create_file(dev, &dev_attr_data_len);  (или attribute_group)
```

```sh
$ cat /sys/class/cppchar/cppchar/data_len
42                                  # читаемо обычными утилитами, без программы
```

Преимущества sysfs над ioctl: текстово (скрипты, `cat`/`echo`), самодокументируемо,
один атрибут — один файл, права через файловую систему. Правило ядра: **один атрибут
— одно значение** (не пихай всё в один файл). Для конфигурации/статуса — sysfs; для
сложных бинарных команд/потоков данных — ioctl/read/write. Многие драйверы дают и то,
и другое.

---

## 18. Практика и самопроверка

### 18.1 Практические задания (в редакторе курса)

Все четыре — модули ядра: собираются Kbuild, грузятся `insmod`, тестируются из
userspace (`echo`/`cat`) в **QEMU**. Идут от полного пути регистрации к более
простому `miscdevice` и разным аспектам `file_operations`.

1. **`01-char-dev`** — символьный драйвер `/dev/cppchar`: регистрация
   (`alloc_chrdev_region` + `cdev_add` + `class_create`/`device_create`), `read`/
   `write` через буфер ядра с `copy_*_user` и корректным `*ppos`, защита мьютексом.
   *(§4–§8, §14)*
2. **`02-misc-uppercase`** — `/dev/cppupper` на `miscdevice` (упрощённая регистрация
   одной `misc_register`): хранит текст, на `read` переводит его в **верхний
   регистр** — обработка данных на границе `copy_to_user` (скопировать в локальный
   буфер под мьютексом, копировать в userspace вне него). *(§5, §6, §7, §14)*
3. **`03-readonly-info`** — `/dev/cppinfo` только для чтения: `read` отдаёт строку
   через `simple_read_from_buffer`, `write` возвращает **`-EACCES`** — правильные
   коды возврата как контракт с userspace. *(§6, §7)*
4. **`04-open-counter`** — `/dev/cppcount`: `open`/`release` + **`atomic_t`** (поток-
   безопасный счётчик без мьютекса), номер открытия хранится в `file->private_data`;
   `.owner = THIS_MODULE` держит модуль, пока устройство открыто. *(§6, §9, §14)*

После реализации (и для углубления): добавь `ioctl` (§10) для сброса буфера/запроса
длины; сделай `read` **блокирующим** с wait queue (§11) и `.poll` (§12), проверь
совместимость с `epoll` из userspace (C2). Это прямой путь к критерию «совместим с
poll/epoll».

Локальная сборка и проверка (вне автопрогона):

```sh
# 1) собрать модуль против своего ядра:
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules     # → module.ko
# 2) загрузить и проверить (на своей машине ОСТОРОЖНО — лучше в QEMU/VM):
sudo insmod module.ko
ls -l /dev/cppchar                     # узел создан?
echo -n "hello" > /dev/cppchar         # write
cat /dev/cppchar                       # read → hello
dmesg | tail                           # твои pr_info / oops
sudo rmmod module                      # выгрузить, проверить «выгружен» в dmesg
```

> **Отладка драйвера — в QEMU:** упадёт виртуалка, не твой хост. `dmesg` внутри QEMU
> показывает твои `pr_info` и oops. Собирай отладочное ядро с KASAN/lockdep. Баг в
> драйвере на **своём** хосте может его подвесить — потому курс гоняет модуль в
> QEMU.

### 18.2 Вопросы для самопроверки

1. Что такое LKM и char device? Что такое major и minor, и зачем `dev_t`?
2. Зачем `MODULE_LICENSE` и `__init`/`__exit`? Что вернёт `module_init` при ошибке?
3. Почему модуль собирается против **конкретной** версии ядра (version magic)?
4. Что такое `struct file_operations` и кто его вызывает? Чем `inode` отличается от
   `file`?
5. Почему **нельзя** разыменовать `__user`-указатель напрямую? Что делают
   `copy_*_user` и что возвращают?
6. Зачем `*ppos` в `read`, и что будет, если не двигать его / не возвращать `0` на EOF?
7. Что делает `container_of` и зачем он повсеместно в ядре? Как драйвер получает
   свою структуру из `inode->i_cdev`?
8. Как кодируется команда `ioctl` (`_IOR`/`_IOW`) и зачем magic? Какой код для
   неизвестной команды?
9. Как усыпить процесс до готовности данных (wait queue)? Чем `wait_event_interruptible`
   похож на condition variable из C1?
10. Как сделать драйвер совместимым с `poll`/epoll? Что делает `poll_wait` и что
    возвращает `.poll`? Как это связано с C2/C4?
11. Что делает `remap_pfn_range` и зачем `.mmap` в драйвере?
12. Mutex vs spinlock vs atomic — что когда в драйвере? Почему `copy_*_user` нельзя
    под спинлоком?
13. `GFP_KERNEL` vs `GFP_ATOMIC` — в чём разница и где какой? Что такое «scheduling
    while atomic»?
14. Почему стек ядра маленький и чем это грозит? Куда большие буферы?
15. Чем oops отличается от паники? Где искать, **где** упал твой код?
16. Почему регистрация устройства в `init` идёт с откатом в обратном порядке (как
    goto-cleanup Ф1)?
17. Почему после `cdev_add` устройство уже активно, и что из этого следует?
18. Как deftmpfs создаёт `/dev/cppchar`? Зачем `class_create`+`device_create`?
19. Чем `miscdevice` проще полного `cdev`-пути и когда его брать?
20. Какие инструменты ловят баги драйвера (KASAN, lockdep, KCSAN) и что каждый?

---

## 19. Банк вопросов

> Полные версии (варианты + разборы) — в `quizzes/k1.json`. Ниже — карта тем.

### БАЗА (термины — мгновенно)
1. Что такое LKM и char device.
2. Major/minor и `dev_t`.
3. Что такое `struct file_operations`.
4. Зачем `__user` и `copy_to_user`/`copy_from_user`.
5. Зачем `MODULE_LICENSE` и `module_init`/`module_exit`.
6. Чем kernel space отличается от userspace (нет libc, маленький стек).
7. Что такое `dmesg`/`printk`.
8. Что такое out-of-tree модуль и version magic.

### МЕХАНИЗМЫ (как и почему работает)
1. Регистрация устройства: `alloc_chrdev_region`, `cdev_add`, class/device.
2. `read`/`write`: `*ppos`, частичный I/O, `copy_*_user` и `-EFAULT`.
3. `container_of`: как драйвер связывает `inode`/`file` со своей структурой.
4. `ioctl`: кодирование команд (`_IOR`/`_IOW`), magic, безопасная проверка.
5. Блокирующий I/O: wait queue, `wait_event_interruptible`, `wake_up`, `O_NONBLOCK`.
6. Конкурентность: mutex vs spinlock vs atomic; почему copy_user не под спинлоком.
7. Память ядра: `kmalloc`/`kzalloc`/`vmalloc`, `GFP_KERNEL` vs `GFP_ATOMIC`.
8. Отладка: oops/Call Trace, KASAN, lockdep, QEMU.

### ЭКСПЕРТ (рассуждение)
1. `mmap` в драйвере: `remap_pfn_range`, отдать память устройства userspace.
2. `poll`/epoll-совместимость: `poll_wait` + wake_up + маска; связь с C2/C4 (signalfd/timerfd).
3. Защита состояния драйвера при конкурентном доступе нескольких процессов (выбор
   примитива, минимум секции, lockdep).
4. Контексты исполнения (process/softirq/hardirq) и что в каждом нельзя (сон, GFP).
5. Откат ресурсов в init и порядок exit; почему ошибка в ядре дороже userspace.

### ЗАДАНИЯ
1. `01-char-dev` — char device с read/write (каркас ioctl), мьютекс, QEMU-тест.
2. `02-misc-uppercase` — miscdevice, uppercase на read.
3. `03-readonly-info` — read-only, write → -EACCES, `simple_read_from_buffer`.
4. `04-open-counter` — open/release + atomic_t, номер открытия в private_data.

---

## 20. Глоссарий

*(Акронимы раскрыты: английская расшифровка + короткое пояснение.)*

- **LKM** — Loadable Kernel Module: загружаемый/выгружаемый модуль ядра (`.ko`).
- **`.ko`** — kernel object: скомпилированный модуль ядра.
- **VFS** — Virtual File System: слой ядра, вызывающий `file_operations` драйвера на
  сисколлы над fd.
- **fd** — file descriptor (файловый дескриптор).
- **Char device** — символьное устройство: поток байт через `/dev/...` (`read`/`write`).
- **Major** — номер драйвера (класса устройств).
- **Minor** — номер экземпляра внутри драйвера.
- **`dev_t`** — упакованные major+minor (`MAJOR`/`MINOR`/`MKDEV`).
- **`cdev`** — структура ядра, связывающая (major,minor) с `file_operations`.
- **`file_operations` (fops)** — таблица указателей на методы драйвера (open/read/…).
- **`struct inode`** — представляет файл/узел устройства (один на устройство).
- **`struct file`** — представляет открытый дескриптор (свой на каждый `open`);
  `private_data` — поле под контекст драйвера.
- **`__user`** — аннотация userspace-указателя (для sparse); нельзя разыменовывать.
- **`copy_to_user`/`copy_from_user`** — безопасный обмен с userspace; возвращают
  число НЕскопированных байт.
- **`get_user`/`put_user`** — копирование одного скаляра в/из userspace.
- **`-EFAULT`** — ошибка «плохой адрес» (copy_*_user не смог).
- **`container_of`** — получить объемлющую структуру по адресу встроенного члена.
- **`offsetof`** — смещение члена в структуре (основа container_of).
- **`*ppos`** — позиция чтения/записи, которую драйвер двигает.
- **EOF** — End Of File: `read` вернул `0` (конец данных).
- **`ioctl`** — input/output control: управляющие команды поверх fd.
- **`_IO`/`_IOR`/`_IOW`/`_IOWR`** — макросы кодирования ioctl-команд (magic/номер/
  направление/размер).
- **`-ENOTTY`** — код «неподдерживаемый ioctl» (исторически «not a typewriter»).
- **Wait queue** — очередь ожидания ядра (condition variable ядра).
- **`wait_event_interruptible`** — усыпить до условия (прерываемо сигналом).
- **`wake_up`** — разбудить ждущих в очереди.
- **`-ERESTARTSYS`** — разбужен сигналом; ядро решит перезапустить сисколл или отдать
  `EINTR`.
- **`O_NONBLOCK`** — неблокирующий режим (драйвер обязан вернуть `-EAGAIN`).
- **`.poll` / `poll_wait`** — механизм совместимости с `poll`/`select`/`epoll`.
- **`__poll_t` / `EPOLLIN`/`EPOLLOUT`** — маска готовности fd.
- **`mmap` / `remap_pfn_range`** — отобразить память драйвера в userspace.
- **`pfn`** — Page Frame Number (номер физической страницы).
- **`PAGE_SIZE`/`PAGE_SHIFT`** — размер страницы (4 КБ) и его log2.
- **VMA** — Virtual Memory Area (область адресного пространства процесса).
- **Mutex** — сонный лок (process context, секция может спать).
- **Spinlock** — крутящийся лок (атомарный контекст, спать нельзя).
- **`spin_lock_irqsave`** — спинлок + запрет прерываний (если лок берут из прерывания).
- **`atomic_t`** — атомарный счётчик/флаг без лока.
- **GFP** — Get Free Pages: флаги аллокации (`GFP_KERNEL` спит, `GFP_ATOMIC` нет).
- **`kmalloc`/`kzalloc`/`vmalloc`/`kfree`** — аллокаторы ядра (нет `malloc`).
- **`devm_*`** — managed-аллокации (авто-освобождение при отвязке устройства).
- **Process/softirq/hardirq context** — контексты исполнения (где можно/нельзя спать).
- **«Scheduling while atomic»** — запретный сон в атомарном контексте (баг).
- **`printk`/`pr_info`/`dev_info`** — журналирование ядра (→ `dmesg`).
- **Oops** — дамп ошибки ядра (поток убит, ядро может выжить).
- **Паника (panic)** — критичная остановка ядра.
- **Call Trace** — стек вызовов в oops (где упало).
- **KASAN** — Kernel Address Sanitizer (OOB/UAF в ядре).
- **lockdep** — валидатор корректности локов (порядок, сон под спинлоком).
- **KCSAN** — детектор гонок данных в ядре.
- **Kbuild** — система сборки модулей ядра (`obj-m`, `M=`).
- **Version magic** — версия ядра, зашитая в `.ko` (несовпадение → `insmod` падает).
- **`EXPORT_SYMBOL`/`_GPL`** — экспорт символов ядра для модулей.
- **devtmpfs** — виртуальная ФС `/dev`, авто-создающая узлы по uevent.
- **`miscdevice`** — упрощённая регистрация простого char-устройства (major 10).
- **`IS_ERR`/`PTR_ERR`** — проверка/извлечение ошибки, закодированной в указателе.
- **`THIS_MODULE`** — указатель на структуру текущего модуля (refcount).
- **`current`** — указатель на `task_struct` текущего процесса (`current->pid`).
- **`jiffies` / `HZ`** — счётчик тиков ядра / тиков в секунду (время).
- **`list_head`** — встраиваемый узел двусвязного списка ядра (+ `container_of`).
- **`udelay`/`msleep`/`usleep_range`** — busy-wait / сон (по контексту и длительности).
- **DMA** — Direct Memory Access: устройство пишет/читает RAM без CPU (K3).
- **MMIO** — Memory-Mapped I/O: регистры устройства в адресном пространстве (Ф1).
- **sysfs** — `/sys`-атрибуты для конфигурации/статуса (альтернатива ioctl).
- **`devm_*`** — managed-ресурсы (авто-освобождение при отвязке устройства).
- **slab / `kmem_cache`** — пул объектов одного типа (как арена, C3/C6).
- **kmemleak / KFENCE / SLUB_DEBUG** — детекторы утечек/порчи памяти ядра.
- **dynamic debug / `trace_printk` / ftrace** — точечное журналирование/трассировка.
- **BKL** — Big Kernel Lock (исторический глобальный лок; удалён).
- **DKMS** — Dynamic Kernel Module Support: пересборка out-of-tree модуля при
  обновлении ядра.
- **MOK** — Machine Owner Key: ключ для подписи модулей при Secure Boot.
- **checkpatch.pl** — проверка стиля кода ядра.
- **KABI/ABI ядра** — нестабильный внутренний интерфейс (драйверы правят с ядром).
- **IRQ** — Interrupt Request: сигнал прерывания от устройства (обработчик — K3).
- **uevent / hotplug** — события ядра о появлении/удалении устройства (→ devtmpfs/udev
  создаёт узлы).
- **`MODULE_DEVICE_TABLE`** — таблица поддерживаемых устройств для авто-загрузки по
  hotplug (PCI/USB).
- **SMP** — Symmetric MultiProcessing: код ядра исполняется параллельно на ядрах.
- **Preemption** — вытеснение: код ядра может быть прерван планировщиком (вне
  атомарных секций).
- **`smp_mb`/`READ_ONCE`/`smp_store_release`** — барьеры/упорядоченный доступ ядра
  (модель памяти C1 ядерными примитивами).
- **`access_ok`** — проверка, что адрес в userspace-диапазоне (внутри `copy_*_user`).
- **Ring buffer (FIFO)** — кольцевой буфер потоковых данных (пайп, tty); голова/хвост.
- **`noop_llseek`** — дефолт `.llseek` для устройства-потока (seek — no-op).
- **GNU C** — диалект C с расширениями (`typeof`, statement expr), на котором написано
  ядро.
- **`-ENOTTY`/`-EFAULT`/`-EAGAIN`/`-ENODEV`** — типовые коды возврата драйвера
  (неподдерживаемый ioctl / плохой userspace-адрес / нет данных в неблокирующем /
  нет устройства).

---

## 21. Что дальше

K1 дал модель: как ядро вызывает драйвер через VFS, как пересекать границу
userspace/kernel, регистрировать устройство и защищать состояние. Дальше —
вглубь стороны ядра:

- **K2 (синхронизация в ядре):** спинлоки/мьютексы/RCU/per-CPU **изнутри** — то, что
  в C1 было userspace-атомиками, здесь обретает ядерные примитивы и контексты (нельзя
  спать, прерывания, preemption). Lockdep, memory barriers ядра.
- **K3 (отложенная работа и прерывания):** обработчики прерываний (верхняя/нижняя
  половина), workqueue/tasklet/softirq, и **wait queue** из §11/§12 в полный рост —
  как устройство **реально** сигналит готовность (мостик в C2/C4 со стороны железа).
- **K4–K7:** аллокаторы ядра, VFS изнутри, netfilter, трассировка (ftrace/eBPF, K7 —
  обратная сторона perf из C6).

### Мини-проект для закрепления

Доведи `01-char-dev` до **полнофункционального** драйвера: (1) поддержка **многих**
устройств (minor 0..N) через `struct my_device` + `container_of` (§9); (2) `ioctl`
для сброса/запроса длины с безопасной проверкой команд (§10); (3) **блокирующий**
`read` через wait queue + неблокирующий по `O_NONBLOCK` (§11); (4) `.poll` для
совместимости с `epoll` — напиши userspace-клиента на `epoll` (C2!), который ждёт
данные от драйвера (§12); (5) защита состояния мьютексом, проверенная под нагрузкой
(несколько процессов) с **lockdep** (§14). Прогоняй в QEMU, читай `dmesg`. Это
микромодель реального char-драйвера и фундамент для K2/K3.

> **Критерий готовности модуля:** ты написал символьный драйвер с `read`/`write`
> через `copy_*_user` и корректным `*ppos`, зарегистрировал устройство (cdev + class
> → `/dev/...`), защитил состояние от конкурентного доступа, и понимаешь, как сделать
> его совместимым с `poll`/epoll (`poll_wait` + wake_up). Можешь объяснить, почему
> userspace-указатель нельзя трогать напрямую, чем mutex отличается от спинлока в
> драйвере, и где искать, где упал твой код в oops. Тогда — вперёд в K2.
