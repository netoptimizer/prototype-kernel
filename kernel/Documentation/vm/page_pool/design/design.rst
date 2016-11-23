==============
Overall design
==============

The page_pool is designed for performance, and for creating a flexible
and common memory model for drivers.  Most drivers are based on
allocating pages for their DMA receive-rings.  Thus, it is a design
goal to make it easy to convert these drivers.

Using page_pool provides an immediate performance improvement, and
opens up for the longer term goal of zero-copy receive into userspace.

.. _`optimization principle`:

Optimization principle
======================

A fundamental property is that pages **must** be recycled back into
the page_pool (when the last user of the page is done).

Recycling pages allow a certain class of optimizations, which is to
move setup and tear-down operations out of the fast-path, sometimes
known as constructor/destruction operations.  DMA map/unmap is one
example of operations this applies to.  Certain page alloc/free
validations can also be avoided in the fast-path.  Another example
could be pre-mapping pages into userspace, and clearing them
(memset-zero) outside the fast-path.

Memory Model
============

The page_pool should be as transparent as possible.  This mean that
page coming out of a page_pool, should be considered a normal page
(with as few restrictions as possible). This implies a more tight
integration with the existing page allocator APIs.  (This should also
make it easier to compile out.)

Driver are still allowed to split-up page and manipulate refcnt.

DMA map+unmap
-------------

The page_pool API takes over the DMA map+unmap operations, based on
the :ref:`optimization principle`.  The cost of DMA map+unmap depend
on the hardware architecture, and whether features like DMA IOMMU have
been enabled or not. Thus, the benefit is harder to quantify.

Taking over DMA map+unmap operations, also implies the page_pool
cannot be a complete drop-in replacement for the page allocator.

Common driver layer
-------------------

It is important to have a common layer drivers use for allocating and
freeing pages.

The time budget for XDP direct forwarding between interfaces (based on
different drivers) cannot rely on pages going through the page
allocator (as the base cost is higher than the budget).  The page_pool
recycle technique is needed here, across drivers.

Drivers also need a flexible memory model for supporting different
use-cases, which have trade-offs for different usage scenarios. And
the page_pool need to support these scenarios.

Network scenarios: XDP requires drivers to change the memory model to
one packet per page.  When no XDP program is loaded, the driver can
instead choose to conserve memory by splitting up the page to share is
for multiple RX packets.  When mapping pages to userspace, one packet
per page is likely also needed. For more details on networking see
:doc:`memory_model_nic`.


Drivers old memory model
------------------------

Drivers (not using the page_pool) allocate pages for DMA operations
directly from the page allocator.  Pages are freed into the page
allocator once their refcnt reach zero.  Thus, pages are cycles
through the page allocator.  This actually comes at a fairly high
cost, measurable by the `page_bench micro-benchmarks`_ and graphs in
`MM-summit2016 presentation`_.

.. _`page_bench micro-benchmarks`:
   https://github.com/netoptimizer/prototype-kernel/tree/master/kernel/mm/bench

.. _`MM-summit2016 presentation`:
   http://people.netfilter.org/hawk/presentations/MM-summit2016/generic_page_pool_mm_summit2016.pdf


Driver work-arounds
-------------------

.. Warning:: Document not complete

Allocation side
===============

.. shouldn't piggybacking be an implementation detail?

Piggyback on drivers RX protection for page allocations.
