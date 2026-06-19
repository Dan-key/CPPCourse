// K2 — seqlock: согласованный снимок пары значений «много читателей, один писатель».
// Устройство /dev/cppseq:
//   write "<N>"  → атомарно (для читателей) установить пару a = b = N
//   read         → согласованный снимок "a b\n"; читатель НИКОГДА не видит a != b
//
// seqlock — НАТИВНЫЙ примитив ядра (seqlock_t), которым защищены, например,
// jiffies и монотонные часы. Это прямой аналог упражнения 03-seqlock из C1:
// читатели не блокируют писателя, а ПЕРЕЧИТЫВАЮТ данные, если во время чтения
// была запись (нечётный счётчик последовательности = идёт запись).
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/seqlock.h>
#include <linux/kstrtox.h>

static DEFINE_SEQLOCK(sl);
static int val_a;
static int val_b;            // инвариант: a == b всегда (для согласованного читателя)

static ssize_t seq_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
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

    // TODO: под write_seqlock установить ОБА значения (счётчик станет чётным после):
    //   write_seqlock(&sl);
    //   val_a = (int)n;
    //   val_b = (int)n;
    //   write_sequnlock(&sl);
    (void)n;
    return count;
}

static ssize_t seq_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    char tmp[32];
    int a = 0, b = 0;
    int len;
    // unsigned seq;

    // TODO: прочитать согласованный снимок, перечитывая при гонке с писателем:
    //   do {
    //       seq = read_seqbegin(&sl);
    //       a = val_a;
    //       b = val_b;
    //   } while (read_seqretry(&sl, seq));    // была запись во время чтения → повтор

    len = scnprintf(tmp, sizeof tmp, "%d %d\n", a, b);
    return simple_read_from_buffer(ubuf, count, ppos, tmp, len);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = seq_read,
    .write = seq_write,
};

static struct miscdevice cppseq = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppseq",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init seq_init(void)  { return misc_register(&cppseq); }
static void __exit seq_exit(void) { misc_deregister(&cppseq); }

module_init(seq_init);
module_exit(seq_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K2: seqlock consistent-snapshot pair");
