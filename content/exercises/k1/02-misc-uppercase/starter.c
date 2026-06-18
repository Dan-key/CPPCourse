// K1 — misc-устройство /dev/cppupper: хранит текст, при чтении переводит в ВЕРХНИЙ
// регистр. Регистрация через miscdevice (проще, чем cdev+class).
//
//   собирается как cppmod.ko, грузится insmod, тест из userspace (echo/cat).

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define BUF_SIZE 1024

static char    buffer[BUF_SIZE];
static size_t  data_len;
static DEFINE_MUTEX(buf_lock);

static ssize_t upper_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    // TODO: EOF при *ppos>=data_len; n=min(count, data_len-*ppos);
    //       скопировать buffer[*ppos..] в локальный tmp, переведя 'a'..'z' в 'A'..'Z';
    //       (под мьютексом — заполнить tmp; ПОСЛЕ unlock — copy_to_user); *ppos+=n; вернуть n.
    return -EINVAL;
}

static ssize_t upper_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
    // TODO: n=min(count, BUF_SIZE); под мьютексом copy_from_user(buffer, ubuf, n);
    //       data_len=n; вернуть n.
    return -EINVAL;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = upper_read,
    .write = upper_write,
};

static struct miscdevice cppupper = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppupper",
    .fops  = &fops,
};

static int __init upper_init(void)
{
    // TODO: return misc_register(&cppupper);
    return 0;
}

static void __exit upper_exit(void)
{
    // TODO: misc_deregister(&cppupper);
}

module_init(upper_init);
module_exit(upper_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K1: misc uppercase device");
