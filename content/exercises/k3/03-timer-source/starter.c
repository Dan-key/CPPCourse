// K3 — таймер ядра как АТОМАРНЫЙ источник событий → workqueue (process context).
// /dev/cpptimer:
//   timer_fn (softirq, атомарно)  → atomic_inc(ticks); schedule_work; перевзвести таймер
//   work_fn  (process context)    → processed = atomic_read(ticks)  (тут могла бы быть
//                                    тяжёлая/сонная обработка)
//   read                          → "ticks=<a> processed=<b>"
//   exit                          → timer_delete_sync → cancel_work_sync (порядок!)
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace в QEMU.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>

static struct timer_list mytimer;
static struct work_struct work;
static atomic_t ticks = ATOMIC_INIT(0);
static int      processed;
static DEFINE_MUTEX(lock);

// PROCESS CONTEXT (kworker): можно спать/mutex. «Тяжёлая» половина.
static void work_fn(struct work_struct *w)
{
    // TODO: под lock — processed = atomic_read(&ticks);
}

// АТОМАРНЫЙ контекст (softirq) — СПАТЬ НЕЛЬЗЯ. Только посчитать, отложить, перевзвести.
static void timer_fn(struct timer_list *t)
{
    // TODO:
    //   atomic_inc(&ticks);
    //   schedule_work(&work);                                 // отложить в process context
    //   mod_timer(&mytimer, jiffies + msecs_to_jiffies(50));  // тикать снова
}

static ssize_t timer_read(struct file *f, char __user *u, size_t count, loff_t *ppos)
{
    char tmp[64];
    int len, p;

    mutex_lock(&lock);
    p = processed;
    mutex_unlock(&lock);

    len = scnprintf(tmp, sizeof tmp, "ticks=%d processed=%d\n", atomic_read(&ticks), p);
    return simple_read_from_buffer(u, count, ppos, tmp, len);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = timer_read,
};

static struct miscdevice cpptimer = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cpptimer",
    .fops  = &fops,
    .mode  = 0444,
};

static int __init timer_init(void)
{
    // TODO:
    //   timer_setup(&mytimer, timer_fn, 0);
    //   INIT_WORK(&work, work_fn);
    //   mod_timer(&mytimer, jiffies + msecs_to_jiffies(50));   // запустить тиканье
    //   return misc_register(&cpptimer);
    return 0;
}

static void __exit timer_exit(void)
{
    // TODO: остановить ИСТОЧНИК, потом добить отложенную работу, потом снять устройство:
    //   timer_delete_sync(&mytimer);     // 1) таймер больше не тикнет и не запланирует work
    //   cancel_work_sync(&work);         // 2) добить то, что он успел запланировать
    //   misc_deregister(&cpptimer);      // 3)
}

module_init(timer_init);
module_exit(timer_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K3: kernel timer as atomic source -> workqueue");
