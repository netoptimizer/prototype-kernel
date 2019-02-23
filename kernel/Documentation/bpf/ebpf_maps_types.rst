==================
Types of eBPF maps
==================

This document describes the different types of eBPF maps available,
and goes into details about the individual map types.  The purpose is
to help choose the right type based on the individual use-case.
Creating and interacting with maps are described in another document
here: :doc:`ebpf_maps`.

The different types of maps available, are defined by ``enum
bpf_map_type`` in include/uapi/linux/bpf.h.  These type definitions
"names" are needed when creating the map. Example of ``bpf_map_type``,
but remember to `lookup latest`_ available maps in the source code.

.. code-block:: c

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
   http://lingrok.org/search?project=linux-net-next&q=bpf_map_type

Implementation details
======================

In-order to understand and follow the descriptions of the different
map types, it is useful for the reader to understand how a map type is
implemented by the kernel.

On the kernel side, implementing a map type requires defining some
function call (pointers) via `struct bpf_map_ops`_.  The eBPF programs
(and userspace) have access to the functions calls
``map_lookup_elem``, ``map_update_elem`` and ``map_delete_elem``,
which get invoked from eBPF via bpf-helpers in `kernel/bpf/helpers.c`_,
or via userspace the bpf syscall (as described in :doc:`ebpf_maps`).

:ref:`Creating a map` requires supplying the following configuration
attributes: map_type, key_size, value_size, max_entries and map_flags.

.. section links

.. _struct bpf_map_ops: http://lxr.free-electrons.com/ident?i=bpf_map_ops

.. _kernel/bpf/helpers.c:
   https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/kernel/bpf/helpers.c


BPF_MAP_TYPE_ARRAY
==================

Implementation defined in `kernel/bpf/arraymap.c`_ via struct
bpf_map_ops `array_ops`_.

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

.. _kernel/bpf/arraymap.c:
   http://lxr.free-electrons.com/source/kernel/bpf/arraymap.c

.. _array_ops:
   http://lxr.free-electrons.com/ident?i=array_ops

.. _array_map_delete_elem:
   http://lxr.free-electrons.com/ident?i=array_map_delete_elem

.. _array_map_lookup_elem():
   http://lxr.free-electrons.com/ident?i=array_map_lookup_elem

.. _samples/bpf/sockex1_kern.c:
   https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/samples/bpf/sockex1_kern.c
