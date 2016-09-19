==============
Overall design
==============

Programmability
===============

XDP is designed for programmability.

Users want programmability as close as possible to the device
hardware, to reap the performance gains, but they also want
portability. The purpose of XDP is making such programs portable
across multiple devices and vendors. (It is even imagined that XDP
programs, should be able to run in userspace, either for simulation
purposes or combined with other raw packet data-plane frameworks like
netmap or DPDK).

It is expected that, some HW vendors, take steps towards offloading
XDP programs into their hardware.  It is fine they compete on this to
sell more hardware. It is no different from producing the fastest
chip. XDP encourage innovation, also for new HW features. But when
extending XDP programs with a new hardware features (e.g. which only a
single vendor supports), then this must be expressed towards the XDP
API as a capability or features (see section `Capabilities
negotiation`_).  This functions as a common capabilities API that
vendors can choose implement (based on customer demand).


Capabilities negotiation
========================

.. Warning:: This interface is missing in the implementation

XDP have hooks and feature dependencies in the device drivers.
Planning for extentability, not all device driver may support all the
future features of XDP, and new feature adaptation in device driver
will occur at different developement rates.

 Thus, there is a need for the device driver to express what XDP
 capabilities or features it provides.

When attaching/loading an XDP program into the kernel, a feature or
capabilities negotiation should be conducted.  This implies an XDP
program need to express what features it want to use.

Loading an XDP program requesting features that the given device
drivers does not support, should simply result in rejecting loading
the program.

.. note:: I'm undecided on whether to have an query interface?
   Because users can just use the regular load-interface to probe for
   supported options.  The down-side of probing is the issues SElinux
   runs into, of false-alarms, when glibc tries to probe after
   capabilities.


Implementation issue
--------------------

The current implementation is missing this interface.  Worse the two
actions :ref:`XDP_DROP` and :ref:`XDP_TX` should have been express as
two different capabilities, because XDP_TX requires more changes to
the device driver than a simple drop like XDP_DROP.

One can (easily) imagine that an older driver only want to implement
the XDP_DROP facility.  The reason is that XDP_TX would requires
changing too much driver code, which is a concern for an old stable
and time-proven driver.

