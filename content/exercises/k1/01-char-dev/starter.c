// K1 — символьный драйвер /dev/cppchar с буфером в ядре.
//
// Реализуй регистрацию устройства и обработчики read/write через file_operations,
// копируя данные через copy_to_user/copy_from_user и защищая буфер мьютексом.
//
// Сборка: out-of-tree (Kbuild), грузится insmod, тест из userspace (echo/cat).
// Узел /dev/cppchar создаётся через class_create + device_create (devtmpfs).

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define DEV_NAME "cppchar"
#define BUF_SIZE 1024

static dev_t          dev_num;
static struct cdev    my_cdev;
static struct class  *my_class;
static char          *buffer;
static size_t         data_len;
static DEFINE_MUTEX(buf_lock);

static ssize_t my_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    // TODO: под buf_lock — EOF при *ppos>=data_len (вернуть 0);
    //       n = min(count, data_len - *ppos); copy_to_user(ubuf, buffer+*ppos, n);
    //       *ppos += n; вернуть n (или -EFAULT при ошибке копирования).
    return -EINVAL;
}

static ssize_t my_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
    // TODO: n = min(count, BUF_SIZE); под buf_lock — copy_from_user(buffer, ubuf, n);
    //       data_len = n; вернуть n (или -EFAULT).
    return -EINVAL;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = my_read,
    .write = my_write,
};

static int __init my_init(void)
{
    // TODO: kzalloc(buffer, BUF_SIZE);
    //       alloc_chrdev_region(&dev_num, 0, 1, DEV_NAME);
    //       cdev_init(&my_cdev, &fops); my_cdev.owner = THIS_MODULE;
    //       cdev_add(&my_cdev, dev_num, 1);
    //       my_class = class_create(DEV_NAME);   // одноаргументная (ядро 6.4+)
    //       device_create(my_class, NULL, dev_num, NULL, DEV_NAME);
    //       аккуратный откат при ошибках (обратный порядок), IS_ERR для class/device.
    return 0;
}

static void __exit my_exit(void)
{
    // TODO: device_destroy(my_class, dev_num); class_destroy(my_class);
    //       cdev_del(&my_cdev); unregister_chrdev_region(dev_num, 1); kfree(buffer);
}

module_init(my_init);
module_exit(my_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K1: char device");
