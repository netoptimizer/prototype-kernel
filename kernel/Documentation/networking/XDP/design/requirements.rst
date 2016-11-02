============
Requirements
============

Driver RX hook
==============

Gives us access to packet-data payload before allocating any meta-data
structures, like SKBs.  This is key to performance, as it allows
processing RX "packet-pages" directly out of the driver's RX ring
queue.


Early drop
==========

Early drop is key for the DoS (Denial of Service) mitigation use-cases.
It builds upon a principle of spending/investing as few CPU cycles as
possible on a packet that will get dropped anyhow.

Doing this "inline", before delivery to the normal network stack, has
the advantage that a packet that *does* need delivery to the normal
network stack can still get all the features and benefits as before;
there is thus no need to deploy a bypass facility merely to re-inject
"good" packets into the stack again.


Write access to packet data
===========================

XDP needs the ability to modify packet data.  This is unfortunately
often difficult to obtain, as it requires fundamental changes to the
driver's memory model.

Unfortunately most drivers don't have "writable" packet data as
default.  This is due to a workaround for performance bottlenecks in
both the page-allocator and DMA APIs, which has the side-effect of
necessitating read-only packet pages.

Instead, most drivers (currently) allocate both a SKB and a writable
memory buffer, in which to copy ("linearise") the packet headers, and
also store ``skb_shared_info``.  Then the remaining payload (pointing
past the headers just copied) is attached as (read-only) paged data.


Header push and pop
===================

The ability to push (add) or pop (remove) packet headers indirectly
depends on write access to packet-data.  (One could argue that a pure
pop could be implemented by only adjusting the payload offset, thus
not needing write access).

This requirement goes hand-in-hand with tunnel encapsulation or
decapsulation.  It is also relevant for e.g adding a VLAN header, as
needed by the :doc:`../use-cases/xdp_use_case_ddos_scrubber` in order
to work around the :ref:`XDP_TX` single NIC limitation.

This requirement implies the ability to adjust the packet-data start
offset/pointer and packet length.  This requires additional data to be
returned.

This also has implications for how much headroom drivers should
reserve in the SKB.


Page per packet
===============

.. memory model

On RX many NIC drivers splitup a memory page, to share it for multiple
packets, in-order to conserve memory.  Doing so complicates handling
and accounting of these memory pages, which affects performance.
Particularly the extra atomic refcnt handling needed for the page can
hurt performance.

XDP defines upfront a memory model where there is only one packet per
page.  This simplifies page handling and open up for future
extensions.

This requirement also (upfront) result in choosing not to support
things like, jumpo-frames, LRO and generally packets split over
multiple pages.

In the future, this strict memory model might be relaxed, but for now
it is a strict requirement.  With a more flexible
:ref:`ref_prog_negotiation` is might be possible to negotiate another
memory model. Given some specific XDP use-case might not require this
strict memory model.


Packet forwarding
=================

Implementing a router/forwarding data plane is DPDK's prime example
for demonstrating superior performance.  For the sheer ability to
compare against DPDK, XDP also needs a forwarding capability.


RX bulking
==========

.. desc why RX bulking is key
