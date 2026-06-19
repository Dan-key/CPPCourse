// K2 — блокирующая очередь сообщений: МЬЮТЕКС + WAIT QUEUE (ядровый condvar).
// Устройство /dev/cppq:
//   write "<msg>"  → положить сообщение в очередь, разбудить ждущего читателя
//   read           → вернуть самое старое сообщение; если очередь ПУСТА — УСНУТЬ,
//                    пока не появится (wait_event_interruptible).
//
// Главный контраст с 02 (спинлок): мьютекс МОЖЕТ спать, поэтому под ним законны
// длинные операции и сон. wait queue — это ядровый аналог condition variable
// (C2 §17): засыпаешь по предикату, писатель будит wake_up.
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>

#define QCAP   16
#define MSGMAX 96

static char q[QCAP][MSGMAX];
static int  q_head, q_tail, q_count;
static DEFINE_MUTEX(qlock);                       // защищает кольцо
static DECLARE_WAIT_QUEUE_HEAD(qwait);            // здесь спят читатели

static ssize_t q_write(struct file *f, const char __user *ubuf, size_t count, loff_t *ppos)
{
    char tmp[MSGMAX];
    size_t n = min(count, (size_t)(MSGMAX - 1));

    if (copy_from_user(tmp, ubuf, n))             // копируем ВНЕ лока
        return -EFAULT;
    tmp[n] = '\0';
    if (n > 0 && tmp[n - 1] == '\n') tmp[n - 1] = '\0';   // срезать перевод строки

    // TODO: под мьютексом положить сообщение в кольцо и разбудить читателя:
    //   mutex_lock(&qlock);
    //   if (q_count < QCAP) {
    //       strscpy(q[q_tail], tmp, MSGMAX);
    //       q_tail = (q_tail + 1) % QCAP;
    //       q_count++;
    //   }
    //   mutex_unlock(&qlock);
    //   wake_up_interruptible(&qwait);            // разбудить ждущего read
    (void)tmp;
    return count;
}

static ssize_t q_read(struct file *f, char __user *ubuf, size_t count, loff_t *ppos)
{
    char tmp[MSGMAX];
    int len;

    // TODO: уснуть, пока очередь пуста; затем под мьютексом снять самое старое:
    //   if (wait_event_interruptible(qwait, READ_ONCE(q_count) > 0))
    //       return -ERESTARTSYS;                  // прерван сигналом
    //   mutex_lock(&qlock);
    //   if (q_count == 0) { mutex_unlock(&qlock); return 0; }  // гонка: кто-то опередил
    //   strscpy(tmp, q[q_head], MSGMAX);
    //   q_head = (q_head + 1) % QCAP;
    //   q_count--;
    //   mutex_unlock(&qlock);
    strscpy(tmp, "", MSGMAX);                      // ← замени на снятое сообщение

    len = strlen(tmp);
    tmp[len++] = '\n';
    if (copy_to_user(ubuf, tmp, min((size_t)len, count)))   // копируем ВНЕ лока
        return -EFAULT;
    return min((size_t)len, count);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = q_read,
    .write = q_write,
};

static struct miscdevice cppq = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppq",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init q_init(void)  { return misc_register(&cppq); }
static void __exit q_exit(void) { misc_deregister(&cppq); }

module_init(q_init);
module_exit(q_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K2: mutex + wait_queue blocking message queue");
