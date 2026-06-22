all:

# Top of the source tree.
TOP := $(shell cd ../../..; pwd)

ifdef TILERA_ROOT

CC = $(TILERA_ROOT)/bin/tile-gcc

HANDY_LIB := $(TOP)/tools/manager/handy/tile-64/libhandy.a

else

$(error Must define TILERA_ROOT.)

endif

EMPTY_COMMAND = :

FROM ?= .
VPATH = $(FROM)

EXTRA_CFLAGS ?=
CFLAGS = -g -O2 -std=gnu99 \
  -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -Werror \
  -D_FILE_OFFSET_BITS=64 $(EXTRA_CFLAGS) -I $(TOP)


RPC := $(FROM)/../rpc

RPC_DEPS := \
  $(RPC)/gen_rpc.py \
  $(RPC)/token.py \
  $(RPC)/parse.py \
  $(RPC)/emit_c.py \
  $(RPC)/emit_java.py

RPC_CMD := $(RPC)/gen_rpc.py


gen_rpc.h: $(RPC)/manager.idl $(RPC_DEPS)
	$(RPC_CMD) -o $(@D)/gen_rpc --target s --c $<

gen_rpc.c: gen_rpc.h
	@$(EMPTY_COMMAND)


gen_rpc_task.h: $(RPC)/manager.idl $(RPC_DEPS)
	$(RPC_CMD) -o $(@D)/gen_rpc_task --target w --c $< \
	  --dispatch dispatch_task_packet

gen_rpc_task.c: gen_rpc_task.h
	@$(EMPTY_COMMAND)


gen_rpc_fuse.h: $(RPC)/manager.idl $(RPC_DEPS)
	$(RPC_CMD) -o $(@D)/gen_rpc_fuse --target f --c $< \
	  --dispatch dispatch_fuse_packet

gen_rpc_fuse.c: gen_rpc_fuse.h
	@$(EMPTY_COMMAND)


GEN := \
  gen_rpc.h \
  gen_rpc_task.h \
  gen_rpc_fuse.h \


%.o: %.c $(GEN)
	$(CC) $(CFLAGS) -MD -MP -c -o $@ $<


SOURCES := \
  gen_rpc.c gen_rpc_task.c \
  main.c ptrace.c rsp.c server.c threads.c upload.c

-include $(SOURCES:.c=.d)

shepherd: $(SOURCES:.c=.o) $(HANDY_LIB)
	$(CC) $(CFLAGS) -o $@ $^ -ltmc


FUSE_SOURCES := gen_rpc_fuse.c fuse.c

-include $(FUSE_SOURCES:.c=.d)

shepherd-fuse: $(FUSE_SOURCES:.c=.o) $(HANDY_LIB)
	$(CC) $(CFLAGS) -o $@ $^ -ltmc -lfuse -ldl -lrt -pthread


all: shepherd shepherd-fuse

clean:
	$(RM) *.d *.o gen_rpc* shepherd shepherd-fuse

.PHONY: all clean
