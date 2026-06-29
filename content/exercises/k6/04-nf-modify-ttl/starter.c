// K6 04-nf-modify-ttl — уменьшить TTL исходящих пакетов с пересчётом чексуммы.
// Хук на LOCAL_OUT: сделать заголовок writable, ПЕРЕПОЛУЧИТЬ указатель,
// уменьшить TTL и инкрементально обновить IP-чексумму (ip_decrease_ttl).
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <net/ip.h>               // ip_decrease_ttl
#include <net/net_namespace.h>    // init_net

static unsigned int ttl_hook(void *priv, struct sk_buff *skb,
			     const struct nf_hook_state *state)
{
	struct iphdr *iph;

	if (!skb)
		return NF_ACCEPT;
	iph = ip_hdr(skb);
	if (!iph || iph->ttl <= 1)        // «умирающие» не трогаем (их дропнет стек с ICMP)
		return NF_ACCEPT;

	// TODO:
	//   1) сделать первые sizeof(struct iphdr) байт writable (расшарит буфер при
	//      необходимости — нельзя писать в shared/cloned skb, §8.1/§9):
	//        if (skb_ensure_writable(skb, sizeof(struct iphdr)))
	//            return NF_DROP;       // нет памяти — дроп
	//   2) ПЕРЕПОЛУЧИТЬ указатель (буфер мог переехать!, §8.1, баг #5):
	//        iph = ip_hdr(skb);
	//   3) уменьшить TTL с инкрементальным пересчётом IP-чексуммы:
	//        ip_decrease_ttl(iph);    // ttl-- ; iph->check обновлён
	(void)iph;
	return NF_ACCEPT;
}

static struct nf_hook_ops nfho = {
	.hook     = ttl_hook,
	.pf       = NFPROTO_IPV4,
	.hooknum  = NF_INET_LOCAL_OUT,    // локально сгенерированные исходящие пакеты
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
MODULE_DESCRIPTION("CPPCourse K6: 04-nf-modify-ttl");
