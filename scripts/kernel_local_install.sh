#!/bin/bash
# Author: Jesper Dangaard Brouer <brouer@redhat.com>
# License: GPL
#
# Installs/copies the kerne binaries directly on localhost
#

export VER=`cat include/config/kernel.release`
echo $VER

if [ -z "$VER" ]; then
    echo "ERROR: Cannot detect version"
    exit 2
fi

# Trick so, program can be run as normal user, will just use "sudo"
if [ "$EUID" -ne 0 ]; then
    # Directly executable easy
    if [ -x $0 ]; then
	sudo "$0"
	exit $?
    fi
    echo "ERROR: cannot perform sudo run of $0"
    exit 4
fi

#export INSTALL_MOD_PATH=/var/kernels/git/install/
export INSTALL_MOD_PATH=/
rm -rf $INSTALL_MOD_PATH/lib/modules/$VER/kernel/
make -j 16 modules_install

export BOOT="$INSTALL_MOD_PATH/boot/"
[ -d $BOOT ] ||  mkdir $BOOT

cp -v arch/x86/boot/bzImage $BOOT/vmlinuz-$VER
cp -v arch/i386/boot/bzImage $BOOT/vmlinuz-$VER
cp -v System.map $BOOT/System.map-$VER
cp -v vmlinux $BOOT/vmlinux-$VER
cp -v .config $BOOT/config-$VER
cp -v Module.symvers $BOOT/Module.symvers-$VER || exit 3

if [ -e /etc/redhat-release ]; then
    # Red Hat version:
    rh_kernel_update="new-kernel-pkg --mkinitrd --depmod --install ${VER}"
    echo "- Detected Red Hat target,"
    echo "   run: $rh_kernel_update"
    $rh_kernel_update
else
    depmod -a ${KERNEL}
    mkinitramfs -o /boot/initrd.img-$VER $VER
    update-grub2
fi

echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
echo "-=-=-=- DONE installed kernel:[$VER] -=-=-=-"
echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
