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

all: prepare verify_kernel_source_dir modules

verify_kernel_source_dir:
	@if [ ! -d $(kbuilddir) ]; then \
		echo "ERROR: kernel source dir missing ($(kbuilddir))"; \
		exit 1; \
	fi

modules:
	$(MAKE) -C $(kbuilddir) M=$$PWD KDIR=$$PWD modules

install:
	$(MAKE) -C $(kbuilddir) M=$$PWD KDIR=$$PWD modules_install

bpf:
	$(MAKE) -C samples/bpf/ kbuilddir=$(kbuilddir)


# Example usage:
#  make push_remote kbuilddir=~/git/kernel/net-next/ HOST=192.168.122.49
push_remote: modules
	@if [ -z $(HOST) ]; then \
		echo -e "\n ***ERROR***: need remote \"HOST\" variable, use:"; \
		echo -e "  make push_remote HOST=192.168.122.49\n"; \
		exit 2; \
	fi
	scripts/push_remote.sh $(HOST)

clean_remote:
	@if [ -z $(HOST) ]; then \
		echo -e "\n ***ERROR***: need remote \"HOST\" variable, use:"; \
		echo -e "  make clean_remote HOST=192.168.122.49\n"; \
		exit 2; \
	fi
	scripts/push_remote.sh --delete $(HOST)

prepare:
	@if [ ! -f .config ]; then \
		cp config.default .config ; \
	else true; fi
	@if [ config.default -nt .config ]; then \
		echo -e "\n**NOTICE**: config.default is newer than .config"; \
		echo -e " you might miss compiling recently added modules\n"; \
	fi

clean: prepare
	$(MAKE) -C $(kbuilddir) M=$$PWD KDIR=$$PWD clean
	@rm -f *~

.PHONY: all prepare modules install clean verify_kernel_source_dir
