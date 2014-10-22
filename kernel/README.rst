
In this directory we try to keep close to the kernel directory layout,
in the hopes that it will make it easier, when posting/proposing these
changes upstream.

Notice the "Kbuild" files, they define and control which modules are
compiled.

The kernel compiled against is detected by following running kernels
build symlink in::

 /lib/modules/`uname -r`/build/

To compile against another kernel use::

 make kbuilddir=~/git/kernel/net-next/

