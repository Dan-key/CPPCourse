// K5 02-procfs — отдать коллекцию через seq_file (без потерь на коротких read).
// /proc/k5_list:
//   write "<строка>" → kmalloc узел, скопировать строку, добавить в хвост списка
//   read            → перечислить строки по одной на строку (seq_printf)
// Список защищён мьютексом: лок берётся в seq_start, отпускается в seq_stop.
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/string.h>

struct k5_node { struct list_head list; char msg[64]; };

static LIST_HEAD(k5_list);
static DEFINE_MUTEX(k5_lock);

static void *k5_seq_start(struct seq_file *s, loff_t *pos)
{
	mutex_lock(&k5_lock);
	return seq_list_start(&k5_list, *pos);
}
static void *k5_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	return seq_list_next(v, &k5_list, pos);
}
static void k5_seq_stop(struct seq_file *s, void *v)
{
	mutex_unlock(&k5_lock);
}
static int k5_seq_show(struct seq_file *s, void *v)
{
	// TODO: получить узел и напечатать его строку:
	//   struct k5_node *n = list_entry(v, struct k5_node, list);
	//   seq_printf(s, "%s\n", n->msg);
	(void)s; (void)v;
	return 0;
}
static const struct seq_operations k5_seq_ops = {
	.start = k5_seq_start, .next = k5_seq_next,
	.stop  = k5_seq_stop,  .show = k5_seq_show,
};
static int k5_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &k5_seq_ops);
}
static ssize_t k5_proc_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	// TODO: выделить узел, скопировать строку из ubuf (не более sizeof msg-1),
	//   поставить '\0', срезать '\n', и под мьютексом list_add_tail в k5_list.
	(void)file; (void)ubuf; (void)ppos;
	return count;
}
static const struct proc_ops k5_proc_ops = {
	.proc_open    = k5_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = seq_release,
	.proc_write   = k5_proc_write,
};
static int __init k5_init(void)
{
	if (!proc_create("k5_list", 0666, NULL, &k5_proc_ops))
		return -ENOMEM;
	return 0;
}
static void __exit k5_exit(void)
{
	struct k5_node *n, *tmp;
	remove_proc_entry("k5_list", NULL);
	list_for_each_entry_safe(n, tmp, &k5_list, list) {
		list_del(&n->list);
		kfree(n);
	}
}
module_init(k5_init);
module_exit(k5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K5: 02-procfs");
