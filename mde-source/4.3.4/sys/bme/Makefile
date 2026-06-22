# This is a -*- Makefile -*-

# Standalone Makefile for the BME libc.

# Default target.
all:

ifdef FROM
VPATH = $(FROM)
else
FROM = .
endif

ifndef ROOT
ROOT = ../..
endif

include $(FROM)/../Makefile.inc

LIBC_SYS_A = $(ROOT)/sys/libc/libc_sys.a

COMMON_HVBME_A = $(ROOT)/sys/common_hvbme/common_hvbme.a

LIBC_BME_A = libc.a

LIBTILECC := $(shell $(CC) -print-libgcc-file-name)

obj = $(addsuffix .o,$(basename $(1)))

#
# These include files are used when making generated .h files via gen_asm_func;
# we omit -mbme since we need to use the real stdio when building natively.
#
GEN_INCFLAGS := -I. -I $(FROM)/$(TILE_ARCH) -I$(FROM) -I $(FROM)/../hv/include

GEN_FLAGS := $(GEN_INCFLAGS)

#
# These include files are used when building everything but generated .h files,
# and just add the use of -mbme to what we use for the generated files.
#
INCFLAGS := -mbme $(GEN_INCFLAGS)

# Create assembly constants for the interrupt vector replacement code.

$(call gen_asm_func, \
  gen_interrupts_asm.c, \
  interrupts_asm.h, \
  )

SYS_COMMON_HVBME_OBJS := \
  board_info.o \
  board_info_common.o \
  serdes.o \
  xgbe.o \
  xgbe_link.o \

SYS_LIBC_EXTRACTED_OBJS := \
  abs.o \
  bsearch.o \
  ctype_.o \
  div.o \
  ffs.o \
  labs.o \
  ldiv.o \
  memccpy.o \
  memchr.o \
  memcmp.o \
  memcpy.o \
  memmove.o \
  mempcpy.o \
  memset.o \
  qsort.o \
  setjmp.o \
  str2l.o \
  strcasecmp.o \
  strcat.o \
  strchr.o \
  strcmp.o \
  strcoll.o \
  strcpy.o \
  strcspn.o \
  strlcat.o \
  strlcpy.o \
  strlen.o \
  strlwr.o \
  strncasecmp.o \
  strncat.o \
  strncmp.o \
  strncpy.o \
  strnlen.o \
  strpbrk.o \
  strrchr.o \
  strspn.o \
  strstr.o \
  strupr.o \
  strxfrm.o \
  swab.o


BME_SOURCES := \
  bme_start.c \
  hv_msg.c \
  bme_malloc.c \
  misc.c \
  stdio_hvcons.c \
  stdio_null.c \
  stdio_sim.c \
  sys_info.c \
  tokenizer.c \
  $(TILE_ARCH)/cfg.c \
  $(TILE_ARCH)/crt1.S \
  $(TILE_ARCH)/crti.S \
  $(TILE_ARCH)/crtn.S \
  $(TILE_ARCH)/interrupts.c \
  $(TILE_ARCH)/intvec.S \
  $(TILE_ARCH)/misc_asm.S \
  $(TILE_ARCH)/tlb.c \
  $(TILE_ARCH)/mem.c


ifeq (0,1)
# These files are not yet supported on TILE-Gx
BME_SOURCES += \
  addr_alloc.c \
  dtlb.c \
  pa_alloc.c \
  va_alloc.c 
endif

BME_SOURCES += \
  tmc_mspace.c \
  tmc_spin.c \

LIBC_SOURCES := \
  panic.c \
  printf.c \
  stdio.c \

BME_OBJECTS := \
  $(call obj, $(BME_SOURCES) $(LIBC_SOURCES))

BME_OBJS := \
  $(SYS_LIBC_EXTRACTED_OBJS) \
  $(SYS_COMMON_HVBME_OBJS) \
  $(BME_OBJECTS)

%.o: %.c
	mkdir -p $(@D); \
	$(CC) -c $(INCFLAGS) -ffunction-sections \
	  -g -Os -std=gnu99 -MD -MP -Wall -Werror $< -o $@

%.o: ../libc/%.c
	$(CC) -c $(INCFLAGS) -ffunction-sections \
	  -g -Os -std=gnu99 -MD -MP -Wall -Werror $<

%.o: %.S interrupts_asm.h
	mkdir -p $(@D); \
	$(CC) -c $(INCFLAGS) -ffunction-sections \
	  -g -MD -MP -Wall -Werror $< -o $@

-include $(BME_OBJECTS:%.o=%.d)

$(SYS_LIBC_EXTRACTED_OBJS): $(LIBC_SYS_A)
	$(AR) p $< $(@F) > $@.tmp && $(MV) $@.tmp $@

$(SYS_COMMON_HVBME_OBJS): $(COMMON_HVBME_A)
	$(AR) p $< $(@F) > $@.tmp && $(MV) $@.tmp $@

$(LIBC_BME_A): $(BME_OBJS)
	set -e; \
	$(RM) $@; \
	$(AR) cq $@ $(sort $(BME_OBJS))

all: $(LIBC_BME_A)

clean:
	$(RM) *.o *.d $(LIBC_BME_A) $(TILE_ARCH)/*.o $(TILE_ARCH)/*.d

.PHONY: all clean
