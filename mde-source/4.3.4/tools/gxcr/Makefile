# This is a -*- Makefile -*-

# Default target.
all:

ifdef TILERA_ROOT

include $(TILERA_ROOT)/etc/chip.mk
CC = $(TILERA_ROOT)/bin/tile-gcc
AR = $(TILERA_ROOT)/bin/tile-ar

else

TILE_ARCH = $(shell uname -m)
ifeq ($(filter tile%,$(TILE_ARCH)),)
$(error TILERA_ROOT environment variable not set)
endif

endif

RM = rm -f
MKDIR = mkdir -p


# NOTE: You can define "FROM" (or "VPATH" and "INCLUDES") to allow building in
# an alternative directory.  You can define "EXTRA_CFLAGS" to modify "CFLAGS".

FROM ?= .

VPATH ?= $(FROM)/source

INCLUDES ?= -I$(FROM)/include -I$(FROM)/source

EXTRA_CFLAGS ?=

CFLAGS = -O2 -g -ffunction-sections -Wall -Wshadow -Werror -std=gnu99 \
  -D_GNU_SOURCE $(INCLUDES) $(EXTRA_CFLAGS)

DLL_FLAGS = \
  -Wl,--gc-sections \
  -Wl,--fatal-warnings \
  -Wl,--warn-unresolved-symbols


SOURCES := \
  aead.c \
  crypto.c \
  ipsec.c \
  pka.c \
  token_aead.c \
  token_bulk_crypto_digest.c \
  token_bulk_successive.c \
  token_esp_ipv4_tunnel_ibound.c \
  token_esp_ipv4_tunnel_obound.c \
  token_hmac_precalc.c \

# Grab dependencies, if they exist.
-include $(SOURCES:.c=.d)



%.o: %.c
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<

%.os: %.c
	$(CC) $(CFLAGS) -fpic -c -o $@ $<

libgxcr.a: $(SOURCES:.c=.o)
	rm -f $@
	$(AR) cq $@ $^

libgxcr.so.1: $(SOURCES:.c=.os)
	$(CC) $(CFLAGS) $(DLL_FLAGS) -Wl,-soname,$@ -shared -o $@ $^ -lrt

libgxcr.so: libgxcr.so.1
	$(RM) $@ && ln -s $< $@


# Build pka_driver
driver:
	make -C drivers all

# Build PKA test programs
tests:
	make -C tests/pka all


all: libgxcr.a libgxcr.so


clean:
	$(RM) *.d *.o *.os libgxcr.*


.PHONY: all clean
