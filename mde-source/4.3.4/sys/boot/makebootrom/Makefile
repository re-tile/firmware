# This is a -*- Makefile -*-

# Default target.
all:

FROM ?= .
EXE = tile-mkrom

include $(FROM)/../../Makefile.inc

CFLAGS := -DTILE_CHIP=$(TILE_CHIP) -I$(TILERA_ROOT)/include $(EXTRA_CFLAGS)

$(EXE): $(FROM)/makebootrom.cc
	$(CXX) -O2 -g -MMD -MP $(CFLAGS) -o $@ $< $(LDFLAGS)

-include tile-mkrom.d

all: $(EXE)

clean:
	rm -f *.o $(EXE)

.PHONY: all clean
