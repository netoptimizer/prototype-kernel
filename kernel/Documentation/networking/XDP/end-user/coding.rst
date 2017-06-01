======================
XDP programs with eBPF
======================

Two projects with example code:

 * Using `kernel samples/bpf`_ XDP programs and libbpf_

 * Using BCC_ toolkit

.. _kernel samples/bpf:
   https://github.com/torvalds/linux/blob/master/samples/bpf/

.. _libbpf:
   https://github.com/torvalds/linux/blob/master/tools/lib/bpf/

.. _BCC: https://github.com/iovisor/bcc/blob/master/README.md


Kernel samples/bpf
------------------

The kernel include some examples of XDP-eBPF programs,
see `kernel samples/bpf`_.

There are also some XDP eBPF code examples in the prototype-kernel_
project under `prototype-kernel/kernel/samples/bpf`_.  Simply run
``make`` inside this directory to compile the samples.

.. _prototype-kernel: https://github.com/netoptimizer/prototype-kernel

.. _prototype-kernel/kernel/samples/bpf:
   https://github.com/netoptimizer/prototype-kernel/tree/master/kernel/samples/bpf

Special XDP eBPF cases
----------------------

With XDP the eBPF program gets "direct" access to the raw/unstructured
packet-data.  Thus, eBPF uses some "direct access" instruction for
accessing this data, but for safety this need to pass the in-kernel
validator.

Walking the packet data, requires writing the boundary checks in a
specialized manor.

Like::

  if (data + nh_off > data_end)
		return rc;


