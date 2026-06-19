// K2 — стек целых чисел в ядре, защищённый СПИНЛОКОМ.
// Устройство /dev/cppstack:
//   write "push <N>"  → положить N на вершину
//   write "pop"       → снять вершину (значение уходит в last_pop)
//   read              → весь стек сверху вниз, через пробел: "3 2 1\n" (или "\n" если пусто)
//
// Спинлок (spinlock_t) защищает список. Критические секции КОРОТКИЕ и НЕ СПЯТ —
// внутри спинлока нельзя вызывать то, что может уснуть (kmalloc(GFP_KERNEL),
// copy_to_user и т.п.). Поэтому: парсим/копируем ВНЕ лока, а под локом — только
// манипуляция списком.
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/kstrtox.h>

struct item { struct list_head node; long val; };

static LIST_HEAD(stack);              // голова списка (вершина — первый элемент)
static DEFINE_SPINLOCK(lock);         // защищает stack

// Положить val на вершину. Узел уже выделен ВНЕ лока (node). Вернуть 0.
static int do_push(struct item *node)
{
    // TODO: spin_lock(&lock); list_add(&node->node, &stack); spin_unlock(&lock);
    (void)node;
    return 0;
}

// Снять вершину. Если пусто — вернуть NULL. Иначе вернуть снятый узел (его освободит вызывающий ВНЕ лока).
static struct item *do_pop(void)
{
    struct item *it = NULL;
    // TODO:
    //   spin_lock(&lock);
    //   if (!list_empty(&stack)) {
    //       it = list_first_entry(&stack, struct item, node);
    //       list_del(&it->node);
    //   }
    //   spin_unlock(&lock);
    return it;   // (замени)
}

static ssize_t stack_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
    char kbuf[48];
    long n;

    if (count == 0 || count >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, ubuf, count))
        return -EFAULT;
    kbuf[count] = '\0';
    strim(kbuf);

    if (strncmp(kbuf, "push ", 5) == 0 && kstrtol(strim(kbuf + 5), 10, &n) == 0) {
        // ВЫДЕЛЯЕМ узел ВНЕ лока (kmalloc может спать):
        struct item *it = kmalloc(sizeof(*it), GFP_KERNEL);
        if (!it)
            return -ENOMEM;
        it->val = n;
        do_push(it);
        return count;
    }
    if (strncmp(kbuf, "pop", 3) == 0) {
        struct item *it = do_pop();
        kfree(it);            // освобождаем ВНЕ лока (kfree безопасен; узел уже снят)
        return count;
    }
    return -EINVAL;
}

static ssize_t stack_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    char *buf;
    struct item *it;
    int len = 0;
    ssize_t ret;

    buf = kmalloc(PAGE_SIZE, GFP_KERNEL);      // временный буфер ВНЕ лока
    if (!buf)
        return -ENOMEM;

    // Под локом только ЧИТАЕМ список в буфер (scnprintf не спит):
    spin_lock(&lock);
    list_for_each_entry(it, &stack, node)
        len += scnprintf(buf + len, PAGE_SIZE - len, "%ld ", it->val);
    spin_unlock(&lock);

    if (len > 0) buf[len - 1] = '\n'; else { buf[0] = '\n'; len = 1; }

    ret = simple_read_from_buffer(ubuf, count, ppos, buf, len);
    kfree(buf);
    return ret;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = stack_read,
    .write = stack_write,
};

static struct miscdevice cppstack = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppstack",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init stack_init(void)
{
    return misc_register(&cppstack);
}

static void __exit stack_exit(void)
{
    struct item *it, *tmp;
    misc_deregister(&cppstack);
    // освободить остаток списка (модуль выгружается — конкуренции уже нет)
    list_for_each_entry_safe(it, tmp, &stack, node) {
        list_del(&it->node);
        kfree(it);
    }
}

module_init(stack_init);
module_exit(stack_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K2: spinlock-protected stack");
