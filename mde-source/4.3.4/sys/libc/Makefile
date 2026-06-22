# This is a -*- Makefile -*-

# Standalone Makefile for the system libc.

# Default target.
all:

#
# Support separate build and source trees; see comments in ../Makefile
# for usage instructions.
#
ifdef TOP
VPATH = $(TOP)/sys/libc
else
TOP = ../..
endif

include $(TOP)/sys/Makefile.inc

NEWLIB_LIBC_A = $(TILE_PREFIX)/usr/lib/newlib/usr/lib/libc.a
LIBC_SYS_A = libc_sys.a

obj = $(addsuffix .o,$(basename $(1)))

NEWLIB_LIBC_EXTRACTED_OBJS := \
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
  strtok_r.o \
  strupr.o \
  strxfrm.o \
  swab.o

LIBC_SOURCES := \
  panic.c \
  printf.c \
  stdio.c \
  str2l.c \
  tnslock.c

LIBC_OBJS := \
  setjmp.o \
  $(NEWLIB_LIBC_EXTRACTED_OBJS) \
  $(call obj, $(LIBC_SOURCES))

setjmp.o: setjmp_$(TILE_ARCH).S
	$(CC) -c -o $@ $<

INCLUDES = -mnewlib -isystem $(TOP)/sys/libc/include

%.o: %.c
	$(CC) -c $(INCLUDES) -ffunction-sections \
	  -g -Os -MD -MP -Wall -Werror -std=gnu99 $<

-include $(addsuffix .d, $(basename $(LIBC_SOURCES)))

$(NEWLIB_LIBC_EXTRACTED_OBJS): $(NEWLIB_LIBC_A)
	$(AR) p $< $(@F) > $@.tmp && $(MV) $@.tmp $@

$(LIBC_SYS_A): $(LIBC_OBJS)
	set -e; \
	$(RM) $@; \
	$(AR) cq $@ $(sort $(LIBC_OBJS))

all: $(LIBC_SYS_A)

clean:
	$(RM) *.o *.d $(LIBC_SYS_A)

.PHONY: all clean
