#define KBUILD_MODNAME "foo"
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

char _license[] SEC("license") = "GPL";

SEC("xdp_drop")
int  xdp_drop0(struct xdp_md *ctx)
{
	return XDP_DROP;
}
