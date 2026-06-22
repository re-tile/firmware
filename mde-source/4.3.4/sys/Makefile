# This is a -*- Makefile -*-

#
# The "all" target in this Makefile will build the hypervisor binaries (which
# requires building several of the "sys" subdirs), which can then be used with
# the "--hv-bin-dir" flag of "tile-mkboot" (and "tile-monitor").
#
# By default, this Makefile is set up to write the object files and output
# binaries to the same directories which contain the source files.  You can cd
# to $TILERA_ROOT/src/sys, type "make", and have everything work; you can also
# copy the entire $TILERA_ROOT/src tree to another location, and build
# there.  (Copying the source is a good idea if you will be making changes to
# it.)  However, you can, if desired, create your object files and output
# binaries in a directory separate from your source directory.  To do this,
# you must pass two variables to the Makefile:
#
# - TOP is set to the root of the entire MDE source directory (e.g.,
#   $TILERA_ROOT/src, or wherever you copied the source to; one directory
#   level above the directory containing this Makefile).
#
# - ROOT is set to the destination build directory.  (The build process will
#   create this directory, as well as sys/hv, sys/libc, sys/libbz2, and
#   sys/common_hvbme subdirectories within it, if it does not exist.)
#
# So, for example, you could build everything into the directory /tmp/foo as
# follows:
#
#   make TOP=$TILERA_ROOT/src ROOT=/tmp/foo
#

ifdef TOP
ROOT ?= /undefined-ROOT
MAKEARGS = \
  TOP=$(if $(filter /%,$(TOP)),$(TOP),$(CURDIR)/$(TOP)) \
  ROOT=$(if $(filter /%,$(ROOT)),$(ROOT),$(CURDIR)/$(ROOT))
else
TOP = $(dir $(CURDIR))
ROOT = $(dir $(CURDIR))
MAKEARGS =
endif

include $(TOP)/sys/Makefile.inc

# Default target.
all:
	mkdir -p $(ROOT)/sys/libc $(ROOT)/sys/libbz2 $(ROOT)/sys/hv \
		$(ROOT)/sys/common_hvbme
	$(MAKE) -C $(ROOT)/sys/libc -f $(TOP)/sys/libc/Makefile $(MAKEARGS)
	$(MAKE) -C $(ROOT)/sys/libbz2 -f $(TOP)/sys/libbz2/Makefile $(MAKEARGS)
	$(MAKE) -C $(ROOT)/sys/common_hvbme -f \
		$(TOP)/sys/common_hvbme/Makefile $(MAKEARGS)
	$(MAKE) -C $(ROOT)/sys/hv -f $(TOP)/sys/hv/Makefile $(MAKEARGS)

clean:
	$(MAKE) -C $(ROOT)/sys/boot/makebootrom \
		-f $(TOP)/sys/boot/makebootrom/Makefile clean $(MAKEARGS)
	$(MAKE) -C $(ROOT)/sys/libc -f $(TOP)/sys/libc/Makefile clean \
		$(MAKEARGS)
	$(MAKE) -C $(ROOT)/sys/libbz2 -f $(TOP)/sys/libbz2/Makefile clean \
		$(MAKEARGS)
	$(MAKE) -C $(ROOT)/sys/common_hvbme -f \
		$(TOP)/sys/common_hvbme/Makefile clean $(MAKEARGS)
	$(MAKE) -C $(ROOT)/sys/hv -f $(TOP)/sys/hv/Makefile clean $(MAKEARGS)

#
# Create a bootrom using the hypervisor built in this directory.
#
BOOTROM = $(ROOT)/vmlinux.bootrom

bootrom: $(BOOTROM)

$(BOOTROM): all
	$(MONITOR) --no-dev --create-bootrom $@ --hv-bin-dir $(ROOT)/sys/hv

.PHONY: all clean bootrom
