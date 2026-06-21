// K3 — отложить обработку НА ВРЕМЯ через delayed_work (workqueue + таймер).
// /dev/cppdw:
//   write "<text>" → запланировать обработку через 100 мс (schedule_delayed_work)
//   dwork_fn       → перевести вход в верхний регистр, посчитать запуски (process context)
//   read           → "<RESULT> runs=<n>" (flush_delayed_work — дождаться)
//   exit           → cancel_delayed_work_sync (синхронизация с остановкой)
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace (echo/cat) в QEMU.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/ctype.h>

#define BUF_SIZE 1024

static struct delayed_work dwork;
static char   in_buf[BUF_SIZE];   static size_t in_len;
static char   res_buf[BUF_SIZE];  static size_t res_len;
static int    runs;
static DEFINE_MUTEX(lock);

// Выполняется в PROCESS CONTEXT (kworker) ЧЕРЕЗ ЗАДЕРЖКУ — можно спать/mutex.
static void dwork_fn(struct work_struct *w)
{
    // TODO: под lock — перевести in_buf[0..in_len) в ВЕРХНИЙ регистр в res_buf;
    //       res_len = in_len; runs++.
    //   mutex_lock(&lock);
    //   for (i = 0; i < in_len; i++) res_buf[i] = toupper(in_buf[i]);
    //   res_len = in_len; runs++;
    //   mutex_unlock(&lock);
}

static ssize_t dw_write(struct file *f, const char __user *u, size_t count, loff_t *ppos)
{
    if (count > BUF_SIZE) count = BUF_SIZE;
    mutex_lock(&lock);
    if (copy_from_user(in_buf, u, count)) { mutex_unlock(&lock); return -EFAULT; }
    in_len = count;
    mutex_unlock(&lock);

    // TODO: запланировать обработку ЧЕРЕЗ 100 мс:
    //   schedule_delayed_work(&dwork, msecs_to_jiffies(100));
    return count;
}

static ssize_t dw_read(struct file *f, char __user *u, size_t count, loff_t *ppos)
{
    char tmp[BUF_SIZE + 32];
    int len;

    // TODO: flush_delayed_work(&dwork);   // дождаться отложенной работы

    mutex_lock(&lock);
    len = scnprintf(tmp, sizeof tmp, "%.*s runs=%d\n", (int)res_len, res_buf, runs);
    mutex_unlock(&lock);
    return simple_read_from_buffer(u, count, ppos, tmp, len);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = dw_read,
    .write = dw_write,
};

static struct miscdevice cppdw = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppdw",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init dw_init(void)
{
    // TODO: INIT_DELAYED_WORK(&dwork, dwork_fn); return misc_register(&cppdw);
    return 0;
}

static void __exit dw_exit(void)
{
    // TODO: misc_deregister(&cppdw); cancel_delayed_work_sync(&dwork);
    //       (сначала снять устройство, потом ДОБИТЬ отложенную работу.)
}

module_init(dw_init);
module_exit(dw_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K3: deferred work via delayed_work");
