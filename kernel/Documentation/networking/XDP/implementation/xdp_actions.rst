.. (comment) The references above each section is used for doing
   cross-referencing from arbitrary locations in any document.
   I know the markup looks a little wierd.
   Used like :ref:`XDP_DROP`
   Online: http://www.sphinx-doc.org/en/stable/markup/inline.html#role-ref

===========
XDP actions
===========

.. TODO:: Describe the action currently implemented here. For now this
          was primairly created to allow referencing these actions
          with link

.. _XDP_PASS:

XDP_PASS
========

XDP_PASS means the XDP program choose to pass the packet to the normal
network stack for processing.  Do notice that the XDP program is
allowed to have modified the packet-data.


.. _XDP_DROP:

XDP_DROP
========

XDP_DROP is perhaps the simplest and fastest action.  It simply
instructs the driver to drop the packet.  Given this action happens at
the earliest RX stage in the driver, dropping a packet simply implies
recycling it back-into the RX ring queue it just "arrived" on.  There
is simply no faster way to drop a packet.  This comes close to a
driver hardware test feature.


.. _XDP_TX:

XDP_TX
======


.. _XDP_ABORTED:

XDP_ABORTED
===========

.. (ebpf program error case)

Fall-through
------------

There must also be a fall-through ``default:`` case, which is hit if
the program returns an unknown action code (e.g. future action this
driver does not support).

In that case the packets is dropped and a warning is generated (once)
about the invalid XDP program action code, by calling::

 bpf_warn_invalid_xdp_action(action_code);


Code example
------------

The basic action code block the driver use, is simply a switch-case
statement as below.

.. code-block:: c

	switch (action) {
		case XDP_PASS:
			break; /* Normal netstack handling */
		case XDP_TX:
			if (driver_xmit(dev, page, length) == NETDEV_TX_OK)
				goto consumed;
			goto xdp_drop; /* Drop on xmit failure */
		default:
			bpf_warn_invalid_xdp_action(action);
		case XDP_ABORTED:
		case XDP_DROP:
		xdp_drop:
			if (driver_recycle(page, ring))
				goto consumed;
			goto next; /* Drop */
		}
	}

.. Warning:: It is still undecided if the ``action`` code need to be
             partitioned into opcodes and some of the upper-bits use
             as values for the given opcode.
