/*  XDP Stateful
 *
 *  Copyright(c) 2018 Justin Iurman
 */
#define KBUILD_MODNAME "stateful"
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/if_vlan.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>
#include <uapi/linux/udp.h>
#include "bpf_helpers.h"

enum {
        TARGET_DROP = 0,
        TARGET_ACCEPT,
        TARGET_MAX
};

struct three_tuple {
	__u8  protocol;
	__u32 ip_source;
	__u32 ip_destination;
};

struct five_tuple {
        __u8  protocol;
	__u32 ip_source;
	__u32 ip_destination;
	__u16 port_source;
	__u16 port_destination;
};

struct flow_state {
	__u64 timestamp;
	__u8  tcp_flags;
	__u64 counter;
};

struct tmp_ports {
	__u16 src;
	__u16 dst;
};

struct vlan_hdr {
	__be16 h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto;
};

struct bpf_map_def SEC("maps") stateful_conn_track = {
	.type        = BPF_MAP_TYPE_HASH,
	.key_size    = sizeof(struct five_tuple),
	.value_size  = sizeof(struct flow_state),
	.max_entries = 100000,
	.map_flags   = BPF_F_NO_PREALLOC,
};

struct bpf_map_def SEC("maps") stateful_three_tuple = {
	.type        = BPF_MAP_TYPE_HASH,
	.key_size    = sizeof(struct three_tuple),
	.value_size  = sizeof(__u8),
	.max_entries = 100000,
	.map_flags   = BPF_F_NO_PREALLOC,
};

struct bpf_map_def SEC("maps") stateful_five_tuple = {
	.type        = BPF_MAP_TYPE_HASH,
	.key_size    = sizeof(struct five_tuple),
	.value_size  = sizeof(__u8),
	.max_entries = 100000,
	.map_flags   = BPF_F_NO_PREALLOC,
};

//#define DEBUG 1
#ifdef  DEBUG
/* Only use this for debug output. Notice output from bpf_trace_printk()
 * end-up in /sys/kernel/debug/tracing/trace_pipe
 */
#define bpf_debug(fmt, ...)						\
		({							\
			char ____fmt[] = fmt;				\
			bpf_trace_printk(____fmt, sizeof(____fmt),	\
				     ##__VA_ARGS__);			\
		})
#else
#define bpf_debug(fmt, ...) { }
#endif

/* Parse Ethernet layer 2, extract network layer 3 offset and protocol
 *
 * Returns false on error and non-supported ether-type
 */
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
	//bpf_debug("Debug: eth_type:0x%x\n", ntohs(eth_type));

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
	/* Handle double VLAN tagged packet */
	if (eth_type == htons(ETH_P_8021Q) || eth_type == htons(ETH_P_8021AD)) {
		struct vlan_hdr *vlan_hdr;

		vlan_hdr = (void *)eth + offset;
		offset += sizeof(*vlan_hdr);
		if ((void *)eth + offset > data_end)
			return false;
		eth_type = vlan_hdr->h_vlan_encapsulated_proto;
	}

	*eth_proto = ntohs(eth_type);
	*l3_offset = offset;
	return true;
}

bool extract_l4_data(struct xdp_md *ctx, u8 proto, void *hdr, struct tmp_ports *ports, u8 *tcp_flags)
{
	void *data_end = (void *)(long)ctx->data_end;
	struct udphdr *udph;
	struct tcphdr *tcph;

	switch (proto) {
	case IPPROTO_UDP:
		udph = hdr;
		if (udph + 1 > data_end) {
			bpf_debug("Invalid UDPv4 packet: L4off:%llu\n",
				  sizeof(struct iphdr) + sizeof(struct udphdr));
			return false;
		}
		ports->src = ntohs(udph->source);
		ports->dst = ntohs(udph->dest);
		*tcp_flags = 0;
		break;
	case IPPROTO_TCP:
		tcph = hdr;
		if (tcph + 1 > data_end) {
			bpf_debug("Invalid TCPv4 packet: L4off:%llu\n",
				  sizeof(struct iphdr) + sizeof(struct tcphdr));
			return false;
		}
		ports->src = ntohs(tcph->source);
		ports->dst = ntohs(tcph->dest);
		*tcp_flags = ((u8 *)tcph)[13];
		break;
	default:
		return true;
	}

	return true;
}

bool lookup_flow(struct five_tuple *key, u8 tcp_flags)
{
	struct flow_state *state = bpf_map_lookup_elem(&stateful_conn_track, key);
	if (state)
	{
		state->timestamp = bpf_ktime_get_ns(); 
                state->tcp_flags |= tcp_flags;
		state->counter++;

		return true;
	}

	return false;
}

bool lookup_match(struct bpf_map_def *map, void *key, u8 *action)
{
	u8 *target = bpf_map_lookup_elem(map, key);
	if (target)
	{
		if (*target != *action)
			*action = *target;

		return true;
	}

	return false;
}

void add_flow_entry(struct five_tuple key, u8 tcp_flags)
{
	u16 tmp_port = key.port_source;
	u32 tmp_ip = key.ip_source;

	struct flow_state state = {};
	state.timestamp = bpf_ktime_get_ns();
	state.tcp_flags |= tcp_flags;
	state.counter = 1L;

	bpf_map_update_elem(&stateful_conn_track, &key, &state, BPF_ANY);

	key.port_source = key.port_destination;
	key.ip_source = key.ip_destination;
	key.port_destination = tmp_port;
	key.ip_destination = tmp_ip;

	state.timestamp = 0L;
	state.tcp_flags = 0;
	state.counter = 0L;
	
	bpf_map_update_elem(&stateful_conn_track, &key, &state, BPF_ANY);
}

static __always_inline
u32 parse_ipv4(struct xdp_md *ctx, u64 l3_offset)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct iphdr *iph = data + l3_offset;
	struct three_tuple key_three_tuple = {};
	struct five_tuple key_five_tuple = {};
	u8 action = TARGET_ACCEPT;
	struct tmp_ports ports = {};	
	u32 srcip=0, dstip=0;
	u8 tcp_flags = 0;
	bool matched;

	/* Hint: +1 is sizeof(struct iphdr) */
	if (iph + 1 > data_end) {
		bpf_debug("Invalid IPv4 packet: L3off:%llu\n", l3_offset);
		return XDP_ABORTED;
	}

	srcip = ntohl(iph->saddr);
	dstip = ntohl(iph->daddr);

	if (!extract_l4_data(ctx, iph->protocol, iph + 1, &ports, &tcp_flags))
		return XDP_ABORTED;

	bpf_debug("Packet: (proto %u) sport = %u, dport = %u\n", iph->protocol, ports.src, ports.dst);
	
	// 3-tuple (both sides)
	key_three_tuple.protocol = iph->protocol;
	key_three_tuple.ip_source = srcip;
	key_three_tuple.ip_destination = dstip;
	matched = lookup_match(&stateful_three_tuple, &key_three_tuple, &action);

	// 5-tuple (both sides)
	key_five_tuple.protocol = iph->protocol;
	key_five_tuple.ip_source = srcip;
	key_five_tuple.ip_destination = dstip;
	key_five_tuple.port_source = ports.src;
	key_five_tuple.port_destination = ports.dst;
	matched |= lookup_match(&stateful_five_tuple, &key_five_tuple, &action);

	// If matched, Flow tracking based on 5-tuple
	if (matched && !lookup_flow(&key_five_tuple, tcp_flags))
	{
		add_flow_entry(key_five_tuple, tcp_flags);
	}

	return (action == TARGET_DROP) ? XDP_DROP : XDP_PASS;
}

static __always_inline
u32 handle_eth_protocol(struct xdp_md *ctx, u16 eth_proto, u64 l3_offset)
{
	switch (eth_proto) {
	case ETH_P_IP:
		return parse_ipv4(ctx, l3_offset);
		break;
	case ETH_P_IPV6: /* Not handler for IPv6 yet*/
	case ETH_P_ARP:  /* Let OS handle ARP */
		/* Fall-through */
	default:
		//bpf_debug("Not handling eth_proto:0x%x\n", eth_proto);
		return XDP_PASS;
	}
	return XDP_PASS;
}

SEC("xdp_prog")
int xdp_program(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct ethhdr *eth = data;
	u16 eth_proto;
	u64 l3_offset;

	if (!(parse_eth(eth, data_end, &eth_proto, &l3_offset))) {
		bpf_debug("Cannot parse L2: L3off:%llu proto:0x%x\n",
			  l3_offset, eth_proto);
		return XDP_PASS; /* Skip */
	}
	//bpf_debug("Reached L3: L3off:%llu proto:0x%x\n", l3_offset, eth_proto);

	return handle_eth_protocol(ctx, eth_proto, l3_offset);
}

char _license[] SEC("license") = "GPL";
