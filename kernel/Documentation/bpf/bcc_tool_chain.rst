=============================
BCC (BPF Compiler Collection)
=============================

BCC is a toolkit to make eBPF programs easier to write, with
front-ends in Python and Lua.  BCC requires LLVM and clang (in version
3.7.1 or newer) to be available on target, because BCC programs do
runtime compilation of the restricted-C code into eBPF instructions.

BBC includes several useful tools_ and examples_, developed by
recognized performance analyst `Brendan Gregg`_ and covered with a
tutorial_ and slides_.

.. _tools:
   https://github.com/iovisor/bcc/tree/master/tools

.. _examples:
   https://github.com/iovisor/bcc/tree/master/examples

.. _`Brendan Gregg`: http://www.brendangregg.com/

.. _tutorial:
   https://github.com/iovisor/bcc/blob/master/docs/tutorial.md

.. _slides:
   http://www.slideshare.net/brendangregg/linux-bpf-superpowers/43/

The project maintains an overview of `eBPF supported kernels`_ and
what versions got which specific features.  There is also a `BCC
Reference Guide`_.

.. _eBPF supported kernels:
   https://github.com/iovisor/bcc/blob/master/docs/kernel-versions.md

.. _BCC Reference Guide:
   https://github.com/iovisor/bcc/blob/master/docs/reference_guide.md

