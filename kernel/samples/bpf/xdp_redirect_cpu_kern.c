/*  XDP redirect to CPUs via cpu_map
 *
 * -=- WARNING -=- EXPERIMENTAL: featured under development!
 *
 *  GPLv2, Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat, Inc.
 */
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/if_vlan.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/ipv6.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>

#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

#define MAX_CPUS 12

/* Special map type that can XDP_REDIRECT frames to another CPU */
struct bpf_map_def SEC("maps") cpu_map = {
	.type		= BPF_MAP_TYPE_CPUMAP,
	.key_size	= sizeof(u32),
	.value_size	= sizeof(u32),
	.max_entries	= MAX_CPUS,
};

/* Count RX packets, as XDP bpf_prog doesn't get direct TX-success
 * feedback.  Redirect TX errors can be caught via a tracepoint.
 */
struct bpf_map_def SEC("maps") rx_cnt = {
	.type		= BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size	= sizeof(u32),
	.value_size	= sizeof(long),
	.max_entries	= 1,
};

/* Helper parse functions */

/* Parse Ethernet layer 2, extract network layer 3 offset and protocol
 *
 * Returns false on error and non-supported ether-type
 */
struct vlan_hdr {
	__be16 h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto;
};

static __always_inline
bool parse_eth(struct ethhdr *eth, void *data_end,
	       u16 *eth_proto, u64 *l3_offset)
{
	u16 eth_type;
	u64 offset;

	offset = sizeof(*eth);
	if ((void *)eth + offset > data_end)
		return false;

	eth_type = eth->h_proto;

	/* Skip non 802.3 Ethertypes */
	if (unlikely(ntohs(eth_type) < ETH_P_802_3_MIN))
		return false;

	/* Handle VLAN tagged packet */
	if (eth_type == htons(ETH_P_8021Q) || eth_type == htons(ETH_P_8021AD)) {
		struct vlan_hdr *vlan_hdr;

		vlan_hdr = (void *)eth + offset;
		offset += sizeof(*vlan_hdr);
		if ((void *)eth + offset > data_end)
			return false;
		eth_type = vlan_hdr->h_vlan_encapsulated_proto;
	}
	/* TODO: Handle double VLAN tagged packet */

	*eth_proto = ntohs(eth_type);
	*l3_offset = offset;
	return true;
}

static __always_inline
int get_proto_ipv4(struct xdp_md *ctx, u64 nh_off)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
        struct iphdr *iph = data + nh_off;

        if (iph + 1 > data_end)
                return 0;
        return iph->protocol;
}

static __always_inline
int get_proto_ipv6(struct xdp_md *ctx, u64 nh_off)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
        struct ipv6hdr *ip6h = data + nh_off;

        if (ip6h + 1 > data_end)
                return 0;
        return ip6h->nexthdr;
}



SEC("xdp_cpu_map0")
int  xdp_prog_cpu_map_prognum0(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	u32 cpu_dest = 0;
	u32 key = 0;
	long *value;

	/* Count RX packet in map */
	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value)
		*value += 1;

	return bpf_redirect_map(&cpu_map, cpu_dest, 0);
}

SEC("xdp_cpu_map_proto_seperate")
int  xdp_prog_cpu_map_prognum1(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	u8 ip_proto = IPPROTO_UDP;
	u16 eth_proto = 0;
	u64 l3_offset = 0;
	u32 cpu_dest = 0;
	u32 key = 0;
	long *value;

	/* Count RX packet in map */
	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value)
		*value += 1;

	if (!(parse_eth(eth, data_end, &eth_proto, &l3_offset))) {
		return XDP_PASS; /* Just skip */
	}

	/* Extract L4 protocol */
	switch (eth_proto) {
	case ETH_P_IP:
		ip_proto = get_proto_ipv4(ctx, l3_offset);
		break;
	case ETH_P_IPV6:
		ip_proto = get_proto_ipv6(ctx, l3_offset);
		break;
	case ETH_P_ARP:
		cpu_dest = 1; /* ARP packet handled on separate CPU */
		break;
	default:
		cpu_dest = 0;
	}

	/* Choose CPU based on L4 protocol */
	switch (ip_proto) {
	case IPPROTO_ICMP:
	case IPPROTO_ICMPV6:
		cpu_dest = 1;
		break;
	case IPPROTO_TCP:
		cpu_dest = 2;
		break;
	case IPPROTO_UDP:
		cpu_dest = 3;
		break;
	default:
		cpu_dest = 0;
	}

	if (cpu_dest >= MAX_CPUS )
		return XDP_ABORTED;

	return bpf_redirect_map(&cpu_map, cpu_dest, 0);
}

SEC("xdp_cpu_map_round_robin")
int  xdp_prog_cpu_map_prognum2(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	u32 cpu_dest = 0;
	u32 key = 0;
	long *value;

	/* Count RX packet in map */
	value = bpf_map_lookup_elem(&rx_cnt, &key);
	if (value) {
		*value += 1;
		cpu_dest = (u32)(*value) % 2;
	}

	if (cpu_dest >= MAX_CPUS )
		return XDP_ABORTED;

	return bpf_redirect_map(&cpu_map, cpu_dest, 0);
}

char _license[] SEC("license") = "GPL";
