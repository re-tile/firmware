# This is a -*- Makefile -*-

#
# Common definitions for Makefiles under src/sys/.
#

__ARCH := $(shell uname -p)
NATIVE = $(if $(filter tile%,$(__ARCH)),1,0)

ifeq ($(NATIVE),1)
#
# Native build definitions.
#

#
# We don't install chip.mk on a Tile processor, so we dynamically calculate the
# variables it normally defines.
#

#
# TILE_ARCH is just the output of uname -p from above.
#
TILE_ARCH = $(__ARCH)
TILE_UARCH = $(__ARCH)

#
# We use a table to change architecture names into chip numbers, which
# are still used when building some host-side tools.
#
__ARCH2CHIP := tilegx:10 tilepro:1
TILE_CHIP = $(patsubst $(__ARCH):%,%,$(filter $(__ARCH):%,$(__ARCH2CHIP)))

#
# We see how the compiler was built to determine if we're big-endian or not.
#
ifeq ($(filter MSB, $(shell file /usr/bin/gcc)),MSB)
TILE_BIG_ENDIAN = 1
endif

CC = cc
AS = as
AR = ar
LD = ld
NM = nm
MPIPE_CC = tile-mpipe-cc
OBJCOPY = objcopy
MONITOR = tile-monitor
MKROM = /usr/sbin/tile-mkrom
MKBOOT = /usr/sbin/tile-mkboot
TILE_PREFIX =

else
#
# Cross-build definitions.
#

ifndef TILERA_ROOT
$(error TILERA_ROOT environment variable not set)
endif

include $(TILERA_ROOT)/etc/chip.mk

CC = $(TILERA_ROOT)/bin/tile-gcc
AS = $(TILERA_ROOT)/bin/tile-as
AR = $(TILERA_ROOT)/bin/tile-ar
LD = $(TILERA_ROOT)/bin/tile-ld
NM = $(TILERA_ROOT)/bin/tile-nm
MPIPE_CC = $(TILERA_ROOT)/bin/tile-mpipe-cc
OBJCOPY = $(TILERA_ROOT)/bin/tile-objcopy
MONITOR = $(TILERA_ROOT)/bin/tile-monitor
MKROM = $(TILERA_ROOT)/bin/tile-mkrom
MKBOOT = $(TILERA_ROOT)/bin/tile-mkboot
TILE_PREFIX = $(TILERA_ROOT)/tile/

endif

MV = mv -f

#
# These functions create an include file, for use with assembly-language code,
# which describes certain attributes of a C data structure.
#
# The way this works is that a generator .c file #includes the data structure
# definitions from various .h files, then includes sys/hv/asmhdr.h, which
# defines a bunch of generator macros; see that file for more details on the
# macros.  It then invokes the macros for each data structure and structure
# member which we need constants generated for; the macros arrange for
# appropriate definitions to be emitted.
#
# We must compile the generator into a Tile executable, since we care about the
# structure layout produced by the Tile compiler.  Thus, how we build and run
# it depends upon how we are doing our overall build, as follows:
#
# - For native builds, we've already got a working OS stack, which we're
#   running on.  Thus, we compile with -DNATIVE, which makes the macros invoke
#   stdio routines, and we just run the resulting binary directly; the
#   constants are produced on stdout.
#
# - For cross builds, we don't have a working OS stack. Thus, we compile
#   without -DNATIVE, which makes the macros invoke special routines that do
#   output via magic simulator SPRs, and we run the binary under the simulator,
#   in a special mode which initializes just enough of the hardware for our
#   program to function; the constants are produced on stdout.
#
# Arguments to _gen_asm_eval_func:
#
# $(1): Relative pathname of generator .c file
# $(2): Relative pathname of output .h file 
# $(3): Extra dependencies
#
# $(GEN_FLAGS): flags to be used when compiling generators
#

ifeq ($(NATIVE),1)
#
# Native version
#

define _gen_asm_eval_func

$(basename $(1)): $(1) $(3)
	set -e; \
	mkdir -p $$(@D); \
	$$(CC) -DNATIVE -O2 -std=gnu99 -MMD -MP -o $$@ \
	  $$(GEN_FLAGS) $$< -Wl,--gc-sections

-include $(basename $(1)).d

$(2): $(basename $(1))
	set -e; \
	mkdir -p $$(@D); \
	$(abspath $$<) > $(2).tmp; \
	$(MV) $(2).tmp $(2); \
	touch $$@

DERIVED += $(basename $(1)) $(basename $(1)).d $(2)

endef

else
#
# Cross version
#

SIM = $(TILERA_ROOT)/bin/tile-sim

define _gen_asm_eval_func

$(basename $(1)): $(1) $(3)
	set -e; \
	mkdir -p $$(@D); \
	$$(CC) -O2 -std=gnu99 -static -nostartfiles -nostdlib -MMD -MP -o $$@ \
	  $$(GEN_FLAGS) $$< -Wl,--gc-sections $$(LIBTILECC)

-include $(basename $(1)).d

$(2): $(basename $(1))
	set -e; \
	mkdir -p $$(@D); \
	$$(SIM) --config 1x1 --pa-is-va --loader-pa-is-va \
	  $(if $(TILE_BIG_ENDIAN),--big-endian,) \
	  $$< > $(2).tmp; \
	$(MV) $(2).tmp $(2); \
	touch $$@

DERIVED += $(basename $(1)) $(basename $(1)).d $(2)

endef

endif

gen_asm_func = \
  $(eval $(call _gen_asm_eval_func,$(1),$(2),$(3)))
