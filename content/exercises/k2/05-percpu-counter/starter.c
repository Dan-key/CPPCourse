// K2 — per-CPU счётчик: масштабируемая статистика БЕЗ общей строки кэша.
// Устройство /dev/cpppercpu:
//   write "<N>"  → прибавить N к счётчику ТЕКУЩЕГО ядра (this_cpu_add)
//   read         → сумма по ВСЕМ ядрам (агрегация на чтении)
//
// Идея (C1 §10): вместо одного общего счётчика, который пинг-понгует между
// ядрами (false sharing + сериализация на шине когерентности), у КАЖДОГО ядра
// свой счётчик в своей памяти. Инкремент локален и почти бесплатен; общая цена
// платится только на чтении (редком) — суммированием. Так ядро считает сетевую
// статистику, vmstat и т.п.
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/kstrtox.h>

static DEFINE_PER_CPU(long, mycount);             // по экземпляру на каждое ядро

static ssize_t pc_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
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

    // TODO: прибавить n к счётчику ТЕКУЩЕГО ядра (preempt-safe):
    //   this_cpu_add(mycount, n);
    (void)n;
    return count;
}

static ssize_t pc_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    char tmp[32];
    long total = 0;
    int len;
    // int cpu;

    // TODO: просуммировать по всем возможным ядрам:
    //   for_each_possible_cpu(cpu)
    //       total += per_cpu(mycount, cpu);
    total = 0;   // ← замени на агрегацию

    len = scnprintf(tmp, sizeof tmp, "%ld\n", total);
    return simple_read_from_buffer(ubuf, count, ppos, tmp, len);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = pc_read,
    .write = pc_write,
};

static struct miscdevice cpppercpu = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cpppercpu",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init pc_init(void)  { return misc_register(&cpppercpu); }
static void __exit pc_exit(void) { misc_deregister(&cpppercpu); }

module_init(pc_init);
module_exit(pc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K2: per-CPU counter");
