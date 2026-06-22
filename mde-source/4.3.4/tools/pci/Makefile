# This is a -*- Makefile -*-

# You can define "CROSS=1" to do a cross-build.
# You can define "FROM=PATH" to allow building in an alternative directory.
# You can define "EXTRA_CFLAGS" to safely extend "CFLAGS".

# Default target.
all:

ifndef TILERA_ROOT
$(error TILERA_ROOT environment variable not set)
endif

include $(TILERA_ROOT)/etc/chip.mk

FROM ?= .

EXTRA_CFLAGS ?=

CFLAGS = -O2 -g -Wall -Wshadow -Werror -std=gnu99 $(EXTRA_CFLAGS)

ifdef CROSS
CC = $(TILERA_ROOT)/bin/tile-gcc
else
CFLAGS += -I$(TILERA_ROOT)/include
endif

CFLAGS += -DTILE_CHIP=$(TILE_CHIP)

LDFLAGS = -Wl,--gc-sections -Wl,--fatal-warnings


SOURCES := \
  pci-net.c \
  pci-pipe.c

# Grab dependencies, if they exist.
-include $(SOURCES:.c=.d)


pci-net.o: $(FROM)/pci-net.c
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<

pci-net: pci-net.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lpthread

all: pci-net


pci-pipe.o: $(FROM)/pci-pipe.c
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<

pci-pipe: pci-pipe.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

all: pci-pipe


clean:
	rm -f *.d *.o pci-net pci-pipe

.PHONY: all clean
