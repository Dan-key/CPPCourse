// K3 — разбудить блокирующий read из отложенной работы (полный круг §13).
// /dev/cppwake:
//   write "<text>" → сохранить, schedule_work (источник события, «top half»)
//   work_fn        → uppercase, data_ready=1, wake_up (process context, bottom half)
//   read           → wait_event_interruptible до готовности, отдать результат
//   poll           → EPOLLIN, когда готово (та же wait queue → epoll из C2)
//   exit           → cancel_work_sync
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace в QEMU.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ctype.h>

#define BUF_SIZE 1024

static struct work_struct work;
static char   in_buf[BUF_SIZE];   static size_t in_len;
static char   res_buf[BUF_SIZE];  static size_t res_len;
static bool   data_ready;
static DEFINE_MUTEX(lock);
static DECLARE_WAIT_QUEUE_HEAD(wq);

// PROCESS CONTEXT (bottom half): обработать и РАЗБУДИТЬ ждущих.
static void work_fn(struct work_struct *w)
{
    // TODO: под lock — uppercase in_buf→res_buf, res_len=in_len;
    //       WRITE_ONCE(data_ready, true); (под lock) ; затем wake_up_interruptible(&wq);
}

static ssize_t wake_write(struct file *f, const char __user *u, size_t count, loff_t *ppos)
{
    if (count > BUF_SIZE) count = BUF_SIZE;
    mutex_lock(&lock);
    if (copy_from_user(in_buf, u, count)) { mutex_unlock(&lock); return -EFAULT; }
    in_len = count;
    WRITE_ONCE(data_ready, false);
    mutex_unlock(&lock);

    // TODO: schedule_work(&work);    // отложить обработку (как top half прерывания)
    return count;
}

static ssize_t wake_read(struct file *f, char __user *u, size_t count, loff_t *ppos)
{
    char tmp[BUF_SIZE];
    size_t n;

    // TODO: уснуть до готовности:
    //   if (wait_event_interruptible(wq, READ_ONCE(data_ready)))
    //       return -ERESTARTSYS;

    mutex_lock(&lock);
    n = min(count, res_len);
    memcpy(tmp, res_buf, n);
    WRITE_ONCE(data_ready, false);     // одноразово: следующий read снова уснёт
    mutex_unlock(&lock);

    if (copy_to_user(u, tmp, n))       // copy_to_user ВНЕ лока
        return -EFAULT;
    return n;
}

static __poll_t wake_poll(struct file *f, struct poll_table_struct *pt)
{
    // TODO: poll_wait(f, &wq, pt);
    //       return READ_ONCE(data_ready) ? (EPOLLIN | EPOLLRDNORM) : 0;
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = wake_read,
    .write = wake_write,
    .poll  = wake_poll,
};

static struct miscdevice cppwake = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppwake",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init wake_init(void)
{
    // TODO: INIT_WORK(&work, work_fn); return misc_register(&cppwake);
    return 0;
}

static void __exit wake_exit(void)
{
    // TODO: misc_deregister(&cppwake); cancel_work_sync(&work);
}

module_init(wake_init);
module_exit(wake_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K3: wake blocking read from deferred work");
