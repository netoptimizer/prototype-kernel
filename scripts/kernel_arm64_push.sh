#!/bin/bash
# Author: Jesper Dangaard Brouer <brouer@redhat.com>
# License: GPL
#
# This script "kernel_arm64_push.sh" rsync the kernel binaries + dtb file
# to a remote ARM64 board
export ARCH=arm64

t=/usr/bin/time

if [ -n "$1" ]; then
    export HOST=$1
else
    if [ -z $HOST ]; then
	echo "ERROR - Please specify HOSTNAME to push to!"
	exit 1
    fi
fi

# Target board OS distro e.g. debian
if [ -n "$2" ]; then
    export TARGET=$2
    echo "Using disto target: $TARGET"
else
    export TARGET=mcbin
fi

if [ -z "$VER" ]; then
	export VER=`cat include/config/kernel.release`
fi
export KERNEL=$VER

if [ -z "$KERNEL" ]; then
	echo "ERROR - Cannot detect the Kernel version you want to push"
	exit 2
fi

IMAGE_FILE=arch/arm64/boot/Image
if [[ ! -e $IMAGE_FILE ]]; then
	echo "ERROR - Cannot find ARM64 kernel image file"
	exit 3
fi

echo "Target ARM64 board: $TARGET"
if [ "$TARGET" == "mcbin" ]; then
    DTS_FILE=arch/arm64/boot/dts/marvell/armada-8040-mcbin.dtb
elif [ "$TARGET" == "something-else" ]; then
    echo "- Add another target: $TARGET"
    exit 4
else
    echo "ERROR - Unknown board target: $TARGET"
    exit 4
fi
$t rsync -e ssh -av $DTS_FILE root@${HOST}:/boot/

echo "-=-=-=- Pushing kernel:[$VER] to host:[$HOST] -=-=-=-"

$t rsync -e ssh -av $IMAGE_FILE root@${HOST}:/boot/


echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
echo "-=-=-=- DONE Pushed kernel:[$VER] to host:[$HOST] -=-=-=-"
echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
