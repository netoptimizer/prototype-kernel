==============
Use-case: DDoS
==============

DDoS protection was the primary use-case XDP was born-out-of.
CloudFlare_ presented their `DDoS use-case`_ at the `Network
Performance BoF`_ at NetDev 1.1, which convinced many Kernel developer
that this was something that needed to be solved.


.. _Network Performance BoF:
   http://people.netfilter.org/hawk/presentations/NetDev1.1_2016/links.html

.. _CloudFlare:
   https://blog.cloudflare.com/single-rx-queue-kernel-bypass-with-netmap/

.. _DDoS use-case:
   https://blog.cloudflare.com/partial-kernel-bypass-merged-netmap/

End-host protection
===================

When a server is under DoS (Denial-of-Service) attack, the attacker is
trying to use as many resource on the server as possible, in-order to
not leave processing time to service the legitimate users.

Due to XDP running so early in the software stack, almost no
processing cost is associated with dropping a packet. This makes it a
viable option to load a XDP program directly on the server, as
filtering out bad/attacker traffic (this early) frees up processing
resources.

As XDP is still part of the Linux network stack, packets that "pass"
the XDP filter, still have all features for further filtering that the
kernel normally provide.  It works in concert with the regular network
stack.



