=============
Userspace API
=============

.. Warning::

   The userspace API specification should have to be defined properly
   before code was accepted upstream.  Concerns have been raise about
   the current API upstream.  Users should expect this first API
   attempt will need adjustments. This cannot be considered a stable
   API yet.

   Most importantly is the missing capabilities negotiation,
   see :ref:`ref_prog_negotiation`.


Planning for API extension
==========================

The kernel documentation about `syscalls`_ have some good
considerations when designing an extendable API, and `Michael Kerrisk`_
also have some entertaining `API examples`_.

.. _syscalls:
   https://github.com/torvalds/linux/blob/master/Documentation/adding-syscalls.txt

.. _API examples: http://man7.org/conf/index.html

.. _Michael Kerrisk: http://man7.org/

Struct xdp_prog
---------------

Currently (4.8-rc6) the XDP program is simply a bpf_prog pointer.
While it is good for simplicity, it is limiting extendability for
upcoming features.

Introducing a new ``struct xdp_prog``, that can carry information
related to the XDP program.  Notice this approach does not affect
performance (tested and benchmarked), because the extra dereference
for the eBPF program only happens once per 64 packets in the poll
function.

The features that need this is:

* Multi-port TX:
  Need to know own port index and port lookup table.

* XDP program per RX queue:
  Need setup info about program type, global or specific, due to
  replace semantics.

* Capabilities negotiation:
  Need to store information about features program want to use,
  in-order to validate this.

.. TODO:: How kernel devel works: This new ``struct xdp_prog``
   features cannot go into the kernel before one of the three users of
   the struct is also implemented. (Note, Jesper have implemented this
   struct change and have even benchmarked that it does not hurt
   performance).


.. _`Troubleshooting and Monitoring`:

Troubleshooting and Monitoring
==============================

Users need the ability to both monitor and troubleshoot an XDP
program. Partigular in case of error events like :ref:`XDP_ABORTED`,
and in case a XDP programs starts to return invalid and unsupported
action code (caught by the :ref:`action fall-through`).

.. Warning::

   The current (4.8-rc6) implementation is not optimal in this area.
   In case of the :ref:`action fall-through` packets is dropped and a
   warning is generated **only once** about the invalid XDP program
   action code, by calling: bpf_warn_invalid_xdp_action(action_code);

The facilities and behavior need to be improved in this area.

Two options are on the table currently:

* Counters.

  Simply add counters to track these events.  This allow admins and
  monitor tools to catch and count these events.  This does requires
  standardizing these counters to help monitor tools.

* Tracepoints.

  Another option is adding tracepoint to these situations.  It is much
  more flexible than counters.  The downside is that these error
  events might never be caught, if the tracepoint isn't active.
