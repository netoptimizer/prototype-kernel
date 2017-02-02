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

=============
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

BPF_MAP_TYPE_ARRAY
==================

As the name ``BPF_MAP_TYPE_ARRAY`` indicate this can be seen as an
array.  All array elements are pre-allocated and zero initialized at
init time.  Key as an index in array and can only be 4 byte (32-bit).
The constant size is defined by ``max_entries``.  This init-time
constant also implies bpf_map_delete_elem (`array_map_delete_elem`_)
is an invalid operation.

Optimized for fastest possible lookup. The size is constant for the
life of the eBPF program, which allows verifier+JIT to perform a wider
range of optimizations.  E.g. `array_map_lookup_elem()`_ may be
'inlined' by JIT.

Inspecting kernel code look at bpf_map_ops `array_ops`_ in
kernel/bpf/arraymap.c.

Small size gotcha, the ``value_size`` is rounded up to 8 bytes.

Example usage BPF_MAP_TYPE_ARRAY, based on `samples/bpf/sockex1_kern.c`_:

.. code-block:: c

  struct bpf_map_def SEC("maps") my_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = 256,
  };

  u32 index = 42;
  long *value;
  value = bpf_map_lookup_elem(&my_map, &index);
	if (value)
		__sync_fetch_and_add(value, 1);

The lookup (from kernel side) ``bpf_map_lookup_elem()`` returns a pointer
into the array element.  To avoid data races with userspace reading
the value, the API-user must use primitives like ``__sync_fetch_and_add()``
when updating the value in-place.

.. section links

.. _array_ops:
   http://lxr.free-electrons.com/ident?i=array_ops

.. _array_map_delete_elem:
   http://lxr.free-electrons.com/ident?i=array_map_delete_elem

.. _array_map_lookup_elem():
   http://lxr.free-electrons.com/ident?i=array_map_lookup_elem

.. _samples/bpf/sockex1_kern.c:
   https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/samples/bpf/sockex1_kern.c


.. links

.. _bpf(2): http://man7.org/linux/man-pages/man2/bpf.2.html

.. _bpf_map_type:
   http://lxr.free-electrons.com/source/tools/include/uapi/linux/bpf.h?v=4.9#L78

.. _lookup latest:
   http://lxr.free-electrons.com/ident?i=bpf_map_type


.. Notes
   git log kernel/bpf/arraymap.c|tail -33
   git log kernel/bpf/hashtab.c|tail -33
   will give an overview of key hash and array map principles.
