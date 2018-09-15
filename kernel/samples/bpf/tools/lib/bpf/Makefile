#
# Makefile for out-of-kernel-tree building libbpf.a
#  similar to kernel/tools/lib/bpf
#
# This is a temporary workaround, as we really want libbpf.so
# to be install and supported by distros.
#

# Should we depend on distro installing: kernel-headers
# (Fedora package called: kernel-headers)
# - E.g. depend on /usr/include/linux/perf_event.h
#
# Or do we depend on kernel source code:
# KERNEL ?= /lib/modules/$(shell uname -r)/build/
#
# Are we independend of kernel source now?

OBJECT_NLATTR       = nlattr.o
OBJECT_BTF          = btf.o
OBJECT_BPF_SYSCALLS = bpf.o
OBJECT_LIBBPF       = libbpf.o
OBJECTS = $(OBJECT_NLATTR) $(OBJECT_BTF) $(OBJECT_BPF_SYSCALLS) $(OBJECT_LIBBPF)

CC = gcc

TOOLS_PATH = ../../../tools

CFLAGS_LIBBPF += -fPIC

# We maintain some of the kernel include files to build easier
CFLAGS_LIBBPF += -I$(TOOLS_PATH)/include
#
# for <perf-sys.h>
CFLAGS_LIBBPF += -I$(TOOLS_PATH)/perf
#
# Files based on kernel 'make headers_install'
CFLAGS_LIBBPF += -I../../../kernel-usr-include/
#
CFLAGS_LIBBPF += -I/usr/include/

# The kernels directories, where include files are located:
#CFLAGS_LIBBPF += -I$(KERNEL)/tools/include
#CFLAGS_LIBBPF += -I$(KERNEL)/tools/perf
#CFLAGS_LIBBPF += -I$(KERNEL)/usr/include
#CFLAGS_LIBBPF += -I$(KERNEL)/arch/x86/include

LDFLAGS = -lelf

all: $(OBJECTS) libbpf.a

.PHONY: clean

clean:
	rm -f $(OBJECTS) libbpf.a

# LIBBPF OBJECTS
$(OBJECTS): %.o: %.c  Makefile
	$(CC) $(CFLAGS_LIBBPF) -o $@ -c $<

libbpf.a: $(OBJECTS) Makefile
	$(RM) $@; $(AR) rcs $@  $(OBJECTS)
