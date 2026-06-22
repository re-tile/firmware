all:

ifdef TILERA_ROOT

CC = $(TILERA_ROOT)/bin/tile-gcc
AR = $(TILERA_ROOT)/bin/tile-ar

else

CC = gcc
AR = ar

endif


FROM ?= .
VPATH = $(FROM)

EXTRA_CFLAGS ?=
CFLAGS = -O2 -g -Wall -Werror -std=gnu99 \
  -D_FILE_OFFSET_BITS=64 $(EXTRA_CFLAGS)

SOURCES := \
  Array.c \
  Buffer.c \
  Pollable.c \
  StringArray.c \
  message.c \
  network.c \
  rpc.c \
  various.c

# Grab dependencies, if they exist.
-include $(SOURCES:.c=.d)


%.o: %.c
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<

libhandy.a: $(SOURCES:.c=.o)
	rm -f $@
	$(AR) cq $@ $^

all: libhandy.a

clean:
	$(RM) *.d *.o *.a

.PHONY: all clean
