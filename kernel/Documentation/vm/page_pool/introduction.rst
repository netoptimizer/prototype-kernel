============
Introduction
============

The page_pool is a generic API for drivers that have a need for a pool
of recycling pages used for streaming DMA.


Motivation
==========

The page_pool is primarily motivated by two things (1) performance
and (2) changing the memory model for drivers.

Drivers have developed performance workarounds when the speed of the
page allocator and the DMA APIs became too slow for their HW
needs. The page pool solves them on a general level providing
performance gains and benefits that local driver recycling hacks
cannot realize.

A fundamental property is that pages are returned to the page_pool.
This property allow a certain class of optimizations, which is to move
setup and tear-down operations out of the fast-path, sometimes known as
constructor/destruction operations.  DMA map/unmap is one example of
operations this applies to.  Certain page alloc/free validations can
also be avoided in the fast-path.  Another example could be
pre-mapping pages into userspace, and clearing them (memset-zero)
outside the fast-path.

Memory model
============

Once drivers are converted to using page_pool API, then it will become
easier change the underlying memory model backing the driver with
pages (without changing the driver).

One prime use-case is NIC zero-copy RX into userspace.  As DaveM
describes in his `Google-plus post`_, the mapping and unmapping
operations in the address space of the process has a cost that cancels
out most of the gains of such zero-copy schemes.

This mapping cost can solved the same way as the keeping DMA mapped
trick.  By keeping the pages VM-mapped to userspace.  This is a layer
that can be added later to the page_pool.  It will likely be
beneficial to also consider using huge-pages (as backing) to reduce
the TLB-stress.

.. _Google-plus post:
   https://plus.google.com/+DavidMiller/posts/EUDiGoXD6Xv

Advantages
==========

Advantages of a recycling page pool as bullet points:

1) Faster than going through page-allocator.  Given a specialized
   allocator require less checks, and can piggyback on drivers
   resource protection (for alloc-side).

2) DMA IOMMU mapping cost is removed by keeping pages mapped.

3) Makes DMA pages writable by predictable DMA unmap point.

4) OOM protection at device level, as having a feedback-loop knows
   number of outstanding pages.

5) Flexible memory model allowing zero-copy RX, solving memory early
   demux (does depend on HW filters into RX queues)

6) Less fragmentation of the page buddy algorithm, when driver
   maintains a steady-state working-set.
