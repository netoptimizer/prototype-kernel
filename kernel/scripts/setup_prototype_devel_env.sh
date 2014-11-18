#!/bin/bash
#
# Setup symlinks to prototype-kernel binary modules
# - Both use for push_remote.sh and local devel setup
#
# Notice, need to be called from the "kernel" dir
#
e=echo

# Options parsing
optspec=":hv-:"
while getopts "$optspec" optchar; do
    case "${optchar}" in
        -)
            case "${OPTARG}" in
                delete)
                    # Deleting symlinks and kernel modules
		    DELETE=yes
                    ;;
                *)
		    echo "Unknown option --${OPTARG}" >&2
                    ;;
            esac;;
        h)
            echo "usage: $0 [-v] [--delete]" >&2
            exit 5
            ;;
        v)
            echo "Verbose '-${optchar}'" >&2
	    e=echo
            ;;
        *)
            if [ "$OPTERR" != 1 ] || [ "${optspec:0:1}" = ":" ]; then
                echo "Unknown option: '-${OPTARG}'" >&2
            fi
            ;;
    esac
done
shift $(( $OPTIND - 1 ))

#SOURCE_DIR=/root/prototype-kernel
#SOURCE_DIR=~/git/protptype-kernel
SOURCE_DIR=`pwd`

# Kernel module dir is auto detected, but can be controlled manually
# by arg $1
#KERNEL_DIR=xxx
if [ -n "$1" ]; then
    KERNEL=$1
    $e "Notice - Using target kernel $KERNEL"
    KERNEL_DIR=/lib/modules/${KERNEL}
fi

# SOURCE_DIR can be given as command line input
if [ -n "$2" ]; then
    $e "Notice - Changing binary source dir to $2"
    SOURCE_DIR=$2
fi

# Setup SOURCE_DIR's if not exist
if [ ! -d "${SOURCE_DIR}" ]; then
    mkdir -p ${SOURCE_DIR} || exit 6
fi
# Modules (under lib)
if [ ! -d "${SOURCE_DIR}/lib" ]; then
    echo "ERROR - Cannot find the dir ${SOURCE_DIR}/lib"
    exit 3
fi

# Detect kernel module dir
if [ -z "$KERNEL_DIR" ]; then
    KERNEL_DIR=/lib/modules/`uname -r`
fi
# Check dir exist
if [ ! -d "$KERNEL_DIR" ]; then
    echo "ERROR - Cannot find KERNEL_DIR:${KERNEL_DIR}"
    exit 4
fi
# Create an "extra" kernel module dir
if [ ! -d "${KERNEL_DIR}/kernel/extra" ]; then
    mkdir ${KERNEL_DIR}/kernel/extra || exit 5
fi

if [ -n "$DELETE" ]; then
    $e "Deleting all kernel modules and symlinks"
    rm -rf ${KERNEL_DIR}/kernel/extra
    # Only delete the compiled kernel modules
    find  ${SOURCE_DIR} -name \*.ko | xargs -I '{}' rm -v '{}'
else
    $e "Symlink all kernel modules"
    cd ${KERNEL_DIR}/kernel/extra
    # Symlinks modules under lib/
    # ln -vsf ${SOURCE_DIR}/lib/*.ko .
    # Symlink all *.ko files
    find  ${SOURCE_DIR} -name \*.ko | xargs -I '{}' ln -vsf '{}' .
fi

$e "Depmod - update kernel module symbols (on `hostname`)"
depmod -a $KERNEL
