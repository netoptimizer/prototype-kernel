============
Introduction
============

What is XDP?
============

XDP or eXpress Data Path provides a high performance, programmable
network data path in the Linux kernel. XDP provides bare metal packet
processing at the lowest point in the software stack.  Much of the
huge speed gain comes from processing RX packet-pages directly out of
drivers RX ring queue, before any allocations of meta-data structures
like SKB's occurs.

The IO Visor Project have an `introduction to XDP`_.

.. _introduction to XDP: https://www.iovisor.org/technology/xdp


.. include:: presentations.rst
