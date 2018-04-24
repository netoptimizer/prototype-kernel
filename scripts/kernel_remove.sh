#!/bin/bash
# Author: Jesper Dangaard Brouer <brouer@redhat.com>
# License: GPL
#
# Delete kernel binaries to save harddisk space

if [ -n "$1" ]; then
    export KERN=$1
else
    echo " - ERROR - Please specify KERNEL to DELETE!"
    exit 1
fi

if [ "$KERN" == "/" ]; then
    echo " - ERROR - dangerus input exit \"$KERN\""
    exit 2
fi

echo "DELETE kernel:${KERN}"

# Trick so, program can be run as normal user, will just use "sudo"
if [ "$EUID" -ne 0 ]; then
    # Directly executable easy
    if [ -x $0 ]; then
	sudo "$0" "$1"
	exit $?
    fi
    echo "ERROR: cannot perform sudo run of $0"
    exit 4
fi

MOD_DIR=/lib/modules/${KERN}
if [ -d ${MOD_DIR} ]; then
    echo " - Deleting modules dir:${MOD_DIR}"
    rm -rf ${MOD_DIR}
else
    echo " - WARNING - Modules dir:${MOD_DIR} does NOT exist"
    #exit 4
fi

function delete() {
    if [ -n "$1" ]; then
	local prefix=$1
    else
	echo " - WARNING - wrong input to delete()"
	return 1
    fi
    if [ -n "$2" ]; then
	local suffix=$2
    fi
    local file=/boot/${prefix}-${KERN}${suffix}

    if [ -f ${file} ]; then
	echo " - Removing file: $file"
	rm $file
    else
	echo " - WARNING - cannot find file:\"$file\""
	return 2
    fi
}

echo " Deleting kernel files"
delete vmlinuz
delete vmlinux
delete System.map
delete config
delete initrd .img
delete initramfs .img
delete Module.symvers

# On RedHat: clean up grub
if [ -e /etc/redhat-release ]; then
    # Red Hat version:
    rh_kernel_remove="new-kernel-pkg --remove ${KERN}"
    echo "- Detected Red Hat target, cleanup grub config"
    echo "   run: $rh_kernel_remove"
    $rh_kernel_remove
else
    echo "Implement cleanup of grub on your distro... please"
fi
