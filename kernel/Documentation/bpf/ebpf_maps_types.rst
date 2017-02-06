==================
Types of eBPF maps
==================

There are diffent types of maps available.  The type definitions
needed when creating the maps are defined in include/uapi/linux/bpf.h
as ``enum bpf_map_type``.

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

.. section links

.. _lookup latest:
   http://lxr.free-electrons.com/ident?i=bpf_map_type

.. _bpf_map_type:
   http://lxr.free-electrons.com/source/tools/include/uapi/linux/bpf.h?v=4.9#L78

BPF_MAP_TYPE_ARRAY
==================

As the name ``BPF_MAP_TYPE_ARRAY`` indicates, this can be seen as an
array.  All array elements are pre-allocated and zero initialized at
init time.  Key is an index in array and can only be 4 bytes (32-bit).
The constant size is defined by ``max_entries``.  This init-time
constant also implies bpf_map_delete_elem (`array_map_delete_elem`_)
is an invalid operation.

Optimized for fastest possible lookup. The size is constant for the
life of the eBPF program, which allows verifier+JIT to perform a wider
range of optimizations.  E.g. `array_map_lookup_elem()`_ may be
'inlined' by JIT.

To inspect kernel code look at bpf_map_ops `array_ops`_ in
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


.. Notes
   git log kernel/bpf/arraymap.c|tail -33
   git log kernel/bpf/hashtab.c|tail -33
   will give an overview of key hash and array map principles.
