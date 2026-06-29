// K6 02-nf-port-filter — дроп TCP по порту назначения (параметр модуля).
// Хук на LOCAL_IN: безопасно (skb_header_pointer) достать TCP-заголовок,
// если dport == block_port → NF_DROP. block_port задаётся параметром модуля.
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/net.h>            // net_ratelimit
#include <net/net_namespace.h>    // init_net

static int block_port;            // 0 = выключено
module_param(block_port, int, 0644);
MODULE_PARM_DESC(block_port, "TCP dst port to drop (0 = off)");

static unsigned int hook_fn(void *priv, struct sk_buff *skb,
			    const struct nf_hook_state *state)
{
	struct iphdr *iph;
	struct tcphdr _tcph, *tcph = NULL;

	if (!skb || block_port == 0)
		return NF_ACCEPT;
	iph = ip_hdr(skb);
	if (!iph || iph->protocol != IPPROTO_TCP)
		return NF_ACCEPT;

	// TODO:
	//   1) безопасно достать TCP-заголовок (пакет может быть короче/нелинейным, §7.3):
	//        tcph = skb_header_pointer(skb, iph->ihl * 4, sizeof(_tcph), &_tcph);
	//        if (!tcph) return NF_ACCEPT;
	//   2) сравнить порт (он в network byte order — приведи через ntohs):
	//        if (ntohs(tcph->dest) == block_port) {
	//            if (net_ratelimit()) pr_info("k6: drop TCP ->:%d\n", block_port);
	//            return NF_DROP;
	//        }
	(void)iph; (void)tcph; (void)_tcph;
	return NF_ACCEPT;
}

static struct nf_hook_ops nfho = {
	.hook     = hook_fn,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_LOCAL_IN,
	.priority = NF_IP_PRI_FILTER,
};

static int __init k6_init(void)
{
	return nf_register_net_hook(&init_net, &nfho);
}
static void __exit k6_exit(void)
{
	nf_unregister_net_hook(&init_net, &nfho);
}
module_init(k6_init);
module_exit(k6_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CPPCourse K6: 02-nf-port-filter");
