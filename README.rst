Prototyping kernel development
==============================
:Authors: Jesper Dangaard Brouer <netoptimizer@brouer.com>

This project and GitHub_ repository is meant for speeding up Linux
Kernel development work, this also includes Documentation_.  The
directory layout tries to keep close to the Kernel directory layout.
This helps when/if upstreaming the work.

This prototype-kernel was primarily meant for prototyping kernel
modules (see blogpost_).

XDP eBPF samples
================

**UPDATE**: See `XDP-tutorial`_ if you want to learn eBPF-coding,
and not depend on kernel tree.

The Linux kernel tree also contains some `eBPF samples`_ which this
github repo is also shadowing for easier prototyping, see directory
`samples/bpf/`_.

This directory `samples/bpf/`_ maintains a different Makefile (than
depending on the kernels) and maintains a copy of some bpf-header
files to ease compiling outside the kernel source tree.

Simply run 'make' in that directory to build the bpf samples.


Documentation
=============

This also covers Kernel Documentation_ which is being auto-generated_
by `Read The Docs`_ (based on `reStructuredText`_ files and `Sphinx`_
to generate pretty documentation).

.. _GitHub: https://github.com/netoptimizer/prototype-kernel
.. _Documentation: kernel/Documentation/
.. _Read The Docs: https://prototype-kernel.readthedocs.io
.. _auto-generated: https://prototype-kernel.readthedocs.io
.. _Sphinx: http://www.sphinx-doc.org/
.. _reStructuredText: http://docutils.sourceforge.net/docs/user/rst/quickref.html
.. _blogpost: http://netoptimizer.blogspot.dk/2014/11/announce-github-repo-prototype-kernel.html
.. _eBPF samples: https://github.com/torvalds/linux/blob/master/samples/bpf/
.. _samples/bpf/: kernel/samples/bpf/
.. _XDP-tutorial: https://github.com/xdp-project/xdp-tutorial
