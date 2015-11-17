#!/bin/bash
#
# The purpose of this script is to test error path in kernel function
# call kmem_cache_alloc_bulk() code.
#
# By using the kernels fault-injection framework "fail_page_alloc".
#
#  https://www.kernel.org/doc/Documentation/fault-injection/fault-injection.txt
#
# The SLUB code have its own "failslab" fault-injection, but cannot be
# used for this test, as does not activate the needed code path.
#
# It is a little tricky to make the SLUB code fail on page_alloc's
# (via "fail_page_alloc"), because SLUB calls alloc_pages() two
# constitutive times (with lowered allocation order) before it gives up.
#
# The second time SLUB calls alloc_pages() it can have lowered the
# "order" to ZERO, and "fail_page_alloc" have min_order=1.  Simply
# changing min_order=0 cause too many faults, not allowing fork to
# start modprobe.
#
# Hack-trick is to use "space" to delay the failures to happen.
#
# Thus, some specific filtering and setup of "fail_page_alloc" is
# needed to create the right conditions.

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

$FAILCMD --probability=100 --times=4 \
 --verbose=1 --interval=1 \
 --space=100 \
 --min-order=0 \
 -- modprobe $MODULE verbose=1 max_objects=10000

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
