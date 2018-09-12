#!/bin/bash

cleanup()
{
	if [ "$?" = "0" ]; then
		echo "selftests: test_xdp_meta [PASS]";
	else
		echo "selftests: test_xdp_meta [FAILED]";
	fi

	set +e
	ip link del veth1 2> /dev/null
	ip netns del ns1 2> /dev/null
	ip netns del ns2 2> /dev/null
}

ip link set dev lo xdp off 2>/dev/null > /dev/null
if [ $? -ne 0 ];then
	echo "selftests: [SKIP] Could not run test without the ip xdp support"
	exit 0
fi

cleanup

# Exit on failure
set -e

# Make rest of shell verbose, showing comments as doc/info
set -v
# Create two namespaces
ip netns add ns1
ip netns add ns2

# Run cleanup if failing or on kill
###trap cleanup 0 2 3 6 9

# Create veth pair
ip link add veth1 type veth peer name veth2

# Move veth1 and veth2 into the respective namespaces
ip link set veth1 netns ns1
ip link set veth2 netns ns2

# Disable rx-vlan-offload (mostly needed on ns1)
ip netns exec ns1 ethtool -K veth1 rxvlan off
ip netns exec ns2 ethtool -K veth2 rxvlan off

export IPADDR1=100.64.41.1
export IPADDR2=100.64.41.2

# In ns1/veth1 add IP-addr on plain net_device
ip netns exec ns1 ip addr add ${IPADDR1}/24 dev veth1
ip netns exec ns1 ip link set veth1 up

# In ns2/veth2 create VLAN device
export VLAN=4011
export DEVNS2=veth2
ip netns exec ns2 ip link add link $DEVNS2 name $DEVNS2.$VLAN type vlan id $VLAN
ip netns exec ns2 ip addr add ${IPADDR2}/24 dev $DEVNS2.$VLAN
ip netns exec ns2 ip link set $DEVNS2 up
ip netns exec ns2 ip link set $DEVNS2.$VLAN up

# At this point, the hosts cannot reach each-other,
# because ns2 are using VLAN tags on the packets.

ip netns exec ns2 sh -c 'ping -W 3 -c 1 100.64.41.1 || echo "Okay ping fails"'


# Now we can use the xdp_vlan01 program to pop/push these VLAN tags
# -----------------------------------------------------------------
# In ns1: ingress use XDP to remove VLAN tags
export DEVNS1=veth1
export FILE=xdp_vlan01_kern.o

#  (del cmd)
#  ip netns exec ns1 ip link set $DEVNS1 xdp off
#
ip netns exec ns1 ip link set $DEVNS1 xdp object $FILE section xdp_vlan_remove_outer2

# In ns1: egress use TC to add back VLAN tag 4011
#  (del cmd)
#  tc qdisc del dev $DEVNS1 clsact 2> /dev/null
#
ip netns exec ns1 tc qdisc add dev $DEVNS1 clsact
ip netns exec ns1 tc filter add dev $DEVNS1 egress \
  prio 1 handle 1 bpf da obj $FILE sec tc_vlan_push

# Now the namespaces can reach each-other, test with ping:

ip netns exec ns2 ping -W 2 -c 3 $IPADDR1

ip netns exec ns1 ping -W 2 -c 3 $IPADDR2

