=========
eBPF maps
=========

Using eBPF maps is a method to keep state between invocations of the
eBPF program, and allows sharing data between eBPF kernel programs,
and also between kernel and user-space applications.

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

Creating a map
==============

A map is created based on a request from userspace, via the `bpf`_
syscall (`bpf_cmd`_ BPF_MAP_CREATE), which returns a new file descriptor
that refers to the map. These are the setup arguments to use when
creating a map.

.. code-block:: c

  struct { /* anonymous struct used by BPF_MAP_CREATE command */
         __u32   map_type;       /* one of enum bpf_map_type */
         __u32   key_size;       /* size of key in bytes */
         __u32   value_size;     /* size of value in bytes */
         __u32   max_entries;    /* max number of entries in a map */
         __u32   map_flags;      /* prealloc or not */
  };

For programs under samples/bpf/ the ``load_bpf_file()`` call (from
`samples/bpf/bpf_load`_) takes care of parsing elf file compiled by
LLVM, pickups 'maps' section and creates maps via BPF syscall.  This is
done by defining a ``struct bpf_map_def`` with an elf section
__attribute__ ``SEC("maps")``, in the xxx_kern.c file.  The maps file
descriptor is available in the userspace xxx_user.c file, via global
array variable ``map_fd[]``, and the array map index corresponds to the
order the maps sections were defined in elf file of xxx_kern.c file.

.. code-block:: c

  struct bpf_map_def {
	unsigned int type;
	unsigned int key_size;
	unsigned int value_size;
	unsigned int max_entries;
	unsigned int map_flags;
  };

  struct bpf_map_def SEC("maps") my_map = {
	.type        = BPF_MAP_TYPE_XXX,
	.key_size    = sizeof(u32),
	.value_size  = sizeof(u64),
	.max_entries = 42,
	.map_flags   = 0
  };

.. _samples/bpf/bpf_load:
   https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/samples/bpf/bpf_load.c


Interacting with maps
=====================

Interacting with an eBPF map from **userspace**, happens through the
`bpf`_ syscall and a file descriptor.  The kernel
`tools/lib/bpf/bpf.h`_ defines some ``bpf_map_*()`` helper functions
for wrapping the `bpf_cmd`_ related to manipulating the map elements.

.. code-block:: c

  enum bpf_cmd {
	[...]
	BPF_MAP_LOOKUP_ELEM,
	BPF_MAP_UPDATE_ELEM,
	BPF_MAP_DELETE_ELEM,
	BPF_MAP_GET_NEXT_KEY,
	[...]
  };
  /* Corresponding helper functions */
  int bpf_map_lookup_elem(int fd, void *key, void *value);
  int bpf_map_update_elem(int fd, void *key, void *value, __u64 flags);
  int bpf_map_delete_elem(int fd, void *key);
  int bpf_map_get_next_key(int fd, void *key, void *next_key);

Notice from userspace, there is no call to atomically increment or
decrement the value 'in-place'. The bpf_map_update_elem() call will
overwrite the existing value.  The flags argument allows
bpf_map_update_elem() to define semantics on whether the element exists:

.. code-block:: c

  /* File: include/uapi/linux/bpf.h */
  /* flags for BPF_MAP_UPDATE_ELEM command */
  #define BPF_ANY	0 /* create new element or update existing */
  #define BPF_NOEXIST	1 /* create new element only if it didn't exist */
  #define BPF_EXIST	2 /* only update existing element */

The eBPF-program running "kernel-side" has almost the same primitives
(lookup/update/delete) for interacting with the map, but it interacts
more directly with the map data structures. For example the call
``bpf_map_lookup_elem()`` returns a direct pointer to the 'value'
memory-element inside the kernel (while userspace gets a copy).  This
allows the eBPF-program to atomically increment or decrement the value
'in-place', by using appropiate compiler primitives like
``__sync_fetch_and_add()``, which is understood by LLVM when
generating eBPF instructions.

On the kernel side, implementing a map type requires defining some
function (pointers) via `struct bpf_map_ops`_.  And eBPF programs have
access to ``map_lookup_elem``, ``map_update_elem`` and
``map_delete_elem``, which get invoked from eBPF via bpf-helpers in
`kernel/bpf/helpers.c`_.

.. section links

.. _tools/lib/bpf/bpf.h:
   https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/tools/lib/bpf/bpf.h

.. _bpf_cmd: http://lxr.free-electrons.com/ident?i=bpf_cmd

.. _struct bpf_map_ops: http://lxr.free-electrons.com/ident?i=bpf_map_ops

.. _kernel/bpf/helpers.c:
   https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/kernel/bpf/helpers.c


=============
Types of maps
=============

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


.. links

.. _bpf(2): http://man7.org/linux/man-pages/man2/bpf.2.html

.. _bpf: http://man7.org/linux/man-pages/man2/bpf.2.html

.. _bpf_map_type:
   http://lxr.free-electrons.com/source/tools/include/uapi/linux/bpf.h?v=4.9#L78

.. _lookup latest:
   http://lxr.free-electrons.com/ident?i=bpf_map_type


.. Notes
   git log kernel/bpf/arraymap.c|tail -33
   git log kernel/bpf/hashtab.c|tail -33
   will give an overview of key hash and array map principles.
