// K6 01-nf-drop — netfilter-хук на LOCAL_IN, который ДРОПАЕТ ICMP.
// Зарегистрировать хук, в обработчике: прочитать ip_hdr, при IPPROTO_ICMP
// вернуть NF_DROP (ratelimited-лог, §15 лекции), остальное — NF_ACCEPT.
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/net.h>            // net_ratelimit
#include <net/net_namespace.h>    // init_net

static unsigned int hook_fn(void *priv, struct sk_buff *skb,
			    const struct nf_hook_state *state)
{
	struct iphdr *iph;

	if (!skb)
		return NF_ACCEPT;
	iph = ip_hdr(skb);
	if (!iph)
		return NF_ACCEPT;

	// TODO: если iph->protocol == IPPROTO_ICMP — вернуть NF_DROP.
	//   Лог БЕЗ флуда (хук бежит на каждый пакет, §15):
	//     if (net_ratelimit()) pr_info("k6: drop ICMP from %pI4\n", &iph->saddr);
	(void)iph;
	return NF_ACCEPT;
}

static struct nf_hook_ops nfho = {
	.hook     = hook_fn,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_LOCAL_IN,    // входящий нам трафик (loopback тоже проходит)
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
MODULE_DESCRIPTION("CPPCourse K6: 01-nf-drop");
