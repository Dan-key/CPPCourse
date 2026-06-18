# Задание: символьный драйвер с read/write (и каркасом ioctl)

Это твой **первый драйвер ядра**: символьное устройство `/dev/cppchar` с буфером в
ядре. Userspace пишет в него (`echo ... > /dev/cppchar`) и читает (`cat
/dev/cppchar`) — а ты реализуешь обработчики `read`/`write` через `file_operations`,
правильно копируя данные между ядром и userspace (`copy_to_user`/`copy_from_user`) и
защищая буфер от конкурентного доступа мьютексом.

Модуль собирается **out-of-tree** (Kbuild), грузится `insmod`, тестируется из
userspace. Это ровно «руки» K1.

## Что реализовать

Out-of-tree модуль (один `.c`), который при загрузке:
1. **регистрирует** символьное устройство (выделяет `dev_t`, добавляет `cdev`);
2. **создаёт узел** `/dev/cppchar` (через `class_create` + `device_create` →
   devtmpfs сам сделает узел);
3. обслуживает `read`/`write` через буфер в ядре.

Семантика:
- **`write`** — сохранить в буфер ядра до `BUF_SIZE` байт (перезаписывая прежнее),
  запомнить длину; вернуть число принятых байт.
- **`read`** — отдать сохранённые данные, **корректно обрабатывая `*ppos`** (вернуть
  `0` на EOF, чтобы `cat` завершился); вернуть число отданных байт.
- **mutex** вокруг буфера — несколько процессов могут обращаться одновременно.

## Скелет (заполни TODO)

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>   // copy_to_user / copy_from_user
#include <linux/mutex.h>
#include <linux/slab.h>      // kzalloc / kfree

#define DEV_NAME "cppchar"
#define BUF_SIZE 1024

static dev_t          dev_num;
static struct cdev    my_cdev;
static struct class  *my_class;
static char          *buffer;
static size_t         data_len;
static DEFINE_MUTEX(buf_lock);

static ssize_t my_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos){
    // TODO: под мьютексом — EOF при *ppos>=data_len; n=min(count, data_len-*ppos);
    //       copy_to_user(ubuf, buffer+*ppos, n); *ppos+=n; вернуть n
}
static ssize_t my_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos){
    // TODO: n=min(count, BUF_SIZE); copy_from_user(buffer, ubuf, n);
    //       data_len=n; вернуть n  (под мьютексом)
}
static const struct file_operations fops = {
    .owner = THIS_MODULE, .read = my_read, .write = my_write,
};

static int __init my_init(void){
    // TODO: kzalloc(buffer); alloc_chrdev_region(&dev_num,0,1,DEV_NAME);
    //       cdev_init(&my_cdev,&fops); my_cdev.owner=THIS_MODULE; cdev_add(&my_cdev,dev_num,1);
    //       my_class=class_create(DEV_NAME); device_create(my_class,NULL,dev_num,NULL,DEV_NAME);
    //       аккуратная обработка ошибок (откат в обратном порядке)
    return 0;
}
static void __exit my_exit(void){
    // TODO: device_destroy; class_destroy; cdev_del; unregister_chrdev_region; kfree
}
module_init(my_init);
module_exit(my_exit);
MODULE_LICENSE("GPL");
```

## Ключевые правила ядра

- **`__user` и `copy_*_user`.** Указатель из userspace (`char __user *`) **нельзя**
  разыменовывать напрямую — он из чужого адресного пространства и может быть
  невалиден. Только `copy_to_user`/`copy_from_user` (они проверяют доступ и
  возвращают **число НЕскопированных** байт; `!=0` → `-EFAULT`).
- **`*ppos`.** Ядро передаёт позицию через `*ppos`. `read` обязан её двигать и
  возвращать `0` на EOF — иначе `cat` зациклится. `write` тоже её получает (мы
  перезаписываем с нуля, поэтому позицию для записи игнорируем).
- **Откат при ошибке** — как goto-cleanup из Ф1, но в `__init`: если `cdev_add`
  упал, освободи `dev_t`; если `class_create` упал — удали `cdev`, и т.д. (обратный
  порядок). `class_create`/`device_create` могут вернуть `ERR_PTR` — проверяй
  `IS_ERR`.
- **Конкурентность.** Несколько процессов могут читать/писать одновременно —
  буфер и `data_len` защищай `mutex_lock`/`mutex_unlock` (read/write — sleepable
  контекст, мьютекс можно).
- **Версия ядра.** `class_create(NAME)` — **одноаргументная** с ядра 6.4+ (как
  здесь). На старых — `class_create(THIS_MODULE, NAME)`.

## Сборка и тест (что делает автопрогон)

Модуль собирается против хост-ядра (`make -C /lib/modules/$(uname -r)/build M=…`),
грузится в **QEMU**, и тестируется из userspace (busybox):

```sh
insmod /mnt/share/cppmod.ko               # модуль грузится как cppmod
[ -c /dev/cppchar ]                       # узел создан?
echo -n "hello-kernel" > /dev/cppchar     # write
cat /dev/cppchar                          # read → "hello-kernel"
echo -n "second" > /dev/cppchar           # перезапись
cat /dev/cppchar                          # → "second"
```

## Отладка

- **`dmesg`** — твои `pr_info`/`pr_err` видны здесь (драйвер не пишет в stdout).
  Добавляй `pr_info("cppchar: ...\n")` в init/read/write при отладке.
- **`lsmod`** — загружен ли модуль; **`rmmod module`** — выгрузить.
- Падение в ядре = **oops** в `dmesg` (а не segfault процесса) — читай стек оттуда.

## Связь с курсом

`copy_*_user` и `__user` — обратная сторона границы userspace/kernel из Ф2.
`file_operations` — то, что вызывает VFS, когда userspace делает `read`/`write` на
твоём fd (C2: «всё есть fd» — здесь ты пишешь **другую** сторону). Мьютекс в драйвере
— синхронизация из C1, но в ядре (K2 — глубже). `poll`-совместимость (`poll_wait`,
wait queues) — отдельная большая тема (см. лекцию §о блокирующем I/O и §о poll),
мостик в C2/C4 (epoll со стороны драйвера).
