all:

# Top of the source tree.
TOP := $(shell cd ../../..; pwd)

ifdef TILERA_ROOT

CC = gcc

HANDY_LIB := $(TOP)/tools/manager/handy/host-32/libhandy.a

else

$(error Must define TILERA_ROOT.)

endif

EMPTY_COMMAND = :

FROM ?= .
VPATH = $(FROM)

EXTRA_CFLAGS ?=
CFLAGS = -g -O2 -std=gnu99 \
  -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -Werror \
  -D_FILE_OFFSET_BITS=64 $(EXTRA_CFLAGS) -I $(TOP) -I $(TILERA_ROOT)/include


RPC := $(FROM)/../rpc

RPC_DEPS := \
  $(RPC)/gen_rpc.py \
  $(RPC)/token.py \
  $(RPC)/parse.py \
  $(RPC)/emit_c.py \
  $(RPC)/emit_java.py

RPC_CMD := $(RPC)/gen_rpc.py


gen_rpc.h: $(RPC)/manager.idl $(RPC_DEPS)
	$(RPC_CMD) -o $(@D)/gen_rpc --target m --c $<

gen_rpc.c: gen_rpc.h
	@$(EMPTY_COMMAND)


GEN := \
  gen_rpc.h \


%.o: %.c $(GEN)
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<


SOURCES := \
  gen_rpc.c main.c

-include $(SOURCES:.c=.d)

monitor: $(SOURCES:.c=.o) $(HANDY_LIB)
	$(CC) $(CFLAGS) -o $@ $^ -lutil -ldl -pthread


all: monitor

clean:
	$(RM) *.d *.o gen_rpc* monitor

.PHONY: all clean

