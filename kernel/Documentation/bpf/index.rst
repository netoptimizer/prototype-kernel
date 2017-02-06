======================================
eBPF - extended Berkeley Packet Filter
======================================

Introduction
============

The Berkeley Packet Filter (BPF) started (`article 1992`_) as a
special-purpose virtual machine (register based filter evaluator) for
filtering network packets, best known for the its in tcpdump. It is
documented in the kernel tree, in the first part of:
`Documentation/networking/filter.txt`_

The extended BPF (eBPF) variant has become a universal in-kernel
virtual machine, that has hooks all over the kernel.  The eBPF
instruction set is quite different, see description in section "BPF
kernel internals" of `Documentation/networking/filter.txt`_ or look at
this `presentation by Alexei`_.

Areas using eBPF:
 * :doc:`../networking/XDP/index`
 * `Traffic control`_
 * Sockets
 * Firewalling (``xt_bpf`` module)
 * Tracing
 * Tracepoints
 * kprobe (dynamic tracing of a kernel function call)
 * cgroups

Documentation
=============

The primary user documentation for extended BPF is in the man-page for
the `bpf(2)`_ syscall.

This documentation is focused on the kernel tree's `samples/bpf/`_ and
`tools/lib/bpf/`_.  It is worth mentioning that other projects exist,
like :doc:`bcc_tool_chain`, that has a slightly different user-facing
syntax, but is interfacing with the same kernel facilities as those
covered by this documentation.

.. Comments / Notes:
   Sections:
   * Build environment
   * Coding
   * Maps

.. toctree::
   :maxdepth: 1

   ebpf_maps
   ebpf_maps_types
   bcc_tool_chain

.. links:

.. _article 1992: http://www.tcpdump.org/papers/bpf-usenix93.pdf

.. _bpf(2): http://man7.org/linux/man-pages/man2/bpf.2.html

.. _Documentation/networking/filter.txt:
   https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/Documentation/networking/filter.txt

.. _presentation by Alexei:
   http://www.slideshare.net/AlexeiStarovoitov/bpf-inkernel-virtual-machine

.. _samples/bpf/:
   https://github.com/torvalds/linux/blob/master/samples/bpf/

.. _tools/lib/bpf/:
   https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/tools/lib/bpf/

.. _Traffic control: http://man7.org/linux/man-pages/man8/tc-bpf.8.html
