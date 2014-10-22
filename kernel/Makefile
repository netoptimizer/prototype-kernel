# -*- Makefile -*-
#
# Author: Jesper Dangaard Brouer <netoptimizer@brouer.com>
#
# Notice:
#  Kernel module targets are defined inside Kbuild files

# Kernel builddir detection
#  can be overwritten by setting it on make cmdline like:
# make kbuilddir=~/git/kernel/net-next/
#
kbuilddir ?= /lib/modules/$(shell uname -r)/build/

# Trick to modify kernels CFLAGS
#KBUILD_CFLAGS+=-g -O0
#KBUILD_CFLAGS+=-g -O3
#KBUILD_CFLAGS+=-g -O2 -fno-inline

all: verify_kernel_source_dir modules

verify_kernel_source_dir:
	@if [ ! -d $(kbuilddir) ]; then \
		echo "ERROR: kernel source dir missing ($(kbuilddir))"; \
		exit 1; \
	fi

modules:
	$(MAKE) -C $(kbuilddir) M=$$PWD KDIR=$$PWD modules

install:
	$(MAKE) -C $(kbuilddir) M=$$PWD KDIR=$$PWD modules_install

clean:
	$(MAKE) -C $(kbuilddir) M=$$PWD KDIR=$$PWD clean
	@rm -f *~

.PHONY: all modules install clean verify_kernel_source_dir
