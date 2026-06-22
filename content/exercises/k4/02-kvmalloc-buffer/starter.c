// K4 — буфер, размер которого задаёт userspace, через KVMALLOC.
// /dev/cppkv:
//   write "<data>" → kvmalloc(размер = длине записи); маленький → kmalloc, большой → vmalloc
//   read           → текущее содержимое буфера
//   exit           → kvfree (универсально)
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace в QEMU.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/mutex.h>

static void   *buf;
static size_t  buf_len;
static DEFINE_MUTEX(lock);

static ssize_t kv_write(struct file *f, const char __user *u, size_t count, loff_t *ppos)
{
    // TODO:
    //   void *nb = kvmalloc(count, GFP_KERNEL);
    //   if (!nb) return -ENOMEM;
    //   if (copy_from_user(nb, u, count)) { kvfree(nb); return -EFAULT; }
    //   mutex_lock(&lock);
    //   kvfree(buf);                 // освободить прежний (kvfree поймёт тип сам)
    //   buf = nb; buf_len = count;
    //   mutex_unlock(&lock);
    return count;
}

static ssize_t kv_read(struct file *f, char __user *u, size_t count, loff_t *ppos)
{
    ssize_t ret;
    mutex_lock(&lock);
    ret = simple_read_from_buffer(u, count, ppos, buf, buf_len);
    mutex_unlock(&lock);
    return ret;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = kv_read,
    .write = kv_write,
};

static struct miscdevice cppkv = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppkv",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init kv_init(void) { return misc_register(&cppkv); }

static void __exit kv_exit(void)
{
    misc_deregister(&cppkv);
    // TODO: kvfree(buf);
}

module_init(kv_init);
module_exit(kv_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K4: kvmalloc buffer");
