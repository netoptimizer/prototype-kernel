#!/bin/bash
# Author: Jesper Dangaard Brouer <brouer@redhat.com>
# License: GPL
#
# Create default config for Macchiatobin Double Shot
export ARCH=arm64

# Wiki Docs:
# http://wiki.macchiatobin.net/tiki-index.php?page=Build+from+source+-+Kernel


# You need to manually get and copy their default config
#  git clone https://github.com/MarvellEmbeddedProcessors/linux-marvell
#  git checkout linux-4.14.22-armada-18.09
#
# Copy arch/arm64/configs/mvebu_v8_lsp_defconfig
#
CONF=mvebu_v8_lsp_defconfig
CONF_FILE=arch/arm64/configs/$CONF

if [[ ! -e $CONF_FILE ]]; then
    echo "ERROR - missing config file: $CONF_FILE"
    echo " Follow inst in this script howto get it..."
    exit 2
fi

# Using Linaro provided pre-compiled toolchain
#  https://releases.linaro.org/components/toolchain/binaries/latest-7/aarch64-linux-gnu/
export VERSION=gcc-linaro-7.3.1-2018.05-x86_64_aarch64-linux-gnu
export CROSS_COMPILE_DIR=/home/${USER}/cross-compilers/${VERSION}
export CROSS_COMPILE="aarch64-linux-gnu-"

if [[ ! -d $CROSS_COMPILE_DIR ]]; then
    echo "ERROR: Cannot find cross-compiler directory: \"$CROSS_COMPILE_DIR\""
    exit 1
fi

export PATH=${CROSS_COMPILE_DIR}/bin:${PATH}
echo "Cross-compiler $CROSS_COMPILE"

set -xv
make mvebu_v8_lsp_defconfig

