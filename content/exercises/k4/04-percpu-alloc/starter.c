// K4 — динамический per-CPU счётчик через ALLOC_PERCPU.
// /dev/cpppc:
//   write "<N>" → this_cpu_add к счётчику СВОЕГО ядра (без локов)
//   read        → сумма по ВСЕМ ядрам (агрегация на чтении)
//   exit        → free_percpu
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace в QEMU.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/kstrtox.h>

static long __percpu *counter;

static ssize_t pc_write(struct file *f, const char __user *u, size_t count, loff_t *ppos)
{
    char kbuf[32];
    long n;

    if (count == 0 || count >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, u, count))
        return -EFAULT;
    kbuf[count] = '\0';
    if (kstrtol(strim(kbuf), 10, &n))
        return -EINVAL;

    // TODO: прибавить n к счётчику ТЕКУЩЕГО ядра:
    //   this_cpu_add(*counter, n);
    (void)n;
    return count;
}

static ssize_t pc_read(struct file *f, char __user *u, size_t count, loff_t *ppos)
{
    char tmp[32];
    long total = 0;
    int len;
    // int cpu;

    // TODO: просуммировать по всем возможным ядрам:
    //   for_each_possible_cpu(cpu)
    //       total += *per_cpu_ptr(counter, cpu);

    len = scnprintf(tmp, sizeof tmp, "%ld\n", total);
    return simple_read_from_buffer(u, count, ppos, tmp, len);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = pc_read,
    .write = pc_write,
};

static struct miscdevice cpppc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cpppc",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init pc_init(void)
{
    // TODO: counter = alloc_percpu(long);
    //       if (!counter) return -ENOMEM;
    return misc_register(&cpppc);
}

static void __exit pc_exit(void)
{
    misc_deregister(&cpppc);
    // TODO: free_percpu(counter);
}

module_init(pc_init);
module_exit(pc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K4: dynamic per-CPU counter");
