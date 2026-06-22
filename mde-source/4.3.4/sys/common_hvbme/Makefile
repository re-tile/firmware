# This is a -*- Makefile -*-

# Standalone Makefile for the files that are shared between the hypervisor
# and the BME.

# Default target.
all:

#
# Support separate build and source trees; see comments in ../Makefile
# for usage instructions.
#
ifdef TOP
VPATH = $(TOP)/sys/common_hvbme
else
TOP = ../..
endif

include $(TOP)/sys/Makefile.inc

obj = $(addsuffix .o,$(basename $(1)))

SYS_COMMON_HVBME_SOURCES := \
  board_info_common.c \
  $(TILE_ARCH)/board_info.c \
  $(TILE_ARCH)/jtag.c \
  $(TILE_ARCH)/serdes.c \
  $(TILE_ARCH)/xgbe.c \
  $(TILE_ARCH)/xgbe_link.c

SYS_COMMON_HVBME_OBJS := \
  $(call obj, $(SYS_COMMON_HVBME_SOURCES))

INCLUDES := \
  -mnewlib -isystem $(TOP)/sys/libc/include \
  -I $(TOP)/sys/common_hvbme/include/hvbme \
  -I $(TOP)/sys/common_hvbme/$(TILE_ARCH)/include/hvbme

%.o: %.c
	set -e; \
	mkdir -p $(@D); \
	$(CC) -c $(INCLUDES) -ffunction-sections \
	  -g -Os -MD -MP -Wall -Werror -Wno-packed-bitfield-compat \
	  -std=gnu99 -o $@ $<

-include $(SYS_COMMON_HVBME_SOURCES:%.c=%.d)


SYS_COMMON_HVBME_A = common_hvbme.a

$(SYS_COMMON_HVBME_A): $(SYS_COMMON_HVBME_OBJS)
	set -e; \
	$(RM) $@; \
	$(AR) cq $@ $(sort $(SYS_COMMON_HVBME_OBJS))

all: $(SYS_COMMON_HVBME_A)

clean:
	$(RM) *.o *.d $(SYS_COMMON_HVBME_A) $(TILE_ARCH)/*.o $(TILE_ARCH)/*.d

.PHONY: all clean
