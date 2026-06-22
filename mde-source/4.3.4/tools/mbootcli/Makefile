# This is a -*- Makefile -*-

# Default target.
all:

ifdef TILERA_ROOT

include $(TILERA_ROOT)/etc/chip.mk
CC = $(TILERA_ROOT)/bin/tile-gcc

else

TILE_ARCH = $(shell uname -m)
ifeq ($(filter tile%,$(TILE_ARCH)),)
$(error TILERA_ROOT environment variable not set)
endif

endif


# NOTE: You can define "FROM" (or "VPATH" and "INCLUDES") to allow building in
# an alternative directory.  You can define "EXTRA_CFLAGS" to modify "CFLAGS".

FROM ?= .

VPATH ?= $(FROM)

EXTRA_CFLAGS ?=

CFLAGS = -g -Os -Wall -Werror -std=gnu99 -fno-strict-aliasing -I$(FROM) \
  $(EXTRA_CFLAGS)

LDFLAGS = \
  -Wl,--gc-sections \
  -Wl,--fatal-warnings \
  -Wl,--warn-unresolved-symbols


SOURCES := \
  cmd/boot.c \
  cmd/bootparam.c \
  cmd/clearcfg.c \
  cmd/dhcp.c \
  cmd/exit.c \
  cmd/fdisk.c \
  cmd/ftp.c \
  cmd/help.c \
  cmd/ifconfig.c \
  cmd/ls.c \
  cmd/mem.c \
  cmd/mkfs.c \
  cmd/ping.c \
  cmd/reboot.c \
  cmd/rm.c \
  cmd/route.c \
  cmd/serial.c \
  cmd/shell.c \
  cmd/showcfg.c \
  cmd/tftp.c \
  flash.c \
  main.c \
  parameters.c \
  utils.c \


# Grab dependencies, if they exist.
-include $(SOURCES:.c=.d)


%.o: %.c
	mkdir -p $(@D) && \
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<

mbootcli: $(SOURCES:.c=.o)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lcrypt


all: mbootcli

clean:
	rm -f *.d *.o mbootcli

.PHONY: all clean
