# This is a -*- Makefile -*-

# Default target.
all:

FROM ?= .

VPATH ?= $(FROM)

include $(FROM)/../Makefile.inc

classifier: $(FROM)/classifier.c
	$(MPIPE_CC) -o $@ $<

all: classifier
