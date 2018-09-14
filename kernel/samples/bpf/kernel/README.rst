
This directory contains files copied from kernel rootdir.

Specifically: kernel/include/uapi/linux/bpf.h

This is specifically needed for compiling xxx_kern.o files, as this
git-repo is (likely) using BPF features, that is not avail in all
distro kernels, and the avail distros kernel-headers package.
