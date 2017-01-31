=========
eBPF maps
=========

Using eBPF maps is a method to keep state between invocations of the
eBPF program, and also share state between different eBPF programs and
userspace.

Key/value store with arbitrary structure (from man-page `bpf(2)`_):

 eBPF maps are a generic data structure for storage of different data
 types.  Data types are generally treated as binary blobs, so a user
 just specifies the size of the key and the size of the value at
 map-creation time.  In other words, a key/value for a given map can
 have an arbitrary structure.

The map handles are file descriptors, and multiple maps can be created
and accessed by multiple programs (from man-page `bpf(2)`_):

 A user process can create multiple maps (with key/value-pairs being
 opaque bytes of data) and access them via file descriptors.
 Different eBPF programs can access the same maps in parallel.  It's
 up to the user process and eBPF program to decide what they store
 inside maps.

Types of maps
=============

There are diffent types of maps available.  The defines needed when
creating the maps are defined in include/uapi/linux/bpf.h as
``enum bpf_map_type``.

Example of `bpf_map_type`_ from kernel 4.9, but remember to `lookup
latest`_ available maps in the source code ::

 enum bpf_map_type {
	BPF_MAP_TYPE_UNSPEC,
	BPF_MAP_TYPE_HASH,
	BPF_MAP_TYPE_ARRAY,
	BPF_MAP_TYPE_PROG_ARRAY,
	BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	BPF_MAP_TYPE_PERCPU_HASH,
	BPF_MAP_TYPE_PERCPU_ARRAY,
	BPF_MAP_TYPE_STACK_TRACE,
	BPF_MAP_TYPE_CGROUP_ARRAY,
	BPF_MAP_TYPE_LRU_HASH,
	BPF_MAP_TYPE_LRU_PERCPU_HASH,
 };


.. TODO:: documentation how I interact with these maps

.. links

.. _bpf(2): http://man7.org/linux/man-pages/man2/bpf.2.html

.. _bpf_map_type:
   http://lxr.free-electrons.com/source/tools/include/uapi/linux/bpf.h?v=4.9#L78

.. _lookup latest:
   http://lxr.free-electrons.com/ident?i=bpf_map_type
