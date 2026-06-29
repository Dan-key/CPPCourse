// K6 03-nf-log-stats — счётчик пакетов по протоколу через per-CPU + procfs.
// Хук на LOCAL_IN считает пакеты по протоколу в per-CPU счётчики (без локов на
// hot path, §15.2), /proc/k6_stats отдаёт агрегат по всем ядрам.
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/net_namespace.h>    // init_net

struct stats { u64 tcp, udp, icmp, other; };
static DEFINE_PER_CPU(struct stats, k6_stats);   // по экземпляру на ядро

static unsigned int count_hook(void *priv, struct sk_buff *skb,
			       const struct nf_hook_state *state)
{
	struct iphdr *iph;

	if (!skb)
		return NF_ACCEPT;
	iph = ip_hdr(skb);
	if (!iph)
		return NF_ACCEPT;

	// TODO: инкрементировать СВОЙ per-CPU счётчик по протоколу, БЕЗ локов:
	//   switch (iph->protocol) {
	//   case IPPROTO_TCP:  this_cpu_inc(k6_stats.tcp);  break;
	//   case IPPROTO_UDP:  this_cpu_inc(k6_stats.udp);  break;
	//   case IPPROTO_ICMP: this_cpu_inc(k6_stats.icmp); break;
	//   default:           this_cpu_inc(k6_stats.other); break;
	//   }
	(void)iph;
	return NF_ACCEPT;
}

// /proc/k6_stats: агрегировать по всем ядрам (дорого, но read редкий, §15.2):
static int k6_show(struct seq_file *m, void *v)
{
	struct stats total = {0};
	int cpu;

	for_each_possible_cpu(cpu) {
		struct stats *s = per_cpu_ptr(&k6_stats, cpu);

		total.tcp += s->tcp;
		total.udp += s->udp;
		total.icmp += s->icmp;
		total.other += s->other;
	}
	seq_printf(m, "tcp=%llu udp=%llu icmp=%llu other=%llu\n",
		   (unsigned long long)total.tcp, (unsigned long long)total.udp,
		   (unsigned long long)total.icmp, (unsigned long long)total.other);
	return 0;
}
static int k6_open(struct inode *i, struct file *f)
{
	return single_open(f, k6_show, NULL);
}
static const struct proc_ops k6_pops = {
	.proc_open    = k6_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static struct nf_hook_ops nfho = {
	.hook     = count_hook,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_LOCAL_IN,
	.priority = NF_IP_PRI_FILTER,
};

static struct proc_dir_entry *k6_ent;

static int __init k6_init(void)
{
	k6_ent = proc_create("k6_stats", 0444, NULL, &k6_pops);   // procfs ДО хука
	if (!k6_ent)
		return -ENOMEM;
	if (nf_register_net_hook(&init_net, &nfho)) {
		proc_remove(k6_ent);
		return -EINVAL;
	}
	return 0;
}
static void __exit k6_exit(void)
{
	nf_unregister_net_hook(&init_net, &nfho);   // 1. снять хук + дождаться in-flight (§16.1)
	proc_remove(k6_ent);                        // 2. убрать procfs
}
module_init(k6_init);
module_exit(k6_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K6: 03-nf-log-stats");
