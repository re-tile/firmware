ifndef TILERA_ROOT
$(error Must define TILERA_ROOT.)
endif

TOP := $(shell cd ../..; pwd)

HANDY := $(TOP)/tools/handy

all:
	mkdir -p handy/tile-64
	$(MAKE) -C handy/tile-64 -f $(HANDY)/Makefile FROM=$(HANDY) \
	  EXTRA_CFLAGS=-m64
	mkdir -p handy/host-32
	$(MAKE) -C handy/host-32 -f $(HANDY)/Makefile FROM=$(HANDY) \
	  EXTRA_CFLAGS=-m32 TILERA_ROOT=
	$(MAKE) -C monitor \
	  EXTRA_CFLAGS=-m32
	$(MAKE) -C shepherd

clean:
	$(RM) -rf handy
	$(MAKE) -C monitor clean
	$(MAKE) -C shepherd clean
