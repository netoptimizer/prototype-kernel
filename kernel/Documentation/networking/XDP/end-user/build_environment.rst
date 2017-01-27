==========================
XDP/eBPF build environment
==========================

Tool chain
==========

The XDP program running (in the driver hook point) is an eBPF program
(see :doc:`../../../bpf/index`). Unless you want to write eBPF
machine-code like instruction by hand, you likely want to install some
front-ends, that allow you to write some pseudo C-code.

Tools for compiling `kernel bpf samples`_ requires having installed:
 * clang >= version 3.4.0
 * llvm >= version 3.7.1

Note that LLVM's tool 'llc' must support target 'bpf', list version
and supported targets with command: ``llc --version``.

.. _kernel bpf samples:
   https://github.com/torvalds/linux/blob/master/samples/bpf/README.rst

There is also toolkit called BCC_ (BPF Compiler Collection) that makes
eBPF programs easier to write, and front-ends in Python and lua.  But
it also depend on LLVM.

.. _BCC: https://github.com/iovisor/bcc/blob/master/README.md

Linux distros
=============

Fedora 25
---------

Since Fedora 25, the package BCC_ is included with the distribution,
and LLVM+clang in the correct versions.

Install commands for Fedora 25::

 dnf install llvm llvm-libs llvm-doc clang clang-libs
 dnf install bcc bcc-tools bcc-doc --enablerepo=updates-testing
 dnf install kernel-devel
 dnf install python3-pyroute2

.. Note:: As of this writing (2017-01-18) BCC for F25 is still in the
          updates-testing repository.

