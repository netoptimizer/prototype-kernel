=======================
Use-case: DDoS scrubber
=======================
:Version: 0.2
:Status: Proposal, need some new XDP features

This document investigates whether XDP can be used for implementing a
machine that does traffic scrubbing at the edge of the network.

DDoS volume attacks
===================

This idea/use-case comes from a customer.  They have a need to perform
traffic scrubbing or cleaning when getting attacked by DDoS volume
attacks.  They have much larger pipes to the Internet than their
internal backbone can actually handle.

Usually a specific IP address is attacked.  When that happens, the IP
address is placed into MPLS-VRF alternative routing tables, so the
traffic gets routed through some scrubbing servers.

The purpose of the scrubbing servers is to reduce (or drop) enough
traffic, such that the DoS volume attack is less than the capacity of
the internal backbone.

Forward clean traffic
=====================

The clean/good traffic needs to be forwarded towards the internal
backbone.

To get around the XDP limitation of only sending back out the same
NIC, they want to add a VLAN header to the packet before calling
:ref:`XDP_TX`, allowing them to catch the traffic and re-steer it back
into the main MPLS-VRF routing table.


Need: traffic sampling XDP_DROP
===============================

They want a way to analyze the traffic they drop (XDP_DROP), to catch
false positives.  This could be implemented by sampling the drop
traffic, by returning XDP_PASS a percentage of the times, and then
have a userspace tcpdump running.

To indicate which eBPF rule caused the drop, they were thinking of
modifying the packet header by adding a VLAN id.  That way the tcpdump
could run on a net_device with a given VLAN.

.. note::

   **NEW-ACTION**: The sampling could be implemented more efficiently,
   if there were a XDP_DUMP action which sent the sampled packets to
   an AF_PACKET socket.

Need: traffic sampling XDP_TX
=============================

If the scrubber filter is not good enough, then too much bad traffic
is allowed through.  This is usually the base case, once the attack
starts.

Thus, they have need for analysing the traffic that gets forwarded
with :ref:`XDP_TX`. (ISSUE) The is currently no way to sample or dump
the :ref:`XDP_TX` traffic.

A physical solution could be to do switch-port mirroring of the
traffic, and then have another machine (or even the same machine)
receive traffic for analysis.  They were talking about just using the
same machine (as there usually are two NIC ports), but the worry is
that this would cost double the PCIe bandwidth.

.. warning::

   **NEW-FEATURE:** A software solution could be a combination of
   :ref:`XDP_TX` and XDP_DUMP.  Doing both XDP_TX and XDP_DUMP would
   only cost an extra page refcnt.  They only need sampling.  The
   XDP_DUMP should be implemented such that it has a limited queue
   size, and simply drops if the queue is full.


Need: smaller eBPF programs
---------------------------

They experience different DDoS attacks.  They don't want to have one
big eBPF program that needs to handle every kind of attack.  This
program would also get too slow once the size increase.

DDoS attacks are usually very specific, and are often stopped by
spotting a very specific pattern in the packet that is constant enough
to identify the bad traffic.  It is key that they can quicky construct
an XDP program matching this very specific pattern, without risking
affecting the stability of other XDP filters.

They also have a need to handle several simultaneous attacks, usually
targeting different destination IP addresses.

.. warning::

   **NEED-RXQ-FEATURE**: This could be solved by using NIC HW filters
   to steer the traffic a specific RX queue, and then allow XDP/eBPF
   programs to run on specific queues.


Ethtool filters for mlx4
------------------------

The HW filter capabilities are highly dependent on the HW, and limited
by what can be expressed by ethtool.

From below documentation, it looks like mlx4 have the filters needed
for this project.

Taken from `mlx4 Linux User Manual`_

.. _mlx4 Linux User Manual:
   http://www.mellanox.com/related-docs/prod_software/Mellanox_EN_for_Linux_User_Manual_v2_0-3_0_0.pdf

Ethtool domain is used to attach an RX ring, specifically its QP to a
specified flow. Please refer to the most recent ethtool manpage for
all the ways to specify a flow.

Examples:

* ethtool -U mlx4p1 flow-type ether dst f4:52:14:7a:58:f1 loc 5 action 2

   All packets that contain the above destination MAC address are to
   be steered into rx-ring 2 (its underlying QP), with
   location/priority 5 (within the ethtool domain)

* ethtool -U mlx4p1 flow-type tcp4 dst-port 22 loc 255 action 2

   All packets that contain the above destination IP address and source
   port are to be steered into rx-ring 2. When destination MAC is not
   given, the user's destination MAC is filled automatically.

* ethtool -u mlx4p1

   Shows all of ethtool’s steering rule

When configuring two rules with the same location/priority, the second
rule will overwrite the first one, so this ethtool interface is
effectively a table.

Inserting Flow Steering rules in the kernel requires support from both
the ethtool in the user space and in kernel (v2.6.28).
