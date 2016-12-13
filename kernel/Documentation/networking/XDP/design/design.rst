==============
Overall design
==============

Requirements defined in document :doc:`requirements`.

Programmability
===============

XDP is designed for programmability.

Users want programmability as close as possible to the device
hardware, to reap the performance gains, but they also want
portability.  The purpose of XDP is making such programs portable
across multiple devices and vendors.

(It is even imagined that XDP programs should be able to run in
user space, either for simulation purposes or combined with other raw
packet data-plane frameworks like netmap or DPDK).

It is expected that some HW vendors will take steps towards offloading
XDP programs into their hardware.  It is fine if they compete on this
to sell more hardware.  It is no different from producing the fastest
chip.  XDP also encourages innovation for new HW features, but when
extending XDP programs with a new hardware feature (e.g. which only a
single vendor supports), this must be expressed within the XDP API as
a capability or feature (see section `Capabilities negotiation`_).
This functions as a common capabilities API from which vendors can
choose what to implement (based on customer demand).

.. _ref_prog_negotiation:

Capabilities negotiation
========================

.. Warning:: This interface is missing in the implementation

XDP has hooks and feature dependencies in the device drivers.
Planning for extendability, not all device drivers will necessarily
support all of the future features of XDP, and new feature adoption
in device drivers will occur at different development rates.

Thus, there is a need for the device driver to express what XDP
capabilities or features it provides.

When attaching/loading an XDP program into the kernel, a feature or
capabilities negotiation should be conducted.  This implies that an
XDP program needs to express what features it wants to use.

If an XDP program being loaded requests features that the given device
driver does not support, the program load should simply be rejected.

.. note:: I'm undecided on whether to have an query interface, because
   users could just use the regular load-interface to probe for
   supported options.  The downside of probing is the issues SElinux
   runs into, of false alarms, when glibc tries to probe for
   capabilities.


Implementation issue
--------------------

The current implementation is missing this interface.  Worse, the two
actions :ref:`XDP_DROP` and :ref:`XDP_TX` should have been expressed
as two different capabilities, because XDP_TX requires more changes to
the device driver than a simple drop like XDP_DROP.

One can (easily) imagine that an older driver only wants to implement
the XDP_DROP facility.  The reason is that XDP_TX would require
changing too much driver code, which is a concern for an old, stable
and time-proven driver.

Data plane split
================

.. See: Packet I/O methods by Ben Pfaff

