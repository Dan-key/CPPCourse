// K7 01-kprobe-count — счётчик вызовов функции ядра через kprobe.
// Зарегистрировать kprobe на символ (параметр модуля, по умолчанию do_sys_openat2),
// в pre_handler инкрементировать per-CPU счётчик; /proc/k7_kprobe отдаёт сумму.
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static char *symbol = "do_sys_openat2";
module_param(symbol, charp, 0444);
MODULE_PARM_DESC(symbol, "kernel symbol to probe");

static DEFINE_PER_CPU(u64, hits);          // по экземпляру на ядро (без локов)

static int pre(struct kprobe *p, struct pt_regs *regs)
{
	// TODO: инкрементировать СВОЙ per-CPU счётчик.
	//   ВНИМАНИЕ: это АТОМАРНЫЙ контекст (§1.3/§7.4) — без локов/сна, только per-CPU:
	//     this_cpu_inc(hits);
	return 0;                          // 0 = продолжить как обычно
}
static struct kprobe kp = { .pre_handler = pre };

static int show(struct seq_file *m, void *v)
{
	u64 total = 0;
	int cpu;

	for_each_possible_cpu(cpu)         // редкий read /proc — агрегируем по ядрам
		total += per_cpu(hits, cpu);
	seq_printf(m, "%s hits=%llu\n", kp.symbol_name, (unsigned long long)total);
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
	int ret;

	kp.symbol_name = symbol;
	ent = proc_create("k7_kprobe", 0444, NULL, &pops);   // procfs ДО probe
	if (!ent)
		return -ENOMEM;
	ret = register_kprobe(&kp);                          // probe — последним
	if (ret) {
		proc_remove(ent);
		return ret;
	}
	pr_info("k7: kprobe на %s\n", kp.symbol_name);
	return 0;
}
static void __exit m_exit(void)
{
	unregister_kprobe(&kp);     // 1. снять probe (иначе breakpoint в freed-памяти)
	proc_remove(ent);           // 2. убрать procfs
}
module_init(m_init);
module_exit(m_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K7: 01-kprobe-count");
