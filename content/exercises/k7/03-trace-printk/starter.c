// K7 03-trace-printk — лог в ftrace ring buffer вместо dmesg.
// /proc/k7_trace: запись в него должна попасть в /sys/kernel/tracing/trace
// (через trace_printk), а НЕ флудить dmesg (как сделал бы printk).
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>            // noop_llseek
#include <linux/minmax.h>       // min

static ssize_t k7_write(struct file *f, const char __user *ubuf,
			size_t n, loff_t *ppos)
{
	char buf[128];
	size_t len = min(n, sizeof(buf) - 1);

	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len] = '\0';

	// TODO: записать маркер в ftrace ring buffer (НЕ printk/pr_info!):
	//   trace_printk("k7_trace: %s", buf);
	// trace_printk пишет в /sys/kernel/tracing/trace дёшево и без лока консоли (§13).

	return n;                    // «съели» весь ввод
}
static const struct proc_ops pops = {
	.proc_write = k7_write,
	.proc_lseek = noop_llseek,
};
static struct proc_dir_entry *ent;

static int __init m_init(void)
{
	ent = proc_create("k7_trace", 0222, NULL, &pops);   // write-only
	return ent ? 0 : -ENOMEM;
}
static void __exit m_exit(void)
{
	proc_remove(ent);
}
module_init(m_init);
module_exit(m_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K7: 03-trace-printk");
