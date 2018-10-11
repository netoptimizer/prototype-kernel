#!/bin/bash
# Author: Jesper Dangaard Brouer <brouer@redhat.com>
# License: GPL
#
# Script for easier cross-compile of ARM64/aarch64 kernels
export ARCH=arm64

# Using Linaro provided pre-compiled toolchain, download from:
#   https://releases.linaro.org/components/toolchain/binaries/latest-7/aarch64-linux-gnu/
#   File: gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu.tar.xz
#
export VERSION=gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu
export CROSS_COMPILE_DIR=/home/${USER}/cross-compilers/${VERSION}
export CROSS_COMPILE="aarch64-linux-gnu-"

if [[ ! -d $CROSS_COMPILE_DIR ]]; then
    echo "ERROR: Cannot find cross-compiler directory: \"$CROSS_COMPILE_DIR\""
    exit 1
fi

export PATH=${CROSS_COMPILE_DIR}/bin:${PATH}
echo "Cross-compiler in PATH: $PATH"

if [[ ! -e .config ]]; then
    echo "ERROR: You need to config kernel first"
    echo " Hint: see kernel_arm64_config_macchostobin.sh"
    exit 2
fi

which nproc 2>&1 > /dev/null
if [[ $? -eq 0 ]]; then
    CPUS=$(nproc)
else
    CPUS=4
fi
echo "Parallel compile on $CPUS CPUs"

OUTPUT=out.compile--$(date +%Y-%m-%d--%H:%M:%S)

(time make -j${CPUS}) 2>&1  | tee $OUTPUT

# For macchiatobin
set -xv
make marvell/armada-8040-mcbin.dtb
