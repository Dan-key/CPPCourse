// K4 — пул именованных объектов в СВОЁМ slab-кэше (kmem_cache).
// /dev/cppslab:
//   write "<name>" → kmem_cache_alloc объект, сохранить имя, добавить в список
//   read           → имена через пробел: "alpha beta\n"
//   exit           → освободить ВСЕ объекты, потом kmem_cache_destroy
//
//   собирается как cppmod.ko; грузится insmod; тест из userspace (echo/cat) в QEMU.

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>

struct entry { struct list_head node; char name[32]; };

static struct kmem_cache *entry_cache;
static LIST_HEAD(entries);
static DEFINE_MUTEX(lock);

static ssize_t slab_write(struct file *f, const char __user *u, size_t count, loff_t *ppos)
{
    char kbuf[32];
    size_t n = min(count, sizeof(kbuf) - 1);

    if (copy_from_user(kbuf, u, n))
        return -EFAULT;
    kbuf[n] = '\0';
    if (n && kbuf[n - 1] == '\n') kbuf[n - 1] = '\0';

    // TODO: выделить объект из СВОЕГО кэша и добавить в список:
    //   struct entry *e = kmem_cache_alloc(entry_cache, GFP_KERNEL);
    //   if (!e) return -ENOMEM;
    //   strscpy(e->name, kbuf, sizeof e->name);
    //   mutex_lock(&lock); list_add_tail(&e->node, &entries); mutex_unlock(&lock);
    return count;
}

static ssize_t slab_read(struct file *f, char __user *u, size_t count, loff_t *ppos)
{
    char tmp[512];
    int len = 0;
    // struct entry *e;

    // TODO: перечислить имена объектов через пробел:
    //   mutex_lock(&lock);
    //   list_for_each_entry(e, &entries, node)
    //       len += scnprintf(tmp + len, sizeof(tmp) - len, "%s ", e->name);
    //   mutex_unlock(&lock);
    if (len > 0) tmp[len - 1] = '\n'; else { tmp[0] = '\n'; len = 1; }
    return simple_read_from_buffer(u, count, ppos, tmp, len);
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read  = slab_read,
    .write = slab_write,
};

static struct miscdevice cppslab = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "cppslab",
    .fops  = &fops,
    .mode  = 0666,
};

static int __init slab_init(void)
{
    // TODO: entry_cache = kmem_cache_create("cpp_entry", sizeof(struct entry),
    //                                       0, SLAB_HWCACHE_ALIGN, NULL);
    //       if (!entry_cache) return -ENOMEM;
    return misc_register(&cppslab);
}

static void __exit slab_exit(void)
{
    struct entry *e, *tmp;
    misc_deregister(&cppslab);
    // TODO: освободить ВСЕ объекты обратно в кэш, ПОТОМ уничтожить кэш:
    //   list_for_each_entry_safe(e, tmp, &entries, node) {
    //       list_del(&e->node);
    //       kmem_cache_free(entry_cache, e);
    //   }
    //   kmem_cache_destroy(entry_cache);
    (void)e; (void)tmp;
}

module_init(slab_init);
module_exit(slab_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K4: per-driver slab cache");
