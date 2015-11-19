#!/bin/bash
#
# This script is almost same as: fail01_kmem_cache_alloc_bulk.sh
#  - See desc about double page alloc fault and "space" trick in that script
#
# Difference is testing normal (non-bulk) call: kmem_cache_alloc()
#
# This test exercise calling slab_post_alloc_hook() with object==NULL.
# The normal slab fault-injector "failslab", does not test this,
# because it will exit after the call to slab_pre_alloc_hook().
#
# Remember to compile kernel with config options for either kmemleak
# or kasan, else the compiler will have removed the code we want to
# exercise.
#
MODULE=slab_bulk_test04_exhaust_mem
VERBOSE=1

# This tool is taken from the kernel: tools/testing/fault-injection/failcmd.sh
FAILCMD=./failcmd.sh

if [[ $UID != 0 ]]; then
	echo must be run as root >&2
	exit 1
fi

$(modinfo $MODULE > /dev/null 2>&1)
if [[ $? != 0 ]]; then
    echo "ERR - Need kernel module $MODULE for this test"
    exit 2
fi

if [[ ! -x $FAILCMD ]]; then
    echo "ERR - Need failcmd.sh ($FAILCMD) script from kernel"
    echo "Copy from kernel tree: tools/testing/fault-injection/failcmd.sh"
    exit 3
fi

# Enable "fail_page_alloc" via environment variable
export FAILCMD_TYPE=fail_page_alloc

$FAILCMD --probability=100 --times=10 \
 --verbose=1 --interval=1 \
 --space=100 \
 --min-order=0 \
 -- modprobe $MODULE verbose=1 max_objects=10000 no_bulk=1
# -=-=-=-=-=-=-=-=-=-=-=-=-
# NOTICE: no_bulk=1 setting
# -=-=-=-=-=-=-=-=-=-=-=-=-

# Cleanup: return min-order to default 1,
#   else it generate too many faults
DEBUGFS=`mount -t debugfs | head -1 | awk '{ print $3}'`
FAULTATTR=$DEBUGFS/$FAILCMD_TYPE
echo 1 > $FAULTATTR/min-order

# Cleanup: remove module again
rmmod $MODULE

if [[ $VERBOSE > 0 ]]; then
    dmesg | egrep -e "fail_page_alloc|SLUB|FAULT_INJECTION|$MODULE" | tail -n40
    echo -e "\nNOTICE - Verify above dmesg if test caused correct fault"
fi
