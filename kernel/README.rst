Prototype Kernel
================

The prototype-kernel project is meant for compiling kernel modules
outside the normal kernel git tree, but still using the kernels make
system.

.. Note::

   It is a pre-requisite that you have a developement kernel tree
   available for compiling against (or install your distributions
   kernel-devel package).

The purpose is getting a separate git development tree for developing
and refining your kernel module over time, before pushing it upstream
for the Linux Kernel.

See the `build process`_ document.

.. _build process: Documentation/prototype-kernel/build-process.rst
