================
Prototype Kernel
================

This documentation is for how to use the `prototype-kernel project`_ itself.

.. _`prototype-kernel project`: https://github.com/netoptimizer/prototype-kernel

XDP and eBPF
============

This github repository also contains samples for XDP and eBPF in the
directory `samples/bpf/`_.  The build process is different.  Simply
run ``make`` in the directory.
Also see :doc:`../networking/XDP/end-user/build_environment`.

.. _samples/bpf/:
   https://github.com/netoptimizer/prototype-kernel/tree/master/kernel/samples/bpf

Prototype Kernel own documentation
==================================

The `prototype-kernel project`_ is meant for compiling kernel modules
outside the normal kernel git tree, but still using the kernels make
system.

The purpose is getting a separate git development tree for developing
and refining your kernel module or Documentation over time, before
pushing it upstream for the Linux Kernel.



Contents:

.. toctree::
   :maxdepth: 2

   build-process


