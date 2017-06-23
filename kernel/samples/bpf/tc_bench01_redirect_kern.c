/*  TC (Traffic Control) eBPF redirect benchmark
 *
 *  NOTICE: TC loading is different from XDP loading. TC bpf objects
 *          use the 'tc' cmdline tool from iproute2 for loading and
 *          attaching bpf programs.
 *
 *  Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 */
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/if_vlan.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/udp.h>

#include <uapi/linux/pkt_cls.h>

#include "bpf_helpers.h"

/* Notice: TC and iproute2 bpf-loader uses another elf map layout */
struct bpf_elf_map {
	__u32 type;
	__u32 size_key;
	__u32 size_value;
	__u32 max_elem;
	__u32 flags;
	__u32 id;
	__u32 pinning;
};

/* TODO: Describe what this PIN_GLOBAL_NS value 2 means???
 *
 * A file is automatically created here:
 *  /sys/fs/bpf/tc/globals/egress_ifindex
 */
#define PIN_GLOBAL_NS	2

struct bpf_elf_map SEC("maps") egress_ifindex = {
	.type = BPF_MAP_TYPE_ARRAY,
	.size_key = sizeof(int),
	.size_value = sizeof(int),
	.pinning = PIN_GLOBAL_NS,
	.max_elem = 1,
};

static void swap_src_dst_mac(void *data)
{
	unsigned short *p = data;
	unsigned short dst[3];

	dst[0] = p[0];
	dst[1] = p[1];
	dst[2] = p[2];
	p[0] = p[3];
	p[1] = p[4];
	p[2] = p[5];
	p[3] = dst[0];
	p[4] = dst[1];
	p[5] = dst[2];
}

/* Notice this section name is used when attaching TC filter
 *
 * Like:
 *  $TC qdisc   add dev $DEV clsact
 *  $TC filter  add dev $DEV ingress bpf da obj $BPF_OBJ sec ingress_redirect
 *  $TC filter show dev $DEV ingress
 *  $TC filter  del dev $DEV ingress
 *
 * Does TC redirect respect IP-forward settings?
 *
 */
SEC("ingress_redirect")
int _ingress_redirect(struct __sk_buff *skb)
{
	void *data     = (void *)(long)skb->data;
	void *data_end = (void *)(long)skb->data_end;
	struct ethhdr *eth = data;
	int key = 0, *ifindex;

	if (data + sizeof(*eth) > data_end)
		return TC_ACT_OK;

	/* Keep ARP resolution working */
	if (eth->h_proto == htons(ETH_P_ARP))
		return TC_ACT_OK;

	/* Lookup what ifindex to redirect packets to */
	ifindex = bpf_map_lookup_elem(&egress_ifindex, &key);
	if (!ifindex)
		return TC_ACT_OK;

	if (*ifindex == 0)
		return TC_ACT_OK; // or TC_ACT_SHOT ?

	if (*ifindex == 42)  /* Hack: use ifindex==42 as DROP switch */
		return TC_ACT_SHOT;

	/* FIXME: with mlx5 we need to update MAC-addr else the HW
	 * will drop the frames silently.
	 */

	/* Swap src and dst mac-addr if ingress==egress */
	if (*ifindex == 5)
		swap_src_dst_mac(data);

	//return bpf_redirect(*ifindex, BPF_F_INGRESS); // __bpf_rx_skb
	return bpf_redirect(*ifindex, 0); // __bpf_tx_skb / __dev_xmit_skb
}

char _license[] SEC("license") = "GPL";
