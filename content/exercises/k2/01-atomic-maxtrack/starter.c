// K2 — атомарный счётчик с отслеживанием максимума (lock-free, без спинлока).
// Устройство /dev/cppmax:
//   write "<N>"  → атомарно прибавить N к счётчику (N может быть отрицательным),
//                  и обновить «исторический максимум» через CAS-цикл (atomic_cmpxchg).
//   read         → строка "counter=<v> max=<m>\n".
//
// Это ядровый аналог lock-free max из C1: счётчик растёт atomic_add_return, а
// максимум обновляется без блокировки — циклом compare-and-swap. На SMP это
// корректно без спинлока; плоский int здесь дал бы потерю обновлений.
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace (echo/cat).

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/kstrtox.h>

static atomic_t counter = ATOMIC_INIT(0);
static atomic_t high    = ATOMIC_INIT(0);

// Обновить high до max(high, v) БЕЗ блокировки — циклом atomic_cmpxchg.
static void bump_high(int v)
{
    // TODO:
    //   int old = atomic_read(&high);
    //   while (v > old) {
    //       int prev = atomic_cmpxchg(&high, old, v);  // если high==old → стало v
    //       if (prev == old) break;                     // успех
    //       old = prev;                                  // кто-то опередил — перечитали
    //   }
    (void)v;
}

static ssize_t max_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
    char kbuf[32];
    long n;

    if (count == 0 || count >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, ubuf, count))
        return -EFAULT;
    kbuf[count] = '\0';
    if (kstrtol(strim(kbuf), 10, &n))
        return -EINVAL;

    // TODO: v = atomic_add_return((int)n, &counter); bump_high(v);
    (void)n;
    return count;
}

static ssize_t max_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    char tmp[64];
    int len;

    // TODO: len = scnprintf(tmp, sizeof tmp, "counter=%d max=%d\n",
    //                       atomic_read(&counter), atomic_read(&high));
    len = scnprintf(tmp, sizeof tmp, "counter=0 max=0\n");   // ← замени на реальные значения
    return simple_read_from_buffer(ubuf, count, ppos, tmp, len);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = max_read,
    .write = max_write,
};

static struct miscdevice cppmax = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppmax",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init max_init(void)
{
    return misc_register(&cppmax);
}

static void __exit max_exit(void)
{
    misc_deregister(&cppmax);
}

module_init(max_init);
module_exit(max_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K2: atomic counter with lock-free max tracking");
