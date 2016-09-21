==========
Disclaimer
==========

XDP is not for every use-case.

Important to understand
=======================

It is important to understand that the XDP speed gains comes at a cost
of loss of generalization and fairness.

XDP does not provide fairness. There is no buffering (qdisc) layer to
absorb traffic bursts when the TX device is too slow, packets will
simply be dropped.  Don't use XDP in situations where the RX device
is faster than the TX device, as there is no back-pressure to save
the packet from being dropped.  There is no qdisc layer or BQL (Byte
Queue Limit) to save your from introducing massive bufferbloat.

Using XDP is about specialization. Crafting a solution towards a very
specialized purpose, that will require selecting and dimensioning the
appropriate hardware. Using XDP requires understanding the dangers and
pitfalls, that come from bypassing large parts of the kernel network
stack code base, which is there for good reasons.

That said, XDP can be the right solution for some use-cases, and can
yield huge (orders of magnitude) performance improvements, by allowing
this kind of specialization.
