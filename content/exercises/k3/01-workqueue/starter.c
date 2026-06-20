// K3 — отложить обработку из «события» (write = top half) в WORKQUEUE (bottom half,
// process context). work_fn переводит данные в верхний регистр; read отдаёт результат.
//
//   собирается как cppmod.ko, грузится insmod, тест из userspace (echo/cat) в QEMU.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#define BUF_SIZE 1024

static struct work_struct work;
static char    in_buf[BUF_SIZE];   static size_t in_len;
static char    res_buf[BUF_SIZE];  static size_t res_len;
static DEFINE_MUTEX(lock);

// Выполняется в PROCESS CONTEXT (kworker) — можно спать/mutex/copy_*_user.
static void work_fn(struct work_struct *w)
{
    // TODO: под lock — перевести in_buf[0..in_len) в ВЕРХНИЙ регистр в res_buf;
    //       res_len = in_len.
}

static ssize_t wq_write(struct file *f, const char __user *u, size_t count, loff_t *ppos)
{
    // TODO: count = min(count, BUF_SIZE); под lock copy_from_user(in_buf, u, count);
    //       in_len = count; schedule_work(&work);  // ОТЛОЖИТЬ обработку; return count.
    return -EINVAL;
}

static ssize_t wq_read(struct file *f, char __user *u, size_t count, loff_t *ppos)
{
    // TODO: flush_work(&work);   // дождаться завершения отложенной работы
    //       отдать res_buf с учётом *ppos (EOF при *ppos>=res_len) через copy_to_user.
    return -EINVAL;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = wq_read,
    .write = wq_write,
};

static struct miscdevice cppwq = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppwq",
    .fops  = &fops,
};

static int __init wq_init(void)
{
    // TODO: INIT_WORK(&work, work_fn); return misc_register(&cppwq);
    return 0;
}

static void __exit wq_exit(void)
{
    // TODO: misc_deregister(&cppwq); cancel_work_sync(&work);
    //       (сначала снять устройство, потом ДОБИТЬ запланированную работу — иначе
    //        kworker дёрнет work_fn после выгрузки → паника.)
}

module_init(wq_init);
module_exit(wq_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K3: IRQ bottom-half via workqueue");
