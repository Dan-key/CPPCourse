// K1 — read-only misc-устройство /dev/cppinfo: read отдаёт INFO, write → -EACCES.
//
//   собирается как cppmod.ko, грузится insmod, тест из userspace (cat / echo).

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

static const char INFO[] = "CPPK1-INFO-OK";

static ssize_t info_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    // TODO: вернуть INFO (sizeof(INFO)-1 байт) с учётом *ppos.
    //       Проще всего: simple_read_from_buffer(ubuf, count, ppos, INFO, sizeof(INFO)-1);
    return -EINVAL;
}

static ssize_t info_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
    // TODO: устройство read-only — вернуть -EACCES.
    return -EINVAL;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = info_read,
    .write = info_write,
};

static struct miscdevice cppinfo = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppinfo",
    .fops  = &fops,
};

static int __init info_init(void)
{
    // TODO: return misc_register(&cppinfo);
    return 0;
}

static void __exit info_exit(void)
{
    // TODO: misc_deregister(&cppinfo);
}

module_init(info_init);
module_exit(info_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K1: read-only info device");
