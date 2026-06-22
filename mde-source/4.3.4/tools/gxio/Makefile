# This is a -*- Makefile -*-

# Default target.
all:

ifdef TILERA_ROOT

CC = $(TILERA_ROOT)/bin/tile-gcc
AR = $(TILERA_ROOT)/bin/tile-ar

# For "TILE_ARCH".
include $(TILERA_ROOT)/etc/chip.mk

else

CC = gcc
AR = ar

TILE_ARCH := $(shell uname -m)

ifeq ($(filter tile%,$(TILE_ARCH)),)
$(error The 'TILERA_ROOT' environment variable is not set.)
endif

endif

# Support separate build and source trees.
ifdef TOP
VPATH = $(TOP)/tools/gxio/source
else
TOP = ../..
VPATH = source
endif

HV_SRC_DIR := $(TOP)/sys/hv
RPCGEN := $(HV_SRC_DIR)/iorpc/gen_rpc.py
RPCGEN_DEPS := $(addprefix $(HV_SRC_DIR)/iorpc/, \
  emit_c.py \
  gen_rpc.py \
  parse.py \
  token.py \
  )

EMPTY_COMMAND :=

# Generate sources given an IDL file basename and the HV's IDL files
define rpcgen_eval_func

stamp_$(1)_rpcgen_stamp: $(2) $$(RPCGEN_DEPS)
	set -e; \
	rm -rf $$@ $(1)_rpc_call.c $(1)_rpc_call.h; \
	$$(RPCGEN) -o $(1)_rpc_call --target user $(2); \
	touch $$@

$(1)_rpc_call.c : stamp_$(1)_rpcgen_stamp
$(1)_rpc_call.h : stamp_$(1)_rpcgen_stamp

RPCGEN_SOURCES += $(1)_rpc_call.c
RPCGEN_INCLUDES += $(1)_rpc_call.h

endef

rpcgen = $(eval $(call rpcgen_eval_func,$(strip $(1)),$(2)))

# Generate RPC sources for all components
$(call rpcgen, test, \
  $(HV_SRC_DIR)/iorpc/test.idl)
$(call rpcgen, gpio, \
  $(HV_SRC_DIR)/tilegx/iorpc/gpio.idl $(HV_SRC_DIR)/tilegx/iorpc/pollfd.idl)
$(call rpcgen, mica, \
  $(HV_SRC_DIR)/tilegx/iorpc/mica.idl)
$(call rpcgen, mpipe, \
  $(HV_SRC_DIR)/tilegx/iorpc/mpipe.idl $(HV_SRC_DIR)/tilegx/iorpc/pollfd.idl)
$(call rpcgen, mpipe_info, \
  $(HV_SRC_DIR)/tilegx/iorpc/mpipe_info.idl)
$(call rpcgen, pka, \
  $(HV_SRC_DIR)/tilegx/iorpc/pka.idl)
$(call rpcgen, trio, \
  $(HV_SRC_DIR)/tilegx/iorpc/trio.idl)
$(call rpcgen, uart, \
  $(HV_SRC_DIR)/tilegx/iorpc/uart.idl)
$(call rpcgen, usb_dev, \
  $(HV_SRC_DIR)/tilegx/iorpc/usb_dev.idl)

$(RPCGEN_STAMP_FILE): $(RPCGEN_STAMP_DEPS)
	touch $@


# Build the userspace library
GXIO_LIB_SOURCES := \
  $(RPCGEN_SOURCES) \
  common.c \
  dma_queue.c \
  gpio.c \
  mica.c \
  mica_queue.c \
  mpipe.c \
  pka.c \
  test.c \
  trio.c \
  uart.c \
  usb_dev.c

-include $(GXIO_LIB_SOURCES:.c=.d)

obj = $(addsuffix .o,$(basename $(1)))

GXIO_LIB_OBJS := $(call obj, $(GXIO_LIB_SOURCES))

INCLUDES ?= \
  -I. \
  -I$(TOP)/tools/gxio/source \
  -I$(TOP)/tools/gxio/include \
  -I$(HV_SRC_DIR)/include

EXTRA_CFLAGS ?=

CFLAGS = -Os -g -std=gnu99 -ffunction-sections -Wall -Wshadow -Werror \
  -D_FILE_OFFSET_BITS=64 $(INCLUDES) $(EXTRA_CFLAGS) -D__GXIO_LIBRARY__

%.o: %.c $(RPCGEN_INCLUDES)
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<

%.os: %.c $(RPCGEN_INCLUDES)
	$(CC) $(CFLAGS) -fpic -c -o $@ $<

libgxio.a: $(GXIO_LIB_SOURCES:.c=.o)
	rm -f $@
	$(AR) cq $@ $^

libgxio.so.1: $(GXIO_LIB_SOURCES:.c=.os)
	$(CC) $(CFLAGS) $(DLL_FLAGS) -Wl,-soname,$@ -shared -o $@ $^

libgxio.so: libgxio.so.1
	$(RM) $@ && ln -s $< $@

LIBS := libgxio.a libgxio.so libgxio.so.1

.PHONY: all
all: $(LIBS)

.PHONY: clean
clean:
	rm -f *.o *.os *.d libgxio* *.c *.h *_stamp
