# This is a -*- Makefile -*-

# Default target.
all:

ifndef TILERA_ROOT
$(error TILERA_ROOT environment variable not set)
endif

include $(TILERA_ROOT)/etc/chip.mk

CC = $(TILERA_ROOT)/bin/tile-gcc
AR = $(TILERA_ROOT)/bin/tile-ar

LIBS := libgxpci.a libgxpci.so libgxpci.so.1

.PHONY: all
all: $(LIBS)

#
# Support separate build and source trees
#
ifdef TOP
VPATH = $(TOP)/tools/gxpci
else
TOP = ../..
endif

HV_SRC_DIR := $(TOP)/sys/hv

EMPTY_COMMAND :=

# Build the userspace library
ifeq ($(TILE_ARCH),tilegx)
GXPCI_LIB_SOURCES := \
  gxpci.c host_nic.c host_packet_queue.c host_raw_dma.c chip_to_chip.c \
  c2c.c streamio.c
endif

-include $(GXPCI_LIB_SOURCES:.c=.d)

obj = $(addsuffix .o,$(basename $(1)))

GXPCI_LIB_OBJS := $(call obj, $(GXPCI_LIB_SOURCES))

INCLUDES ?= \
  -I. \
  -I$(TOP)/tools/gxpci/include \
  -I$(HV_SRC_DIR)/include

EXTRA_CFLAGS ?=

CFLAGS = -O3 -g -std=gnu99 -ffunction-sections -Wall -Wshadow -Werror \
  $(INCLUDES) $(EXTRA_CFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<

%.os: %.c
	$(CC) $(CFLAGS) -fpic -c -o $@ $<

libgxpci.a: $(GXPCI_LIB_SOURCES:.c=.o)
	rm -f $@
	$(AR) cq $@ $^

libgxpci.so.1: $(GXPCI_LIB_SOURCES:.c=.os)
	$(CC) $(CFLAGS) $(DLL_FLAGS) -Wl,-soname,$@ -shared -o $@ $^

libgxpci.so: libgxpci.so.1
	$(RM) $@ && ln -s $< $@

.PHONY: clean
clean:
	rm -f *.o *.os *.d libgxpci*
