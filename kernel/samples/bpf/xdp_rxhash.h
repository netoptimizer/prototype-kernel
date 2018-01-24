// Needed for (obsoleted) xdp_rxhash program.
#ifndef __XDP_RXHASH_H__
#define __XDP_RXHASH_H__

/* RX-hash proposed extending xdp_md, was rejected

 struct xdp_md {
        __u32 data;
        __u32 data_end;
	__u32 data_meta;
+       __u32 rxhash;
+       // Do we need rxhash_type here???
+       __u32 rxhash_type;
+       // Can be done as a translation, reading part of xdp_buff->flags
 };
*/

/* BPF_FUNC_xdp_rxhash flags */
#define BPF_F_RXHASH_SET		0ULL
#define BPF_F_RXHASH_GET		(1ULL << 0)

/* XDP rxhash have an associated type, which is related to the RSS
 * (Receive Side Scaling) standard, but NIC HW have different mapping
 * and support. Thus, create mapping that is interesting for XDP.  XDP
 * would primarly want insight into L3 and L4 protocol info.
 *
 * TODO: Likely need to get extended with "L3_IPV6_EX" due RSS standard
 *
 * The HASH_TYPE will be returned as the top 32-bit of the 64-bit
 * rxhash (internally type stored in xdp_buff->flags).
 */
#define XDP_HASH(x)		((x) & ((1ULL << 32)-1))
#define XDP_HASH_TYPE(x)	((x) >> 32)

#define XDP_HASH_TYPE_L3_SHIFT	0
#define XDP_HASH_TYPE_L3_BITS	3
#define XDP_HASH_TYPE_L3_MASK	((1ULL << XDP_HASH_TYPE_L3_BITS)-1)
#define XDP_HASH_TYPE_L3(x)	((x) & XDP_HASH_TYPE_L3_MASK)
enum {
	XDP_HASH_TYPE_L3_IPV4 = 1,
	XDP_HASH_TYPE_L3_IPV6,
};

#define XDP_HASH_TYPE_L4_SHIFT	XDP_HASH_TYPE_L3_BITS
#define XDP_HASH_TYPE_L4_BITS	5
#define XDP_HASH_TYPE_L4_MASK						\
	(((1ULL << XDP_HASH_TYPE_L4_BITS)-1) << XDP_HASH_TYPE_L4_SHIFT)
#define XDP_HASH_TYPE_L4(x)	((x) & XDP_HASH_TYPE_L4_MASK)
enum {
	_XDP_HASH_TYPE_L4_TCP = 1,
	_XDP_HASH_TYPE_L4_UDP,
};
#define XDP_HASH_TYPE_L4_TCP (_XDP_HASH_TYPE_L4_TCP << XDP_HASH_TYPE_L4_SHIFT)
#define XDP_HASH_TYPE_L4_UDP (_XDP_HASH_TYPE_L4_UDP << XDP_HASH_TYPE_L4_SHIFT)

#define XDP_HASH_TYPE_BITS   (XDP_HASH_TYPE_L3_BITS + XDP_HASH_TYPE_L4_BITS)
#define XDP_HASH_TYPE_MASK   (XDP_HASH_TYPE_L3_MASK | XDP_HASH_TYPE_L4_MASK)

#endif /* __XDP_RXHASH_H__ */
