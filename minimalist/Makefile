# Minimal Makefile for compiling external kernel modules
kbuilddir ?= /lib/modules/$(shell uname -r)/build/
obj-m += example01.o
obj-m += example02.o
modules:
	$(MAKE) -C $(kbuilddir) M=$$PWD modules
install:
	$(MAKE) -C $(kbuilddir) M=$$PWD modules_install
clean:
	$(MAKE) -C $(kbuilddir) M=$$PWD clean
	@rm -f *~
.PHONY: default install clean
