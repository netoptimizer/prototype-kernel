#!/bin/bash
# Author: Jesper Dangaard Brouer <brouer@redhat.com>
# License: GPL
#
# This script "kernel_install.sh" just installs/copies the binaries into
# a directory (~/git/kernel/install) with the kernel version (extracted
# from include/config/kernel.release).
#
# It is connected to "kernel_push.sh" which rsync the kernel
# binaries+modules to a remote host (usually a KVM virtual guest).

export INSTALL_MOD_PATH=~/git/kernel/install/

export VER=`cat include/config/kernel.release`
echo $VER

CPUS=24

if [ ! -d "$INSTALL_MOD_PATH" ]; then
    echo "ERROR: Install dir for kernel binaries \"$INSTALL_MOD_PATH\" does not exist"
    exit 3
fi

if [ -z "$VER" ]; then
    echo "ERROR: Cannot detect version"
    exit 2
fi

rm -rf $INSTALL_MOD_PATH/lib/modules/$VER/kernel/
make -j${CPUS} modules_install

export BOOT="$INSTALL_MOD_PATH/boot/"
[ -d $BOOT ] ||  mkdir $BOOT

#cp -v arch/x86/boot/bzImage $BOOT/vmlinuz-$VER
#cp -v arch/i386/boot/bzImage $BOOT/vmlinuz-$VER
cp -v arch/x86_64/boot/bzImage $BOOT/vmlinuz-$VER
cp -v System.map $BOOT/System.map-$VER
cp -v vmlinux $BOOT/vmlinux-$VER
cp -v .config $BOOT/config-$VER
cp -v Module.symvers $BOOT/Module.symvers-$VER

echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
echo "-=-=-=- DONE installed kernel:[$VER] -=-=-=-"
echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
