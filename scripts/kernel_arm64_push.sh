#!/bin/bash
# Author: Jesper Dangaard Brouer <brouer@redhat.com>
# License: GPL
#
# This script "kernel_arm64_push.sh" rsync the kernel binaries + dtb file
# to a remote ARM64 board
export ARCH=arm64

t=/usr/bin/time

# exit script on any errors
set -e

if [ -n "$1" ]; then
    export HOST=$1
else
    if [ -z $HOST ]; then
	echo "ERROR - Please specify HOSTNAME to push to!"
	exit 1
    fi
fi

# Target board for choosing correct DTS/DTB file (arch/arm64/boot/dts/)
if [ -n "$2" ]; then
    TARGET=$2
    echo "Using arch/arm64 DTS target: $TARGET"
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

if [ "$TARGET" == "mcbin" ]; then
    DTS_FILE=arch/arm64/boot/dts/marvell/armada-8040-mcbin.dtb
elif [ "$TARGET" == "espressobin" ]; then
    DTS_FILE=arch/arm64/boot/dts/marvell/armada-3720-espressobin.dtb
else
    echo "Not updating board DTS/DTB"
fi
# If target specified push the DTS/DTB file
if [ -n "$DTS_FILE" ]; then
    echo "Update DST/DTB file for target ARM64 board: $TARGET"
    $t rsync -e ssh -av $DTS_FILE root@${HOST}:/boot/
fi

echo "-=-=-=- Pushing kernel:[$VER] to host:[$HOST] -=-=-=-"

$t rsync -e ssh -av $IMAGE_FILE root@${HOST}:/boot/


echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
echo "-=-=-=- DONE Pushed kernel:[$VER] to host:[$HOST] -=-=-=-"
echo "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-"
