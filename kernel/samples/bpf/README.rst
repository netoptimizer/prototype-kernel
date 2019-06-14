eBPF and XDP samples
====================

**UPDATE**: See `XDP-tutorial`_ if you want to learn eBPF-coding,
and not depend on kernel tree (instead it uses `libbpf`_).

This directory is for prototyping BPF samples, that is intended for
inclusion in the Linux kernel tree containing `eBPF samples`_. If you don't
intend to send this at patches to the Linux kernel, then instead look at
`XDP-tutorial`_ for howto integrate with `libbpf`_.

**WARNING**: This directory contains its own **out-of-date** BPF ELF-loader
(in `bpf_load.c`_).  Instead people should use `libbpf`_.


.. _eBPF samples: https://github.com/torvalds/linux/blob/master/samples/bpf/
.. _XDP-tutorial: https://github.com/xdp-project/xdp-tutorial
.. _bpf_load.c: bpf_load.c
.. _libbpf: https://github.com/libbpf/libbpf
