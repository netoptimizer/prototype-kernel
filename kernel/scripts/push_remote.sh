#!/bin/bash
#
# Upload kernel binary modules to a remote host
#
# See Makefile target "push_remote"
# Usage ala:
#  make push_remote kbuilddir=~/git/kernel/net-next/ HOST=192.168.122.49
#
# Notice, need to be called from the "kernel" dir, one dir back ".."
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

if [ -n "$1" ]; then
    export HOST=$1
else
    if [ -z $HOST ]; then
	echo "ERROR - Please specify HOST to push to!"
	exit 1
    fi
fi

if [ -n "$2" ]; then
    KERNEL=$2
fi

export SSH_HOST=root@${HOST}
export UPLOAD_DIR=/root/prototype-kernel
export UPLOAD_SCRIPTS=${UPLOAD_DIR}/scripts
export UPLOAD_LIB_MODULES=${UPLOAD_DIR}/lib

setup_script=setup_prototype_devel_env.sh

SOURCE_DIR=`pwd`
# Check if we have been called from correct path
if [ ! -d "${SOURCE_DIR}/lib" ]; then
    echo "ERROR - Wrong path, cannot find the dir ${SOURCE_DIR}/lib"
    exit 6
fi

# Make dir on remote
ssh $SSH_HOST mkdir -p $UPLOAD_DIR || exit 1

# Copy over setup scripts
rsync -avz scripts ${SSH_HOST}:/${UPLOAD_DIR}/ || exit 2

if [ -n "$DELETE" ]; then
    # Instruct setup_script to delete modules and symlinks
    ssh $SSH_HOST "cd $UPLOAD_DIR && ./scripts/$setup_script --delete $KERNEL" || exit 5
else
    # Push/copy binary modules to remote
    #rsync -avz lib/*.ko ${SSH_HOST}:/${UPLOAD_LIB_MODULES}/ || exit 3
    #
    dirs_with_modules="lib mm mm/bench"
    for dir in $dirs_with_modules; do
	ssh $SSH_HOST mkdir -p ${UPLOAD_DIR}/$dir || exit 7
	rsync -avz ${dir}/*.ko ${SSH_HOST}:/${UPLOAD_DIR}/$dir/ || exit 3
    done
    # Run setup script remotely
    ssh $SSH_HOST "cd $UPLOAD_DIR && ./scripts/$setup_script $KERNEL" || exit 6
fi
