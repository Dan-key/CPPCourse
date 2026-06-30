// K7 04-kprobe-arg — фильтр по АРГУМЕНТУ функции в kprobe.
// kprobe на do_sys_openat2(int dfd, ...): прочитать 1-й аргумент (dfd) через
// regs_get_kernel_argument и считать ТОЛЬКО открытия относительно cwd (dfd==AT_FDCWD).
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>       // regs_get_kernel_argument
#include <linux/fcntl.h>        // AT_FDCWD
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static DEFINE_PER_CPU(u64, cwd_opens);

static int pre(struct kprobe *p, struct pt_regs *regs)
{
	// TODO: прочитать 1-й аргумент do_sys_openat2(int dfd, ...) и считать
	//   ТОЛЬКО открытия относительно текущего каталога (dfd == AT_FDCWD):
	//     int dfd = (int)regs_get_kernel_argument(regs, 0);
	//     if (dfd == AT_FDCWD)
	//         this_cpu_inc(cwd_opens);
	return 0;
}
static struct kprobe kp = {
	.symbol_name = "do_sys_openat2",
	.pre_handler = pre,
};

static int show(struct seq_file *m, void *v)
{
	u64 total = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		total += per_cpu(cwd_opens, cpu);
	seq_printf(m, "cwd_opens=%llu\n", (unsigned long long)total);
	return 0;
}
static int k7_open(struct inode *i, struct file *f) { return single_open(f, show, NULL); }
static const struct proc_ops pops = {
	.proc_open    = k7_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};
static struct proc_dir_entry *ent;

static int __init m_init(void)
{
	int rc;

	ent = proc_create("k7_arg", 0444, NULL, &pops);
	if (!ent)
		return -ENOMEM;
	rc = register_kprobe(&kp);
	if (rc) {
		proc_remove(ent);
		return rc;
	}
	return 0;
}
static void __exit m_exit(void)
{
	unregister_kprobe(&kp);
	proc_remove(ent);
}
module_init(m_init);
module_exit(m_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K7: 04-kprobe-arg");
