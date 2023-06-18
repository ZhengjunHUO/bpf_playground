#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* store L3 blacklist rules for ingress */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1000);
    __type(key, __u32);
    __type(value, _Bool);
} ingress_blacklist SEC(".maps");

/* store L3 blacklist rules for egress */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1000);
    __type(key, __u32);
    __type(value, _Bool);
} egress_blacklist SEC(".maps");

/* apply saved rules to ingress/egress packets, drop the packet if match */
static inline int filter_packet(struct __sk_buff *skb) {
    void *data = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;

    bool isIngress = false;
    if (skb->ingress_ifindex > 0) {
        isIngress = true;
    }

    struct iphdr *iphd = data;
    __u32 iphdr_len = sizeof(struct iphdr);
    // avoid verifier's complain
    if (data + iphdr_len > data_end)
        return 1;

    if (iphd->protocol == IPPROTO_TCP) {
        struct tcphdr *tcphd = data + iphdr_len;
        __u32 tcphdr_len = sizeof(struct tcphdr);
        // avoid verifier's complain
        if ((void *)tcphd + tcphdr_len > data_end)
            return 1;
    }

    if (iphd->protocol == IPPROTO_UDP) {
        struct udphdr *udphd = data + iphdr_len;
        __u32 udphdr_len = sizeof(struct udphdr);
        // avoid verifier's complain
        if ((void *)udphd + udphdr_len > data_end)
            return 1;
    }

    bool isBannedL3;
    if (isIngress) {
	__u32 src_addr = bpf_ntohl(iphd->saddr);
        bpf_printk("Ingress from %lu", src_addr);
        isBannedL3 = bpf_map_lookup_elem(&ingress_blacklist, &src_addr);
    } else {
	__u32 dst_addr = bpf_ntohl(iphd->daddr);
        bpf_printk("Egress to %lu", dst_addr);
        isBannedL3 = bpf_map_lookup_elem(&egress_blacklist, &dst_addr);
    }

    __u32 bitmap = (isBannedL3 << 1) | isIngress;

    bpf_printk("bitmap is %d", bitmap);

    // return 0 => drop
    if (bitmap > 1) {
        return 0;
    }

    return 1;
}

SEC("tc")
int tc_filter(struct __sk_buff *skb) {
    if (filter_packet(skb) == 0) {
        bpf_printk("Should be dropped");
        return TC_ACT_SHOT;
    }

    bpf_printk("Should pass");
    return TC_ACT_UNSPEC;
}

char __license[] SEC("license") = "Dual MIT/GPL";
