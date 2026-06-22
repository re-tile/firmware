# This is a -*- Makefile -*-

# Standalone makefile for L1 booter and hypervisor.

# Default target.
all:

#
# Support separate build and source trees; see comments in ../Makefile
# for usage instructions.
#
ifdef TOP
ROOT ?= /undefined-ROOT
VPATH = $(TOP)/sys/hv
else
TOP = ../..
ROOT = ../..
endif

include $(TOP)/sys/Makefile.inc

#
# Hypervisor protection level.
#
HV_PL = 3

BIN2C = $(TOP)/sys/hv/bin2c.py

LIBC_INCLUDE = $(TOP)/sys/libc/include
LIBC_A = $(ROOT)/sys/libc/libc_sys.a

COMMON_HVBME_INCLUDE = $(TOP)/sys/common_hvbme/include
COMMON_HVBME_ARCH_INCLUDE = $(TOP)/sys/common_hvbme/$(TILE_ARCH)/include
COMMON_HVBME_A = $(ROOT)/sys/common_hvbme/common_hvbme.a

LIBBZ2_INCLUDE = $(TOP)/sys/libbz2
LIBBZ2_A = $(ROOT)/sys/libbz2/libbz2.a

LIBTILECC := $(shell $(CC) -print-libgcc-file-name)

#
# These libs are used to build little-endian hv_l1boot and hv_lhboot.
# We run little-endian booter even on big-endian MDE.
#
ifdef TILE_BIG_ENDIAN
LIBC_A_LE = $(ROOT)/sys/libc/build-le/libc_sys.a
COMMON_HVBME_A_LE = $(ROOT)/sys/common_hvbme/build-le/common_hvbme.a
LIBTILECC_LE := \
  $(ROOT)/tools/gcc/host-le/build/tilegxbe-redhat-linux/libgcc/libgcc.a
else
LIBC_A_LE = $(LIBC_A)
COMMON_HVBME_A_LE = $(COMMON_HVBME_A)
LIBTILECC_LE := $(LIBTILECC)
endif

#
# This will accumulate a list of derived files, for use by the clean target.
#
DERIVED =

EXES = hv_lhboot hv_l1boot hv sromboot sromboot.bin sromboot.dbg
EXES += pci_preboot pci_preboot.bin pci_preboot.dbg

DEFS := -D__HV__ -DDEBUG $(EXTRA_DEFS)
ifneq ($(HV_PL),3)
DEFS += -DHV_PL=$(HV_PL)
endif


#
# Base address for the booter; the exact value of this isn't all that
# important, as long as it doesn't match a physical address which the
# hypervisor might be loaded at.  See comments in hv_l?boot_asm.S.
#
BOOT_BASEADDR = 0x80000000


#
# NOTE: You can define EXTRA_HV_CFLAGS for the hypervisor build.
#
# ISSUE: The "-I$(TOP)" allows direct use of "sys/libc/include/util.h".
#

#
# These include files are used when making generated .h files via gen_asm_func;
# we omit sys/libc since we need to use the real stdio when building natively.
#
GEN_INCFLAGS := \
  -I. \
  -I$(TOP) \
  -I$(TOP)/sys/hv/$(TILE_ARCH) \
  -I$(TOP)/sys/hv \
  -I$(TOP)/sys/hv/include \
  -I$(LIBBZ2_INCLUDE) \
  -I$(COMMON_HVBME_INCLUDE) \
  -I$(COMMON_HVBME_ARCH_INCLUDE) \
  $(EXTRA_INCLUDES)

GEN_FLAGS := $(GEN_INCFLAGS) -D__HV__ -Wno-packed-bitfield-compat

#
# These include files are used when building everything but generated
# .h files, and just add the use of sys/libc to what we use for the
# generated files.
#
INCFLAGS := \
  -isystem $(LIBC_INCLUDE) \
  $(GEN_INCFLAGS)

BASE_CFLAGS = \
  -mnewlib $(INCFLAGS) -ffunction-sections -MMD -MP -Wall -Wtype-limits \
  -Werror -Wno-packed-bitfield-compat -Wundef -g -std=gnu99

BASE_CFLAGS += -fdebug-prefix-map=$(TOP)=

HV_CFLAGS = $(BASE_CFLAGS) $(EXTRA_HV_CFLAGS)

BOOT_CFLAGS = $(BASE_CFLAGS) -DL1BOOT -DBOOT_BASEADDR=$(BOOT_BASEADDR) -Os
ifdef TILE_BIG_ENDIAN
BOOT_CFLAGS += -mlittle-endian
endif

obj = $(addsuffix .o,$(basename $(1)))

#
# LH ("half") booter build
#

LHBOOT_SOURCES := \
  $(TILE_UARCH)/hv_lhboot_asm.S

LHBOOT_OBJS := $(call obj, $(LHBOOT_SOURCES))

$(call obj,$(filter %.c, $(LHBOOT_SOURCES))): %.o: %.c
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

$(call obj,$(filter %.S, $(LHBOOT_SOURCES))): %.o: %.S
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

LHBOOT_LD_SCRIPT := hv_lhboot.ld.out

-include $(LHBOOT_OBJS:.o=.d)


$(LHBOOT_LD_SCRIPT): $(TOP)/sys/hv/hv_lhboot.ld
	set -e; \
	mkdir -p $(@D); \
	sed -e s/@BASE@/$(BOOT_BASEADDR)+0x200/ <$< >$@.tmp;
	$(MV) $@.tmp $@

hv_lhboot: $(LHBOOT_OBJS) $(LHBOOT_LD_SCRIPT)
	set -e; \
	mkdir -p $(@D); \
	$(LD) -EL -o $@.tmp --script=$(LHBOOT_LD_SCRIPT) \
	  --gc-sections $(LHBOOT_OBJS); \
	$(MV) $@.tmp $@

DERIVED += $(LHBOOT_OBJS) $(LHBOOT_OBJS:.o=.d) $(LHBOOT_LD_SCRIPT)

#
# L1 booter build
#

L1BOOT_SOURCES := \
  $(TILE_UARCH)/hv_l1boot_asm.S \
  $(TILE_ARCH)/hv_l1boot.c \
  $(TILE_ARCH)/post/post_ram_l1boot_asm.S \
  $(TILE_ARCH)/post/post_ram_l1boot.c \
  $(TILE_ARCH)/post/post_ram_quick.c \
  boot_printf.c \
  tilegx/mshim_cfg.c

SHARED_SOURCES := \
  $(EXTRA_SHARED_SOURCES) \
  $(EXTRA_SROM_SOURCES) \
  board_info.c \
  boot_error.c \
  $(TILE_ARCH)/cfg.c \
  $(TILE_ARCH)/gpio_acc.c \
  i2c_acc.c \
  $(TILE_ARCH)/idn.c \
  $(TILE_ARCH)/mshim_acc.c \
  $(TILE_ARCH)/physacc_asm.S \
  sig.c \
  srom_acc.c \
  srom_table.c \
  tilegx/cons_rshim.c \
  tilegx/hw_config.c

l1boot_obj = $(addsuffix _l1boot.o,$(basename $(1)))

L1BOOT_OBJS=$(foreach source,$(L1BOOT_SOURCES),$(call obj,$(source)))
L1BOOT_SHARED_OBJS=\
  $(foreach source,$(SHARED_SOURCES),$(call l1boot_obj,$(source)))


GEN_DEPS := srom_asm.h

$(call obj,$(filter %.c, $(L1BOOT_SOURCES))): %.o: %.c
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

$(call obj,$(filter %.S, $(L1BOOT_SOURCES))): %.o: %.S $(GEN_DEPS)
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

$(call l1boot_obj,$(filter %.c, $(SHARED_SOURCES))): %_l1boot.o: %.c
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

$(call l1boot_obj,$(filter %.S, $(SHARED_SOURCES))): %_l1boot.o: %.S $(GEN_DEPS)
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

L1BOOT_LD_SCRIPT := hv_l1boot.ld.out

-include $(L1BOOT_OBJS:.o=.d) $(L1BOOT_SHARED_OBJS:.o=.d)

$(L1BOOT_LD_SCRIPT): $(TOP)/sys/hv/hv_l1boot.ld hv_lhboot
	set -e; \
	mkdir -p $(@D); \
	lhstart=`$(NM) hv_lhboot | awk '$$3 == "_lh_start" {print $$1}'`; \
	lhend=`$(NM) hv_lhboot | awk '$$3 == "_lh_end" {print $$1}'`; \
	sed -e s/@BASE@/0x$$lhend/ \
	    -e s/@LHSTART@/$$lhstart/ -e s/@LHEND@/$$lhend/ \
	  <$< >$@.tmp;
	$(MV) $@.tmp $@

hv_l1boot: $(L1BOOT_OBJS) $(L1BOOT_SHARED_OBJS) $(L1BOOT_LD_SCRIPT) \
  $(LIBC_A_LE) $(LIBTILECC_LE) $(COMMON_HVBME_A_LE)
	set -e; \
	mkdir -p $(@D); \
	$(LD) -EL -o $@.tmp --script=$(L1BOOT_LD_SCRIPT) --gc-sections \
	  $(L1BOOT_OBJS) $(L1BOOT_SHARED_OBJS) \
	  --start-group $(LIBC_A_LE) $(LIBTILECC_LE) $(COMMON_HVBME_A_LE) \
	  --end-group; \
	$(MV) $@.tmp $@


DERIVED += $(L1BOOT_OBJS) $(L1BOOT_SHARED_OBJS) \
  $(L1BOOT_OBJS:.o=.d) $(L1BOOT_SHARED_OBJS:.o=.d) \
  $(L1BOOT_LD_SCRIPT)

#
# SROM booter build
#

SROMBOOT_SOURCES := \
  $(TILE_ARCH)/sromboot_asm.S 

SROMBOOT_OBJS := \
  $(call obj, $(SROMBOOT_SOURCES)) \
  $(call l1boot_obj, srom_table.c $(EXTRA_SROM_SOURCES))

$(call obj,$(filter %.c, $(SROMBOOT_SOURCES))): %.o: %.c
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

$(call obj,$(filter %.S, $(SROMBOOT_SOURCES))): %.o: %.S srom_asm.h
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

SROMBOOT_LD_SCRIPT := sromboot.ld.out

-include $(SROMBOOT_OBJS:.o=.d)

$(SROMBOOT_LD_SCRIPT): $(TOP)/sys/hv/$(TILE_ARCH)/sromboot.ld hv_lhboot
	set -e; \
	mkdir -p $(@D); \
	lhstart=`$(NM) hv_lhboot | awk '$$3 == "_lh_start" {print $$1}'`; \
	lhend=`$(NM) hv_lhboot | awk '$$3 == "_lh_end" {print $$1}'`; \
	sed -e "s/@BASE@/$(BOOT_BASEADDR)/" \
	    -e "s/@LHSTART@/$$lhstart/" -e "s/@LHEND@/$$lhend/" \
	  <$< >$@.tmp;
	$(MV) $@.tmp $@

sromboot: $(SROMBOOT_OBJS) $(SROMBOOT_LD_SCRIPT)
	set -e; \
	mkdir -p $(@D); \
	$(LD) -EL -o $@.tmp  --script=$(SROMBOOT_LD_SCRIPT) --gc-sections \
	  $(SROMBOOT_OBJS); \
	$(MV) $@.tmp $@

sromboot.bin: hv_lhboot sromboot $(MKROM)
	set -e; \
	$(MKROM) -n -H -b sromboot -o $@.tmp_intresult; \
	$(MKROM) -C -n -b hv_lhboot -o $@.tmp_result $@.tmp_intresult; \
	$(MV) $@.tmp_result $@; \
	$(RM) $@.tmp*

sromboot.dbg: sromboot
	set -e; \
	$(OBJCOPY) --only-keep-debug sromboot $@.tmp_result; \
	$(MV) $@.tmp_result $@; \
	$(RM) $@.tmp*

DERIVED += $(SROMBOOT_OBJS) $(SROMBOOT_OBJS:.o=.d) $(SROMBOOT_LD_SCRIPT)

#
# PCIe prebooter build
#

BOOT_BASEADDR = 0x80000000

# Files only used by the prebooter
PCI_PREBOOT_SOURCES := \
  $(TILE_ARCH)/pci_preboot.c \
  $(TILE_ARCH)/pci_preboot_asm.S

# Files shared between the prebooter and the L1 booter
PCI_PREBOOT_L1_SOURCES := \
  $(TILE_UARCH)/hv_l1boot_asm.S \
  boot_printf.c

# Files shared between the prebooter, the L1 booter, and the hypervisor
PCI_PRE_SHARED_SOURCES := \
  $(EXTRA_PCI_PRE_SHARED_SOURCES) \
  boot_error.c \
  $(TILE_ARCH)/cfg.c \
  $(TILE_ARCH)/idn.c \
  $(TILE_ARCH)/physacc_asm.S

PCI_PREBOOT_OBJS= \
  $(foreach source,$(PCI_PREBOOT_SOURCES),$(call obj,$(source))) \
  $(foreach source,$(PCI_PREBOOT_L1_SOURCES),$(call obj,$(source))) \
  $(foreach source,$(PCI_PRE_SHARED_SOURCES),$(call l1boot_obj,$(source)))

$(call obj,$(filter %.c, $(PCI_PREBOOT_SOURCES))): %.o: %.c
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

$(call obj,$(filter %.S, $(PCI_PREBOOT_SOURCES))): %.o: %.S
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c -o $@ $< $(BOOT_CFLAGS) $(DEFS)

PCI_PREBOOT_LD_SCRIPT := pci_preboot.ld.out

-include $(PCI_PREBOOT_OBJS:.o=.d)

$(PCI_PREBOOT_LD_SCRIPT): $(TOP)/sys/hv/hv_l1boot.ld hv_lhboot
	set -e; \
	mkdir -p $(@D); \
	lhstart=`$(NM) hv_lhboot | awk '$$3 == "_lh_start" {print $$1}'`; \
	lhend=`$(NM) hv_lhboot | awk '$$3 == "_lh_end" {print $$1}'`; \
	sed -e "s/@BASE@/$(BOOT_BASEADDR) + 0x8000/" \
	    -e "s/@LHSTART@/$$lhstart/" -e "s/@LHEND@/$$lhend/" \
	  <$< >$@.tmp;
	$(MV) $@.tmp $@

#
# The Gx prebooter needs the SERDES JTAG stuff from common_hvbme.
#
PCI_PREBOOT_LIBS := $(COMMON_HVBME_A_LE)

pci_preboot: $(PCI_PREBOOT_OBJS) $(PCI_PREBOOT_LD_SCRIPT) \
  $(LIBC_A_LE) $(LIBTILECC_LE) $(PCI_PREBOOT_LIBS)
	set -e; \
	mkdir -p $(@D); \
	$(LD) -EL -o $@.tmp --script=$(PCI_PREBOOT_LD_SCRIPT) --gc-sections \
	  $(PCI_PREBOOT_OBJS) --start-group $(LIBC_A_LE) $(LIBTILECC_LE) \
	  $(PCI_PREBOOT_LIBS) --end-group; \
	$(MV) $@.tmp $@

PCI_PREBOOT_MKROM_OPTS := -n


pci_preboot.bin: hv_lhboot pci_preboot $(MKROM)
	set -e; \
	$(MKROM) $(PCI_PREBOOT_MKROM_OPTS) -H -b pci_preboot \
	  -o $@.tmp_intresult; \
	$(MKROM) -C -n -b hv_lhboot -o $@.tmp_result $@.tmp_intresult; \
	$(MV) $@.tmp_result $@; \
	$(RM) $@.tmp*

pci_preboot.dbg: pci_preboot
	set -e; \
	$(OBJCOPY) --only-keep-debug pci_preboot $@.tmp_result; \
	$(MV) $@.tmp_result $@; \
	$(RM) $@.tmp*

DERIVED += $(PCI_PREBOOT_OBJS) $(PCI_PREBOOT_OBJS:.o=.d) \
  $(PCI_PREBOOT_LD_SCRIPT)


#
# Hypervisor glue build
#

# Create assembly constants for the glue.

$(call gen_asm_func, \
  gen_glue_asm.c, \
  glue_asm.h, \
  )

glue.o: $(TILE_ARCH)/glue.S include/hv/hypervisor.h syscall.h glue_asm.h
	set -e; \
	$(RM) $@.tmp.*; \
	tmpfile=$@.tmp.$$$$; \
	$(CC) -P -E $(INCFLAGS) $(DEFS) $< > $$tmpfile; \
	$(AS) $$tmpfile -o $@; \
	$(RM) $$tmpfile

glue: glue.o
	set -e; \
	mkdir -p $(@D); \
	$(LD) -static -e 0 --oformat binary --fatal-warnings $< -o $@.tmp; \
	$(MV) $@.tmp $@

glue_data.c: glue
	set -e; \
	$(BIN2C) glue <glue >glue_data.c.tmp; \
	$(MV) glue_data.c.tmp glue_data.c

glue_data.o: glue_data.c
	$(CC) -c -o $@ $<

DERIVED += glue.o glue glue_data.c glue_data.o


#
# Hypervisor build
#
# NOTE: You can use "EXTRA_SOURCES" to add extra source files.
#

# Some of the driver sources are built using an RPC generator.

GEN_IORPC := $(TOP)/sys/hv/iorpc/gen_rpc.py
GEN_IORPC_DEPS := $(addprefix iorpc/, \
  emit_c.py \
  gen_rpc.py \
  parse.py \
  token.py \
  )
IORPC_SOURCES :=

# Generate iorpc sources with output path prefix $(1) from IDL files $(2).
define gen_iorpc_eval_func

$(1)_rpc_stamp: $(2) $$(GEN_IORPC_DEPS)
	set -e; \
	$(RM) $$@ $(1)_rpc_dispatch.c $(1)_rpc_dispatch.h; \
	mkdir -p $$(@D); \
	$$(GEN_IORPC) -o $(1)_rpc_dispatch --target hv $$(filter %.idl,$$^); \
	touch $$@

$(1)_rpc_dispatch.h: $(1)_rpc_stamp
	@$(EMPTY_COMMAND)

$(1)_rpc_dispatch.c: $(1)_rpc_stamp
	@$(EMPTY_COMMAND)

IORPC_SOURCES += $(1)_rpc_dispatch.c
IORPC_HEADERS += $(1)_rpc_dispatch.h

DERIVED += $(1)_rpc_stamp $(1)_rpc_dispatch.h $(1)_rpc_dispatch.c

endef

gen_iorpc = $(eval $(call gen_iorpc_eval_func,$(1),$(2)))

IORPC_GLOBALS := tilegx/iorpc/globals.idl
$(call gen_iorpc, drivers/test/test, iorpc/test.idl)
$(call gen_iorpc, tilegx/drivers/gpio/gpio, \
	          tilegx/iorpc/gpio.idl tilegx/iorpc/pollfd.idl \
		  $(IORPC_GLOBALS))
$(call gen_iorpc, tilegx/drivers/i2cs/i2cs, \
	          tilegx/iorpc/i2cs.idl $(IORPC_GLOBALS))
$(call gen_iorpc, tilegx/drivers/mica/mica, \
	          tilegx/iorpc/mica.idl $(IORPC_GLOBALS))
$(call gen_iorpc, tilegx/drivers/mpipe/mpipe, \
	          tilegx/iorpc/mpipe.idl tilegx/iorpc/pollfd.idl \
		  $(IORPC_GLOBALS))
$(call gen_iorpc, tilegx/drivers/mpipe/mpipe_info, \
	          tilegx/iorpc/mpipe_info.idl $(IORPC_GLOBALS))
$(call gen_iorpc, tilegx/drivers/mica/pka, \
	          tilegx/iorpc/pka.idl $(IORPC_GLOBALS))
$(call gen_iorpc, tilegx/drivers/trio/trio, \
	          tilegx/iorpc/trio.idl $(IORPC_GLOBALS))
$(call gen_iorpc, tilegx/drivers/uart/uart, \
	          tilegx/iorpc/uart.idl $(IORPC_GLOBALS))
$(call gen_iorpc, tilegx/drivers/usb_dev/usb_dev, \
	          tilegx/iorpc/usb_dev.idl $(IORPC_GLOBALS))
$(call gen_iorpc, tilegx/drivers/usb_host/usb_host, \
	          tilegx/iorpc/usb_host.idl $(IORPC_GLOBALS))

# The LINUX_INDENT_FLAGS are documented as "--linux-style" in indent 2.2.10.
# That option is not present in version 2.2.9, which we still need to support.
LINUX_TYPES := \
  int8_t \
  int16_t \
  int32_t \
  int64_t \
  uint8_t \
  uint16_t \
  uint32_t \
  uint64_t \
  HV_PTE \
  __gxio_dma_queue_t \
  _gxio_mpipe_link_name_t \
  _gxio_mpipe_link_mac_t \
  gxio_mpipe_context_t \
  gxio_mpipe_edesc_t \
  gxio_mpipe_equeue_t \
  gxio_mpipe_idesc_t \
  gxio_mpipe_info_context_t \
  gxio_mpipe_iqueue_t \
  gxio_mpipe_link_t \
  gxio_mpipe_notif_group_bits_t \
  gxio_mpipe_rules_t \
  gxio_mpipe_rules_stacks_t \
  gxio_trio_dma_queue_t \
  gxio_trio_context_t \
  gxio_uart_context_t \
  gxio_usb_host_context_t \

LINUX_INDENT_FLAGS := $(patsubst %,-T%,$(LINUX_TYPES)) \
  -nbad -bap -nbc -bbo -hnl -br -brs \
  -ncdb -ce -ci4 -cli0 -d0 -di1 -nfc1 -i8 -ip0 -l80 -lp \
  -npcs -nprs -npsl -sai -saf -saw -ncs -nsc -sob -nfca -ss -ts8
IORPC_TYPES := gpio i2cs mica mpipe test mpipe_info pka trio uart \
  usb_dev usb_host globals

# Generate iorpc sources for linux.  This code should really be in the Linux
# build system, but that environment doesn't support Python so we include the
# rule here so that developers don't have to generate these files manually.
#
# Generating the test driver code isn't really necessary, but we
# include it for now because it's a good test of the IDL parser.  It
# will likely be removed once the other drivers have good coverage of
# all the iorpc features.
.PHONY: linux_iorpc
linux_iorpc:
	set -e; \
	mkdir -p $(LINUX_IORPC_DIR); \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_gpio \
	  --target linux --use-context-obj \
	  $(TOP)/sys/hv/tilegx/iorpc/gpio.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/pollfd.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
        $(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_i2cs \
          --target linux --use-context-obj \
          $(TOP)/sys/hv/tilegx/iorpc/i2cs.idl \
          $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_mica --target linux \
	  $(TOP)/sys/hv/tilegx/iorpc/mica.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_mpipe \
	  --target linux --use-context-obj \
	  $(TOP)/sys/hv/tilegx/iorpc/mpipe.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/pollfd.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_test --target linux \
	  $(TOP)/sys/hv/iorpc/test.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_mpipe_info \
	  --target linux --use-context-obj \
	  $(TOP)/sys/hv/tilegx/iorpc/mpipe_info.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_pka \
	  --target linux --use-context-obj \
	  $(TOP)/sys/hv/tilegx/iorpc/pka.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_trio \
	  --target linux --use-context-obj \
	  $(TOP)/sys/hv/tilegx/iorpc/trio.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_uart \
	  --target linux --use-context-obj \
	  $(TOP)/sys/hv/tilegx/iorpc/uart.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_usb_dev \
	  --target linux --use-context-obj \
	  $(TOP)/sys/hv/tilegx/iorpc/usb_dev.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_usb_host \
	  --target linux --use-context-obj \
	  $(TOP)/sys/hv/tilegx/iorpc/usb_host.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	$(GEN_IORPC) -o $(LINUX_IORPC_DIR)/iorpc_globals --target linux \
	  $(TOP)/sys/hv/iorpc/linux.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/pollfd.idl \
	  $(TOP)/sys/hv/tilegx/iorpc/globals.idl; \
	indent $(LINUX_INDENT_FLAGS) \
	  $(patsubst %,$(LINUX_IORPC_DIR)/iorpc_%.c,$(IORPC_TYPES)) \
	  $(patsubst %,$(LINUX_IORPC_DIR)/iorpc_%.h,$(IORPC_TYPES)); \
	$(RM) $(patsubst %,$(LINUX_IORPC_DIR)/iorpc_%.?~,$(IORPC_TYPES)); \


HV_SOURCES := \
  $(SHARED_SOURCES) \
  $(EXTRA_SOURCES) \
  $(IORPC_SOURCES) \
  bme.c \
  client.c \
  client_msg.c \
  client_obj.c \
  config.c \
  console.c \
  cons_client.c \
  cons_null.c \
  cons_remote.c \
  cons_uart.c \
  debug.c \
  devices.c \
  $(TILE_ARCH)/device_table.c \
  downcall.c \
  $(TILE_ARCH)/drivers/gpio/gpio.c \
  drivers/i2cm/i2cm.c \
  $(TILE_ARCH)/drivers/mshim/mshim.c \
  drivers/memprof/memprof.c \
  drivers/rshim/rshim.c \
  drivers/srom/srom.c \
  drivers/srom/srom_mtd.c \
  drivers/test/test.c \
  $(TILE_ARCH)/drivers/test/test_asm.S \
  drvintf.c \
  exit.c \
  fault.c \
  filesys.c \
  hv.c \
  $(TILE_ARCH)/intvec.S \
  loader.c \
  lock.c \
  mapping.c \
  $(TILE_ARCH)/misc.c \
  $(TILE_ARCH)/misc_asm.S \
  msg.c \
  nap.c \
  nap_asm.S \
  $(TILE_ARCH)/physacc.c \
  post/post_ram_hv.c \
  rtc.c \
  $(TILE_ARCH)/start.S \
  temp_sens.c \
  temp_sens_lm95235.c \
  temp_sens_max6639.c \
  tile.c \
  tile_mask.c \
  $(TILE_ARCH)/tlb.c \
  $(TILE_ARCH)/tsb.c \
  drivers/test/iorpc_test.c \
  tilegx/cons_switch.c \
  tilegx/drivers/corepll/corepll.c \
  tilegx/drivers/i2cs/i2cs.c \
  tilegx/drivers/memprof/memprof_asm.S \
  tilegx/drivers/mica/mica.c \
  tilegx/drivers/mpipe/classifier.c \
  tilegx/drivers/mpipe/link.c \
  tilegx/drivers/mpipe/mpipe.c \
  tilegx/drivers/mpipe/rules.c \
  tilegx/drivers/sensor/sensor.c \
  tilegx/drivers/tmfifo/tmfifo.c \
  tilegx/drivers/trio/trio.c \
  tilegx/drivers/uart/uart.c \
  tilegx/drivers/usb_dev/usb_dev.c \
  tilegx/drivers/usb_host/usb_host.c \
  tilegx/drivers/watchdog/watchdog.c \
  tilegx/drvintf_mmio.c \
  tilegx/tilegx_stubs.c \

# Create assembly constants for downcall assembly stuff.

$(call gen_asm_func, \
  gen_downcall_asm.c, \
  downcall_asm.h, \
  )

# Create assembly constants for the SROM booter and the assembly SROM
# access code.

$(call gen_asm_func, \
  gen_srom_asm.c, \
  srom_asm.h, \
  )

HV_OBJS = $(foreach source,$(HV_SOURCES),$(call obj,$(source)))

# Function to compile a file.
#
# $(1): Object file name
# $(2): Source file name
# $(3): Extra compiler flags
#

define _compile_eval_func

$(1): $(2) downcall_asm.h srom_asm.h $$(IORPC_HEADERS)
	set -e; \
	mkdir -p $$(@D); \
	$(CC) -c -o $$@ $$< $(HV_CFLAGS) -I$$(dir $(2)) $(3) $(DEFS)

endef

compile = $(eval $(call _compile_eval_func,$(1),$(2),$(3)))

# Compile the source files.
$(foreach source, $(HV_SOURCES), \
  $(call compile, $(call obj,$(source)), $(source), -Os))

-include $(HV_OBJS:.o=.d)

HV_LD_SCRIPT := hv.ld.out

#
# We need to form virtual addresses based on HV_PL in the linker script, in
# contexts where the linker won't let you do arithmetic.  So, instead, we
# do the arithmetic here, and replace @HV_BASE@ with the top 8 bits of the
# hypervisor VA.  (This is only relevant for TILEPro, but it doesn't hurt
# to do it on TILE-Gx, it's just a no-op there.)
#
ifeq ($(HV_PL),2)
__HV_BASE := FE
else
__HV_BASE := FF
endif

$(HV_LD_SCRIPT): $(TOP)/sys/hv/$(TILE_ARCH)/hv.ld
	set -e; \
	mkdir -p $(@D); \
	sed -e s/@HV_BASE@/$(__HV_BASE)/ <$< >$@.tmp;
	$(MV) $@.tmp $@

hv: version.c.in $(call obj, $(HV_SOURCES)) \
    $(HV_LD_SCRIPT) glue_data.o $(LIBC_A) \
    $(LIBBZ2_A) $(LIBTILECC) \
	$(COMMON_HVBME_A)
	set -e; \
	date="`date '+%Y-%m-%d %H:%M:%S'`"; \
	test -n "$$date"; \
	sed "s/@DATE@/$$date/" < $< > version.c; \
	$(CC) -c -o version.o version.c; \
	$(LD) -o $@.tmp --script=$(HV_LD_SCRIPT) -static \
	  --gc-sections \
	  $(call obj, $(HV_SOURCES)) \
	  glue_data.o version.o \
	  --start-group $(LIBC_A) $(LIBBZ2_A) $(LIBTILECC) \
	  $(COMMON_HVBME_A) --end-group
	$(MV) $@.tmp $@

DERIVED += $(HV_OBJS) $(HV_OBJS:.o=.d) version.c version.o $(HV_LD_SCRIPT)

all: $(EXES)

clean:
	$(RM) $(DERIVED) $(EXES)

docs:
	set -e; \
	cd docs; \
	doxygen config.doxy

.PHONY: all clean docs
