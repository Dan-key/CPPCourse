// K2 — конфигурация под RCU (Read-Copy-Update) — флагманский примитив ядра.
// Устройство /dev/cppcfg:
//   read              → текущая конфигурация: "<name> <value>\n"
//   write "<name> <value>" → ОПУБЛИКОВАТЬ новую конфигурацию (атомарная замена
//                            указателя), дождаться читателей и освободить старую.
//
// RCU: читатели почти бесплатны (rcu_read_lock + rcu_dereference, без блокировки и
// без записи в общую память), писатель публикует НОВУЮ версию (rcu_assign_pointer)
// и ждёт grace period (synchronize_rcu), прежде чем освободить старую. Это та самая
// модель из C1 §12 — теперь по-настоящему, в ядре.
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/kstrtox.h>

struct cfg {
    char name[48];
    int  value;
};

static struct cfg __rcu *current_cfg;             // защищён RCU
static DEFINE_MUTEX(writer_mutex);                // сериализует ПИСАТЕЛЕЙ между собой

static ssize_t cfg_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    char name[48];
    int  value;
    char tmp[80];
    int  len;

    // TODO: прочитать текущую конфигурацию под RCU read-side (НЕ спать внутри!):
    //   struct cfg *p;
    //   rcu_read_lock();
    //   p = rcu_dereference(current_cfg);
    //   strscpy(name, p->name, sizeof name);     // скопировать в локальные (стек)
    //   value = p->value;
    //   rcu_read_unlock();
    strscpy(name, "?", sizeof name); value = -1;   // ← замени на чтение под RCU

    len = scnprintf(tmp, sizeof tmp, "%s %d\n", name, value);
    return simple_read_from_buffer(ubuf, count, ppos, tmp, len);   // copy_to_user ВНЕ RCU
}

static ssize_t cfg_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
    char kbuf[80];
    struct cfg *neu, *old;
    char *sp;
    int value;

    if (count == 0 || count >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, ubuf, count))
        return -EFAULT;
    kbuf[count] = '\0';
    strim(kbuf);

    // Разбор "<name> <value>" и выделение нового узла — ВНЕ RCU:
    sp = strchr(kbuf, ' ');
    if (!sp) return -EINVAL;
    *sp = '\0';
    if (kstrtoint(strim(sp + 1), 10, &value)) return -EINVAL;

    neu = kmalloc(sizeof(*neu), GFP_KERNEL);
    if (!neu) return -ENOMEM;
    strscpy(neu->name, kbuf, sizeof neu->name);
    neu->value = value;

    // TODO: опубликовать neu, дождаться читателей старой версии, освободить старую:
    //   mutex_lock(&writer_mutex);                       // сериализуем писателей
    //   old = rcu_dereference_protected(current_cfg, lockdep_is_held(&writer_mutex));
    //   rcu_assign_pointer(current_cfg, neu);            // атомарная публикация
    //   mutex_unlock(&writer_mutex);
    //   synchronize_rcu();                               // ждём, пока все читатели старой уйдут
    //   kfree(old);                                      // теперь старую освободить безопасно
    (void)old;
    return count;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = cfg_read,
    .write = cfg_write,
};

static struct miscdevice cppcfg = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppcfg",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init cfg_init(void)
{
    struct cfg *init = kmalloc(sizeof(*init), GFP_KERNEL);
    if (!init)
        return -ENOMEM;
    strscpy(init->name, "boot", sizeof init->name);
    init->value = 0;
    rcu_assign_pointer(current_cfg, init);
    return misc_register(&cppcfg);
}

static void __exit cfg_exit(void)
{
    struct cfg *p;
    misc_deregister(&cppcfg);
    p = rcu_dereference_protected(current_cfg, 1);
    rcu_assign_pointer(current_cfg, NULL);
    synchronize_rcu();
    kfree(p);
}

module_init(cfg_init);
module_exit(cfg_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K2: RCU-protected configuration");
