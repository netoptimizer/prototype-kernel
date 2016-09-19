============
Requirements
============

Driver RX hook
==============

Access to packet-data payload before allocating any meta-data
structures, like SKBs.  It is key to performance, allowing processing
RX "packet-pages" directly out of drivers RX ring queue.


Early drop
==========

Early drop is key for the DoS (Denial of Service) mitigation use-cases.
It builds upon a principal of spending/investing as few CPU cycles as
possible on a packet that will get dropped anyhow.

Doing this "inline" before delivery to the normal network stack, have
the advantage that packets that does need delivery to the normal
network stack, can still get all the features and benefits as before.
(No need to deploy a bypass facility, just to reinject "good" packets
into the stack again).


Write access to packet-data
===========================

Need the ability to modify packet-data.  This is unfortunately often
difficult to obtain, as it requires fundamental changes to the drivers
memory model.

Unfortunately most driver don't have "writable" packet-data as
default.  The packet-data in drivers is often not writable, because
the drivers likely have choosen to work-arounds performance
bottlenecks in both the page-allocator and DMA APIs, which side-effect
is read-only packet-pages.

Instead most drivers, allocate both a SKB and a writable memory
buffer, which the packet headers are copied into (and for placing
``skb_shared_info``). Afterwards the SKB (and ``skb_shared_info``) is
adjusted to point into the remaining payload (pointing past the
headers just copied).


Header push and pop
===================

The ability to push (add) or pop (remove) packet headers indirectly
depend on write acces to packet-data. (One could argue that a pure
pop, could be implemented by only adjusting the payload offset, thus
no write-access).

This requirement goes hand-in-hand with tunnel encapsulation or
decapsulation.  It is also relevant for e.g adding a VLAN head as
needed by the :doc:`../use-cases/xdp_use_case_ddos_scrubber` in-order
to workaround the :ref:`XDP_TX` single NIC limitation.

This requirement implies the ability to adjust the packet-data start
offset/pointer and packet length.  This requires additional data to be
returned

This also have implication for how much headroom drivers should
reserve.


Page per packet
===============


Packet forwarding
=================

Implementing a router/forwarding data plane is DPDK prime example for
demonstrating superior performance.  For the shear ability to compare
against DPDK, XDP also need a forwarding capability.


RX bulking
==========

