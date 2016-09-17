==============================
Prototype Kernel build process
==============================

In the ``kernel/`` directory we try to keep close to the kernel
directory layout, in the hopes that it will make it easier, when
posting/proposing these changes upstream.

.. Note::

   It is a pre-requisite that you have a developement kernel tree
   available for compiling against (or install your distributions
   kernel-devel package).

Compiling modules
=================

To compile your modules simply type ``make`` in the ``kernel``
directory.

The ``Makefile`` tries to detect the kernel directory to compile
against by following the running kernels build symlink in::

 /lib/modules/`uname -r`/build/

To compile against another (specific) kernel tree use::

 make kbuilddir=~/git/kernel/net-next/

Notice look in the ``Kbuild`` files, they define and control which
modules are compiled, also see the ``.config`` file.


Push to remote host
===================

Q: Want to compile locally and push the binary modules to a remote host.
A: Yes, this is supported.

The Makefile target "push_remote" uploads the kernel module to a
remote host.  (You need to setup SSH-keys to SSH allow root logins.)

Usage example::

 make push_remote kbuilddir=~/git/kernel/net-next/ HOST=192.168.122.49

If you want to run this manually call the script directly::

 ./scripts/push_remote.sh 192.168.122.49


Enable/disable modules
======================

It can be practical to allow manual enable/disable of which modules
are getting build.  This is supported by locally adjusting .config.
On first run the content is based on config.default.

This feature is useful when developing against API's that have not
been included the mainline kernel yet.  See CONFIG_SLAB_BULK_API=m for
an example.
