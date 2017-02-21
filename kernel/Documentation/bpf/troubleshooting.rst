====================
Troubleshooting eBPF
====================

This document should help end-users with troubleshooting their eBPF
programs.  With a primary focus on programs under kernels samples/bpf.

Memory ulimits
==============

The eBPF maps uses locked memory, which is default very low.
Your program likely need to increase resource limit ``RLIMIT_MEMLOCK``
see system call `setrlimit(2)`_.

The ``bpf_create_map`` call will return errno EPERM (Operation not
permitted) when the RLIMIT_MEMLOCK memory size limit is exceeded.


.. _setrlimit(2): http://man7.org/linux/man-pages/man2/setrlimit.2.html

