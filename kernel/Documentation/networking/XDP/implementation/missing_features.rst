================
Missing Features
================

Record missing implementation features here.


Missing: Push/pop headers
=========================

Requirement defined here: :ref:`adjust_header`.

Needed by :doc:`../use-cases/xdp_use_case_ddos_scrubber`

Initial support for XDP head adjustment added to net-next in this commit:
 https://git.kernel.org/davem/net-next/c/293bfa9b486

Initial support only covers driver mlx4.

.. TODO:: Update document once feature is avail in a kernel release.
	  Plus, keep track of drivers supporting this feature.

.. TODO:: Create new section under :doc:`userspace_api` that describe
          howo use this and point to sample programs.

The eBPF program gets a new helper function called: ``bpf_xdp_adjust_head``


Missing: Multi-port TX
======================

Missing: Capabilities negotiation
=================================

See: :ref:`ref_prog_negotiation`

Missing: XDP program per RX queue
=================================

Changes to the user space API are needed to add this feature.

Missing: Cache prefetching
==========================

