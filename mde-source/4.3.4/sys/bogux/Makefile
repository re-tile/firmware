# This is a -*- Makefile -*-

# Standalone makefile for "bogux" (a sample supervisor).

# Default target.
all:

ifdef TOP
ROOT ?= /undefined-ROOT
INCLUDES = -I$(TOP)/sys/bogux -I$(TOP)/sys/bogux/tilegx/include
VPATH = $(TOP)/sys/bogux
else
TOP = ../..
ROOT = ../..
INCLUDES = -I. -Itilegx/include
endif

include $(TOP)/sys/Makefile.inc

LIBC_A = $(ROOT)/sys/libc/libc_sys.a
LIBTILECC_A := $(shell $(CC) -print-libgcc-file-name)


CDEFS = -D__BOGUX__ -DDEBUG

SYSTEM_INCLUDES := -mnewlib -isystem $(TOP)/sys/libc/include

# ISSUE: Do we really want the "-g"?
# ISSUE: Do we want "-ffunction-sections" and "--gc-sections"?
# ISSUE: Was using "-isystem $(TOP)/sys/libc".
CFLAGS := -g -Os $(CDEFS) -Wall -Werror -std=gnu99 \
  $(SYSTEM_INCLUDES) \
  $(INCLUDES) \
  -I$(TOP) \
  -I$(TOP)/sys/hv/include \
  -MD -MP
ASFLAGS = $(CFLAGS)


hvglue.S: mkgluesyms.pl $(TOP)/sys/hv/include/hv/hypervisor.h
	perl $^ > $@.tmp && $(MV) $@.tmp $@

tracefuncs.c: mktrace.py syscall.h
	python $< < $(filter %.h, $^) > $@.tmp && $(MV) $@.tmp $@

OBJS = hvglue.o tracefuncs.o tilegx/start.o mmap.o init.o tilegx/intvec.o \
  pa_allocator.o loader.o console.o syscall.o files.o devices.o \
  tokenizer.o argv.o messaging.o util.o debug.o interrupt.o

-include $(OBJS:%.o=%.d)

%.o: %.c
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(CFLAGS)

%.o: %.S
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(ASFLAGS)

bogux.ld: $(TOP)/sys/bogux/bogux.ld.S \
  $(TOP)/sys/bogux/tilegx/include/mem_layout.h $(TOP)/sys/bogux/bogux.h
	$(CC) -E -P -I $(TOP)/sys/hv/include \
	  -I $(TOP)/sys/bogux/tilegx/include \
	  -I $(TOP)/sys/bogux \
	  $< > $@.tmp && $(MV) $@.tmp $@

# ISSUE: What exactly is the point of the "grep"?
bogux: bogux.ld $(OBJS) $(LIBC_A)
	$(LD) -o $@ --script=$< $(OBJS) $(LIBC_A) $(LIBTILECC_A)
	$(NM) $@ | grep -q '00000000 d mem_l2_page_table_check'


# This sample just builds a "hello, world" user program.
# You can override it to run another program by overriding $(PROGRAM).

PROGRAM = hello

hello.c:
	echo '#include <stdio.h>' > $@.tmp
	echo 'int main() { printf("Hello, world.\n"); return 0; }' >> $@.tmp
	$(MV) $@.tmp $@

hello: hello.o
	$(CC) -static -o $@ $^


bogux-sim.hvc:
	echo 'client bogux' > $@.tmp
	echo '  args program' >> $@.tmp
	$(MV) $@.tmp $@

bogux-sim.bootrom: bogux-sim.hvc bogux $(PROGRAM)
	$(MKBOOT) -o $@.tmp --hv-bin-dir $(ROOT)/sys/hv \
	  --hvc $< bogux=bogux program=$(PROGRAM)
	$(MV) $@.tmp $@

all: bogux-sim.bootrom


ifeq ($(TILE_ARCH),tilegx)
VMLINUX_HVC := vmlinux.hvc
else
VMLINUX_HVC := vmlinux-pci.hvc
endif

# HACK: Create "bogux-pci.hvc" by slightly modifying the standard
# "vmlinux.hvc" to run "bogux" instead of "vmlinux".
bogux-pci.hvc: $(TILE_PREFIX)/etc/hvc/$(VMLINUX_HVC)
	sed '/client/,$$d' < $< >> $@.tmp
	echo "client bogux 8x7" >> $@.tmp
	echo "  args program" >> $@.tmp
	$(MV) $@.tmp $@

all: bogux-pci.hvc


# Run on PCI, using an hvc file.  ISSUE: This target "finishes" before
# the program has started running, and to see the program output, you
# must run "tile-console" in a separate window.
run_dev: bogux-pci.hvc
	$(TILERA_ROOT)/bin/tile-monitor --boot-only \
	  --hv-bin-dir $(ROOT)/sys/hv --no-vmlinux --hvc $< \
          --mkboot-args - bogux=bogux program=$(PROGRAM) -

# Run on simulator, using a bootrom file.  Note the use of "--wait"
# to wait for a shepherd greeting which will never actually occur,
# allowing this target to wait until the program finishes and the
# simulator shuts down.
run_sim: bogux-sim.bootrom
	$(TILERA_ROOT)/bin/tile-monitor --simulator --config 1x4 --functional \
	  --hv-bin-dir $(ROOT)/sys/hv --no-vmlinux --bootrom-file $< \
	  --console --wait


clean:
	$(RM) $(OBJS) hvglue.S tracefuncs.c bogux.ld bogux
	$(RM) hello hello.c bogux.hvc bogux.bootrom bogux-pci.hvc

.PHONY: all clean
