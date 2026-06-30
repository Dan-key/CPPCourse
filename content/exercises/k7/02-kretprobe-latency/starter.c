// K7 02-kretprobe-latency — латентность функции ядра через kretprobe.
// entry_handler засекает время входа в ri->data, handler считает дельту на возврате.
// /proc/k7_lat отдаёт count/last_ns/max_ns.
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/ktime.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static char *symbol = "do_sys_openat2";
module_param(symbol, charp, 0444);
MODULE_PARM_DESC(symbol, "kernel symbol to probe");

struct lat { ktime_t t0; };                // что храним между входом и выходом

static atomic64_t count   = ATOMIC64_INIT(0);
static atomic64_t last_ns = ATOMIC64_INIT(0);
static atomic64_t max_ns  = ATOMIC64_INIT(0);

static int ent(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// TODO: засечь время входа в ri->data:
	//   struct lat *d = (struct lat *)ri->data;
	//   d->t0 = ktime_get();
	return 0;
}
static int ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// TODO: посчитать дельту и накопить (атомарный контекст — без сна):
	//   struct lat *d = (struct lat *)ri->data;
	//   s64 ns = ktime_to_ns(ktime_sub(ktime_get(), d->t0));
	//   atomic64_inc(&count);
	//   atomic64_set(&last_ns, ns);
	//   if (ns > atomic64_read(&max_ns)) atomic64_set(&max_ns, ns);
	return 0;
}
static struct kretprobe krp = {
	.entry_handler = ent,
	.handler       = ret,
	.data_size     = sizeof(struct lat),   // на КАЖДЫЙ вызов «в полёте»
	.maxactive     = 64,                   // сколько одновременных вызовов держим
};

static int show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s count=%lld last_ns=%lld max_ns=%lld\n",
		   krp.kp.symbol_name,
		   (long long)atomic64_read(&count),
		   (long long)atomic64_read(&last_ns),
		   (long long)atomic64_read(&max_ns));
	return 0;
}
static int k7_open(struct inode *i, struct file *f) { return single_open(f, show, NULL); }
static const struct proc_ops pops = {
	.proc_open    = k7_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};
static struct proc_dir_entry *ent_pde;

static int __init m_init(void)
{
	int rc;

	krp.kp.symbol_name = symbol;
	ent_pde = proc_create("k7_lat", 0444, NULL, &pops);
	if (!ent_pde)
		return -ENOMEM;
	rc = register_kretprobe(&krp);
	if (rc) {
		proc_remove(ent_pde);
		return rc;
	}
	pr_info("k7: kretprobe на %s\n", krp.kp.symbol_name);
	return 0;
}
static void __exit m_exit(void)
{
	unregister_kretprobe(&krp);
	proc_remove(ent_pde);
}
module_init(m_init);
module_exit(m_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K7: 02-kretprobe-latency");
