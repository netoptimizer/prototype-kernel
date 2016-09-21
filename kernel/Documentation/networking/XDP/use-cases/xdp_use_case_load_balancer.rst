=======================
Use-case: Load Balancer
=======================

The load-balancer use-case originated from Facebook, as they have a
need to load-balance their traffic. They obviously already load
balance, but are looking for a faster and more scalable approach.

Facebook currently use the IPVS_ (IP Virtual Server) load balancer
software, which is part of the standard Linux Kernel (since kernel
2.6.10).  They even wrote a `Python module`_ for configuring IPVS,
which is a pure-python replacement for ipvsadm_ (`ipvsadm git`_ tree).

.. _IPVS: http://www.linuxvirtualserver.org/

.. _Python module: https://github.com/facebook/gnlpy/blob/master/ipvs.py

.. _ipvsadm:     https://kernel.org/pub/linux/utils/kernel/ipvsadm/
.. _ipvsadm git: https://git.kernel.org/cgit/utils/kernel/ipvsadm/ipvsadm.git/


Traditional load balancer
=========================

Traditionally a service load balancer (like IPVS) has more NICs
(Network Interface Cards), and forwards traffic to the back-end
servers (called "real server" for IPVS).

The current XDP implementation (**XDP_TX** in kernel 4.8) can only
forward packets back out the same NIC they arrived on.  This makes XDP
unsuited for implementing a traditional multi-NIC load balancer.

A traditional load balancer easily becomes a single point of failure.
Thus, multiple load balancers are usually deployed, in a
`High Availability`_ (HA) cluster.  In order to make load balancer
failover transparent to client applications, the load balancer(s) need
to synchronize their state (E.g. via `IPVS sync protocol`_ sending UDP
multicast, preferable send on a separate network/NIC).

.. _High Availability:
   https://en.wikipedia.org/wiki/High-availability_cluster#Node_configurations

.. _IPVS sync protocol:
   http://www.linuxvirtualserver.org/docs/sync.html

Untraditional XDP load balancer
===============================

Imagine implementing a load balancer without any dedicated servers for
load balancing, 100% scalable and with no single point of failure.

 Be the load balancer yourself!

The main idea is, allow XDP to *be* the load-balancing layer.  Running
the XDP load balancer software directly on the "back-end" server, with
no dedicated central server.

It corresponds to running IPVS on the backend ("real servers"), which
is possible, and some `IPVS examples`_ are available (e.g `Ultra
Monkey`_). But that is generally not recommended (in high load
situations), because it increases the load on the application server
itself, which leaves less CPU time for serving requests.

.. _IPVS examples: http://kb.linuxvirtualserver.org/wiki/Examples
.. _Ultra Monkey: http://www.ultramonkey.org/2.0.1/topologies/sl-ha-lb-eg.html


 Why is this a good idea for XDP then?

XDP has a speed advantage.  The XDP load balance forwarding decision
happens **very** early, before the OS has spent/invested too many
cycles on the packet.  This means the XDP load balancing functionality
should not increase the load on the server significantly.  Thus, it
should okay to run the service and LB on the same server.  One can
even imagine having a feedback loop into the LB-program decision,
based on whether the service is struggling to keep up.

Who will balance the incoming traffic?
--------------------------------------

The router can distribute/spread incoming packets across the servers
in the cluster, e.g. via using Equal-Cost Multi-Path routing (ECMP)
like Google's Maglev_ solutions does. Google then use some consistent
hashing techniques to forward packets to the correct service backend
servers.

.. _Maglev:
   https://cloudplatform.googleblog.com/2016/03/Google-shares-software-network-load-balancer-design-powering-GCP-networking.html

Serving correct client
----------------------

The challenging part, in such a distributed system of load balancers,
is to coordinate packets getting forwarded to the (correct) server
responsible for serving the client.

Google uses a consistent hashing scheme, but other solutions are also
possible.

Hardware setup
--------------

As mentioned under :doc:`../disclaimer`, it is very important to
understand hardware environment this kind of setup works within.

When using the same network segment for the load balancing traffic
(due to XDP_TX limitations), extra care need to be taken when
dimensioning the network capacity.

One can create a cluster of servers, all connected to the same
10Gbit/s switch, and the switch has the same 10Gbit/s uplink capacity
limitation. The 10Gbit/s capacity is bidirectional, meaning both RX
and TX have 10Gbit/s.  No (incoming) network overload situation can
occur, because the uplink can only forward with 10G, and LB server can
RX with 10G and TX with 10G to another "service-server", happening
over the Ethernet switch fabric, thus RX capacity of the
"service-server" is still 10G.  Sending traffic back to the uplink
happens via "direct-return" from the "service-server", still have 10G
capacity left in the Ethernet switch fabric.  Thus, with a proper HW
setup the XDP_TX limitation can be dealt with.


Need: RX HW hash
================

.. warning::

   **FEATURE**:
   provide NIC RX HW hash has as meta-data input to XDP program.

A scheme to determine which **flows** a given server is responsible
for serving can benefit from getting the NIC RX hardware hash as
input.

The XDP load balancing decision can be made faster, if it does not
have to read+parse the packet contents before making a route decision.
This is possible if basing the decision on the RX hardware hash,
available via the RX descriptor.

.. note::
   Requires: setting up the same NIC HW hash on all servers in the cluster.


