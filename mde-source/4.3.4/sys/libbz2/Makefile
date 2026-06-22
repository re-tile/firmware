# This is a -*- Makefile -*-

# Standalone Makefile for the bzip2 library.

# Default target.
all:

#
# Support separate build and source trees; see comments in ../Makefile
# for usage instructions.
#
ifdef TOP
VPATH = $(TOP)/sys/libbz2
else
TOP = ../..
endif

include $(TOP)/sys/Makefile.inc

obj = $(addsuffix .o,$(basename $(1)))

LIBBZ2_SOURCES := \
  blocksort.c \
  bzlib.c \
  compress.c \
  crctable.c \
  decompress.c \
  huffman.c \
  randtable.c

LIBBZ2_OBJS := \
  $(call obj, $(LIBBZ2_SOURCES))

%.o: %.c
	$(CC) -c -ffunction-sections -g -mnewlib -Os -MD -MP -Wall \
	  -DBZ_NO_STDIO -DBZ_NO_DEFAULT_ALLOC $<

-include $(LIBBZ2_SOURCES:%.c=%.d)


LIBBZ2_SYS_A = libbz2.a

$(LIBBZ2_SYS_A): $(LIBBZ2_OBJS)
	set -e; \
	$(RM) $@; \
	$(AR) cq $@ $(sort $(LIBBZ2_OBJS))

all: $(LIBBZ2_SYS_A)

clean:
	$(RM) *.o *.d $(LIBBZ2_SYS_A)

.PHONY: all clean
