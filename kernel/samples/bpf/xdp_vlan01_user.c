/* SPDX-License-Identifier: GPL-2.0
 *  Copyright(c) 2018 Jesper Dangaard Brouer.
 */
static const char *__doc__= "XDP/TC VLAN manipulation example";

static const char *__doc2__ =
"For now manually install programs with 'ip' tools command:\n"
"\n"
"export ROOTDEV=ixgbe2\n"
"ip link set $ROOTDEV xdp off\n"
"ip link set $ROOTDEV xdp object xdp_vlan01_kern.o section xdp_drop_vlan_4011\n"
"";

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <linux/if_link.h>

int main(int argc, char **argv)
{
	printf("Simple: %s\n\n", __doc__);
	printf("%s\n", __doc2__);

	return EXIT_SUCCESS;
}
