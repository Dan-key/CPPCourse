// K4 — одна страница напрямую у BUDDY-аллокатора.
// /dev/cpppage:
//   init  → get_zeroed_page (одна обнулённая страница, PAGE_SIZE байт)
//   write → записать в страницу до PAGE_SIZE байт
//   read  → отдать записанное
//   exit  → free_pages (тот же order!)
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace в QEMU.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/mutex.h>

#define ORDER 0                 // одна страница (2^0)

static unsigned long page_addr;
static size_t        data_len;
static DEFINE_MUTEX(lock);

static ssize_t page_write(struct file *f, const char __user *u, size_t count, loff_t *ppos)
{
    size_t n = min(count, (size_t)PAGE_SIZE);
    if (!page_addr)
        return -ENOMEM;
    mutex_lock(&lock);
    if (copy_from_user((void *)page_addr, u, n)) { mutex_unlock(&lock); return -EFAULT; }
    data_len = n;
    mutex_unlock(&lock);
    return count;
}

static ssize_t page_read(struct file *f, char __user *u, size_t count, loff_t *ppos)
{
    ssize_t ret;
    mutex_lock(&lock);
    ret = simple_read_from_buffer(u, count, ppos, (void *)page_addr, data_len);
    mutex_unlock(&lock);
    return ret;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = page_read,
    .write = page_write,
};

static struct miscdevice cpppage = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cpppage",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init page_init(void)
{
    // TODO: page_addr = get_zeroed_page(GFP_KERNEL);
    //       if (!page_addr) return -ENOMEM;
    return misc_register(&cpppage);
}

static void __exit page_exit(void)
{
    misc_deregister(&cpppage);
    // TODO: free_pages(page_addr, ORDER);   // ОРДЕР тот же, что при выделении!
}

module_init(page_init);
module_exit(page_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K4: raw page from buddy");
