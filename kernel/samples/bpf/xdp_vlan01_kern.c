/* SPDX-License-Identifier: GPL-2.0
 *  Copyright(c) 2018 Jesper Dangaard Brouer.
 *
 * XDP/TC VLAN manipulation example
 *
 * GOTCHA: Remember to disable NIC hardware offloading of VLANs,
 * else the VLAN tags are NOT inlined in the packet payload:
 *
 *  # ethtool -K ixgbe2 rxvlan off
 *
 * Verify setting:
 *  # ethtool -k ixgbe2 | grep rx-vlan-offload
 *  rx-vlan-offload: off
 *
 */
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_vlan.h>
#include <uapi/linux/in.h>
#include "bpf_helpers.h"

/* linux/if_vlan.h have not exposed this as UAPI, thus mirror some here
 *
 * 	struct vlan_hdr - vlan header
 * 	@h_vlan_TCI: priority and VLAN ID
 *	@h_vlan_encapsulated_proto: packet type ID or len
 */
struct _vlan_hdr {
	__be16 h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto;
};
#define VLAN_PRIO_MASK		0xe000 /* Priority Code Point */
#define VLAN_PRIO_SHIFT		13
#define VLAN_CFI_MASK		0x1000 /* Canonical Format Indicator */
#define VLAN_TAG_PRESENT	VLAN_CFI_MASK
#define VLAN_VID_MASK		0x0fff /* VLAN Identifier */
#define VLAN_N_VID		4096

struct parse_pkt {
	u16 l3_proto;
	u16 l3_offset;
	u16 vlan_outer;
	u16 vlan_inner;
	u8  vlan_outer_offset;
	u8  vlan_inner_offset;
};

char _license[] SEC("license") = "GPL";

static __always_inline
bool parse_eth_frame(struct ethhdr *eth, void *data_end, struct parse_pkt *pkt)
{
	u16 eth_type;
	u8 offset;

	offset = sizeof(*eth);
	/* Make sure packet is large enough for parsing eth + 2 VLAN headers */
	if ((void *)eth + offset + (2*sizeof(struct _vlan_hdr)) > data_end)
		return false;

	eth_type = eth->h_proto;

	/* Handle outer VLAN tag */
	if (eth_type == htons(ETH_P_8021Q) || eth_type == htons(ETH_P_8021AD)) {
		struct _vlan_hdr *vlan_hdr;

		vlan_hdr = (void *)eth + offset;
		pkt->vlan_outer_offset = offset;
		pkt->vlan_outer = ntohs(vlan_hdr->h_vlan_TCI) & VLAN_VID_MASK;
		eth_type        = vlan_hdr->h_vlan_encapsulated_proto;
		offset += sizeof(*vlan_hdr);
	}

	/* Handle inner (double) VLAN tag */
	if (eth_type == htons(ETH_P_8021Q) || eth_type == htons(ETH_P_8021AD)) {
		struct _vlan_hdr *vlan_hdr;

		vlan_hdr = (void *)eth + offset;
		pkt->vlan_inner_offset = offset;
		pkt->vlan_inner = ntohs(vlan_hdr->h_vlan_TCI) & VLAN_VID_MASK;
		eth_type        = vlan_hdr->h_vlan_encapsulated_proto;
		offset += sizeof(*vlan_hdr);
	}

	pkt->l3_proto = ntohs(eth_type); /* Convert to host-byte-order */
	pkt->l3_offset = offset;

	return true;
}

SEC("xdp_drop_vlan_4011")
int  xdp_prognum0(struct xdp_md *ctx)
{
        void *data_end = (void *)(long)ctx->data_end;
        void *data     = (void *)(long)ctx->data;
	struct parse_pkt pkt = { 0 };

	if (!parse_eth_frame(data, data_end, &pkt))
		return XDP_ABORTED;

	/* Allow ARP-packet through, e.g test with arping */
	if (pkt.l3_proto == ETH_P_ARP)
		return XDP_PASS;

	/* Drop specific VLAN ID example */
	if (pkt.vlan_outer == 4011) /* == 0xFAB */
		return XDP_ABORTED;

	return XDP_PASS;
}
/*
Commands to setup VLAN on Linux to test packets gets dropped:

 export ROOTDEV=ixgbe2
 export VLANID=4011
 ip link add link $ROOTDEV name $ROOTDEV.$VLANID type vlan id $VLANID
 ip link set dev  $ROOTDEV.$VLANID up

 ip link set dev $ROOTDEV mtu 1508
 ip addr add 100.64.40.11/24 dev $ROOTDEV.$VLANID

Load prog with ip tool:

 ip link set $ROOTDEV xdp off
 ip link set $ROOTDEV xdp object xdp_vlan01_kern.o section xdp_drop_vlan_4011

*/
