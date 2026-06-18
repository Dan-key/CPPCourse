// K1 — misc-устройство /dev/cppcount: считает открытия (open) атомарным счётчиком,
// read возвращает номер ТЕКУЩЕГО открытия (из file->private_data).
//
//   собирается как cppmod.ko, грузится insmod, тест из userspace (cat).

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>

static atomic_t open_count = ATOMIC_INIT(0);

static int cnt_open(struct inode *inode, struct file *f)
{
    // TODO: n = atomic_inc_return(&open_count); f->private_data = (void *)n; return 0;
    return 0;
}

static ssize_t cnt_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    // TODO: n = (long)f->private_data; отформатировать в текст (scnprintf);
    //       вернуть через simple_read_from_buffer(ubuf, count, ppos, tmp, len).
    return -EINVAL;
}

static int cnt_release(struct inode *inode, struct file *f)
{
    return 0;
}

static const struct file_operations fops = {
    .owner   = THIS_MODULE,        // важно: держит модуль, пока устройство открыто
    .open    = cnt_open,
    .read    = cnt_read,
    .release = cnt_release,
};

static struct miscdevice cppcount = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppcount",
    .fops  = &fops,
};

static int __init cnt_init(void)
{
    // TODO: return misc_register(&cppcount);
    return 0;
}

static void __exit cnt_exit(void)
{
    // TODO: misc_deregister(&cppcount);
}

module_init(cnt_init);
module_exit(cnt_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K1: open-counter device");
