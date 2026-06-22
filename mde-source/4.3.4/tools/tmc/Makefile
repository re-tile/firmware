# This is a -*- Makefile -*-

LIBDIR = /lib
USRLIBDIR = /usr$(LIBDIR)
RELLIBDIR = ../..$(LIBDIR)/
INCDIR = /usr/include

# Default target.
all:

ifdef TILERA_ROOT

include $(TILERA_ROOT)/etc/chip.mk
TARGET := $(TILE_ARCH)
CC = $(TILERA_ROOT)/bin/tile-gcc
AR = $(TILERA_ROOT)/bin/tile-ar

else

TARGET := $(shell uname -m)

endif

RM = rm -f
CP = cp -f
MKDIR = mkdir -p


# NOTE: You can define "FROM" (or "VPATH" and "INCLUDES") to allow building in
# an alternative directory.  You can define "EXTRA_CFLAGS" to modify "CFLAGS".

FROM ?= .

VPATH ?= $(FROM)/source

INCLUDES ?= -I$(FROM)/include -I$(FROM)/source

EXTRA_CFLAGS ?=

DEFS ?=


CFLAGS = -O2 -g -ffunction-sections \
  -Wall -Wextra -Wshadow -Wno-sign-compare -Wno-unused-parameter \
  -Werror -std=gnu99 -D_GNU_SOURCE $(INCLUDES) $(EXTRA_CFLAGS) $(DEFS)
SFLAGS = $(EXTRA_CFLAGS)

DLL_FLAGS = \
  -Wl,--gc-sections \
  -Wl,--fatal-warnings \
  -Wl,--warn-unresolved-symbols


SOURCES := \
  alloc.c \
  cmem.c \
  cpus.c \
  mspace.c \
  shmem.c \
  sync.c \
  task.c \

HEADERS := \
  alloc.h \
  cmem.h \
  cpus.h \
  malloc.h \
  mspace.h \
  shmem.h \
  sync.h \
  task.h \

ifneq ($(filter tile%,$(TARGET)),)
SOURCES += \
  mem.c \
  udn.c
ASM_SOURCES := \
  mem_asm.S \
  interrupt_asm.S
HEADERS += \
  cacheline_queue.h \
  interrupt.h \
  ipi.h \
  mem.h \
  queue.h \
  queue_internal.h \
  udn.h
ifeq ($(TARGET),tilegx)
SOURCES += spin_64.c \
  interrupt_64.c \
  ipi.c \
  perf.c
HEADERS += perf.h
SPIN_HEADER := spin_64.h
else
SOURCES += spin_32.c \
  interrupt_32.c
SPIN_HEADER := spin_32.h
endif
endif

# Grab dependencies, if they exist.
-include $(SOURCES:.c=.d)
-include $(ASM_SOURCES:.S=.d)



ifndef NO_STATIC

%.o: %.c
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<

%.o: %.S
	$(CC) $(SFLAGS) -MD -MP -c -o $@ $<

libtmc.a: $(SOURCES:.c=.o) $(ASM_SOURCES:.S=.o)
	rm -f $@
	$(AR) cq $@ $^

libtmc_malloc.a: malloc.o
	rm -f $@
	$(AR) cq $@ $^

all: libtmc.a libtmc_malloc.a

install:: all
	$(MKDIR) $(DESTDIR)$(USRLIBDIR)
	cp libtmc.a libtmc_malloc.a $(DESTDIR)$(USRLIBDIR)

endif
ifndef NO_SHARED

%.os: %.c
	$(CC) $(CFLAGS) -fpic -DSHARED -c -o $@ $<

libtmc.so.1: $(SOURCES:.c=.os) $(ASM_SOURCES:.S=.o)
	$(CC) $(CFLAGS) $(DLL_FLAGS) -Wl,-soname,$@ -shared -o $@ $^

libtmc.so: libtmc.so.1
	$(RM) $@ && ln -s $< $@

libtmc_malloc.so.1: malloc.os $(FROM)/source/malloc_$(TARGET).ver libtmc.so
	$(CC) -shared $(CFLAGS) $(DLL_FLAGS) -Wl,-soname,$@ \
	  -Wl,--version-script=$(FROM)/source/malloc_$(TARGET).ver \
	  -o $@ $< -L. -ltmc

libtmc_malloc.so: libtmc_malloc.so.1
	$(RM) $@ && ln -s $< $@

all: libtmc.so libtmc_malloc.so

install:: all
	$(MKDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(USRLIBDIR)
	cp libtmc.so.1 libtmc_malloc.so.1 $(DESTDIR)$(LIBDIR)
	ln -sf $(RELLIBDIR)libtmc.so.1 \
	  $(DESTDIR)$(USRLIBDIR)/libtmc.so
	ln -sf $(RELLIBDIR)libtmc_malloc.so.1 \
	  $(DESTDIR)$(USRLIBDIR)/libtmc_malloc.so

endif
ifndef NO_HEADERS

install::
	$(MKDIR) $(DESTDIR)$(INCDIR)/tmc
	$(CP) $(addprefix $(FROM)/include/tmc/, $(HEADERS)) \
	  $(DESTDIR)$(INCDIR)/tmc
ifdef SPIN_HEADER
	$(CP) $(FROM)/include/tmc/$(SPIN_HEADER) $(DESTDIR)$(INCDIR)/tmc/spin.h
endif

endif

clean:
	$(RM) *.d *.o *.os libtmc.* libtmc_malloc.*


.PHONY: all install clean
