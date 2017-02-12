TARGETS = xdp_ddos01_blacklist

kbuilddir ?= /lib/modules/$(shell uname -r)/build/
KERNEL=$(kbuilddir)

CFLAGS := -O2 -Wall

CGLAGS += -I$(KERNEL)/include/
CFLAGS += -I$(KERNEL)/usr/include
CFLAGS += -I$(KERNEL)/tools/lib -I$(KERNEL)/tools/include
CFLAGS += -I$(KERNEL)/tools/perf
# Strange dependency to "selftests"
CFLAGS += -I$(KERNEL)/tools/testing/selftests/bpf/

LDFLAGS= -lelf

LLC ?= llc
CLANG ?= clang

CC = gcc

NOSTDINC_FLAGS=-nostdinc -isystem /usr/lib/gcc/x86_64-redhat-linux/6.2.1/include
LINUXINCLUDE=-I $(KERNEL)/arch/x86/include -I$(KERNEL)/arch/x86/include/generated/uapi -I$(KERNEL)/arch/x86/include/generated -I$(KERNEL)/include -I$(KERNEL)/arch/x86/include/uapi -I$(KERNEL)/include/uapi -I$(KERNEL)/include/generated/uapi -include $(KERNEL)/include/linux/kconfig.h  -I$(KERNEL)/tools/lib
EXTRA_CFLAGS=-Werror

all: linux-src-devel-headers verify_llvm_target_bpf $(TARGETS)

clean:
	rm -f bpf_load.o  bpf.o
	rm -f xdp_ttl xdp_ttl_kern.o xdp_ttl_kern.bc xdp_ttl_user.o
	rm -f xdp_passer xdp_passer_kern.o xdp_passer_kern.bc xdp_passer_user.o

linux-src:
	@if ! test -d $(KERNEL)/; then \
		echo "ERROR: Need kernel source code to compile against" ;\
		exit 1; \
	else true; fi

linux-src-libbpf: linux-src
	@if ! test -d $(KERNEL)/tools/lib/bpf/; then \
		echo "ERROR: Need kernel source code to compile against" ;\
		echo "       and specifically tools/lib/bpf/ "; \
		exit 1; \
	else true; fi

linux-src-devel-headers: linux-src-libbpf
	@if ! test -d $(KERNEL)/usr/include/ ; then \
		echo -n "ERROR: Need kernel source devel headers"; \
		echo    " likely need to run:"; \
		echo "       (in kernel source dir: $(KERNEL))"; \
		echo -e "\n  make headers_install\n"; \
		false; \
	else true; fi

verify_cmds: $(CLANG) $(LLC)
	@for TOOL in $^ ; do \
		if ! (which -- "$${TOOL}" > /dev/null 2>&1); then \
			echo "*** ERROR: Cannot find LLVM tool $${TOOL}" ;\
			exit 1; \
		else true; fi; \
	done

verify_llvm_target_bpf: verify_cmds
	@if ! (${LLC} -march=bpf -mattr=help > /dev/null 2>&1); then \
		echo "*** ERROR: LLVM (${LLC}) does not support 'bpf' target" ;\
		echo "   NOTICE: LLVM version >= 3.7.1 required" ;\
		exit 2; \
	else true; fi


bpf.o: $(KERNEL)/tools/lib/bpf/bpf.c
	$(CC) $(CFLAGS) -o $@ -c $<

bpf_load.o: $(KERNEL)/samples/bpf/bpf_load.c
	$(CC) $(CFLAGS) -I$(KERNEL)/tools/perf -o $@ -c $<



%_kern.o: %_kern.c
	$(CLANG) $(NOSTDINC_FLAGS) $(LINUXINCLUDE) $(EXTRA_CFLAGS) \
	    -D__KERNEL__ -D__ASM_SYSREG_H -Wno-unused-value -Wno-pointer-sign \
	    -Wno-compare-distinct-pointer-types \
	    -Wno-gnu-variable-sized-type-not-at-end \
	    -Wno-tautological-compare \
	    -O2 -emit-llvm -c $< -o -| $(LLC) -march=bpf -filetype=obj -o $@

xdp_ddos01_blacklist_user.o: xdp_ddos01_blacklist_user.c
	$(CC) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

xdp_ddos01_blacklist: xdp_ddos01_blacklist_user.o  bpf.o bpf_load.o
	make $@_kern.o
	$(CC) $(CFLAGS) -I$(KERNEL)/tools/include/uapi/linux $(LDFLAGS) -o $@ $^

.PHONY: verify_cmds verify_target_bpf $(CLANG) $(LLC)
