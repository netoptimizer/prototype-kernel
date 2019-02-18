#!/bin/bash
# Author: Jesper Dangaard Brouer <brouer@redhat.com>
# License: GPL
#
# This script "kernel_push.sh" rsync the kernel binaries+modules to a
# remote host (usually a KVM virtual guest).  And invokes "new-kernel-pkg"
# on the remote target, which updates grub.conf appropriately (debian targets
# uses depmod+mkinitramfs+update-grub2).
#
# It depends on kernel binaries have been installed, by the script
# "kernel_install.sh", which installs/copies the binaries into a
# directory (~/git/kernel/install)
#
BINARIES_PATH=~/git/kernel/install

#time usage debugging
#t=time
t=/usr/bin/time

if [ -n "$1" ]; then
    export HOST=$1
else
    if [ -z $HOST ]; then
	echo "ERROR - Please specify HOSTNAME to push to!"
	exit 1
    fi
fi

# Target host OS distro e.g. debian
if [ -n "$2" ]; then
    export TARGET=$2
    echo "Using disto target: $TARGET"
else
    if ssh root@${HOST} test -e /etc/debian_version; then
        export TARGET=debian
    else
        export TARGET=redhat
    fi
fi


if [ -z "$VER" ]; then
	export VER=`cat include/config/kernel.release`
fi
export KERNEL=$VER

if [ -z "$KERNEL" ]; then
	echo "ERROR - Cannot detect the Kernel version you want to push"
	exit 2
fi

echo "-=-=-=- Pushing kernel:[$VER] to host:[$HOST] -=-=-=-"

pushd $BINARIES_PATH
$t rsync -e ssh -rptlvuz boot/vmlinuz-${KERNEL} \
   boot/config-${KERNEL} \
   boot/System.map-${KERNEL} \
   boot/Module.symvers-${KERNEL} \
   root@${HOST}:/boot/
$t rsync -e ssh -rcptlvuz --delete lib/modules/${KERNEL}  root@${HOST}:/lib/modules/
popd

echo "Executing on remote host: ${HOST}"

echo "Target install distro: $TARGET"
if [ "$TARGET" == "debian" ]; then
    #Debian version:
    echo " - depmod -a ${KERNEL}"
    $t ssh root@${HOST} depmod -a ${KERNEL}
    echo " - mkinitramfs"
    $t ssh root@${HOST} mkinitramfs -o /boot/initrd.img-${KERNEL} ${KERNEL}
    $t ssh root@${HOST} update-grub2
elif [ "$TARGET" == "redhat" ]; then
    # Red Hat version:
    rh_kernel_update="new-kernel-pkg --mkinitrd --depmod --install ${KERNEL}"
    # note mkinitrd is slow
    #rh_kernel_update="new-kernel-pkg --depmod --install ${KERNEL}"
    #
    # Try to use --host-only to limit the size of initrd
    #rh_kernel_update="new-kernel-pkg --mkinitrd --host-only --depmod --install ${KERNEL}"
    #
    # Try --host-only and --dracut
    rh_kernel_update="new-kernel-pkg --mkinitrd --dracut --host-only --depmod --install ${KERNEL}"

    echo "- Assuming Red Hat target,"
    echo "   run: $rh_kernel_update"
    $t ssh root@${HOST} "$rh_kernel_update"
else
    echo "ERROR - Unknown install target: $TARGET"
fi
# Fedora 17:
#  grub2-mkconfig -o /boot/grub2/grub.cfg

echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
echo "-=-=-=- DONE Pushed kernel:[$VER] to host:[$HOST] -=-=-=-"
echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
