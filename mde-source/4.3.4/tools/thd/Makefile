# This is a -*- Makefile -*-

# =============================================================================
# Makefile
# =============================================================================

# -----------------------------------------------------------------------------
# Definitions
# -----------------------------------------------------------------------------

# Commands:
SET_AND_MKDIR=set -e; mkdir -p $(@D)
CP=cp -f
RM=rm -f

ifdef TOP
ROOT ?= /undefined-ROOT
VPATH = $(TOP)/tools/thd
else
TOP = ../..
ROOT = ../..
endif

THD_HTML_DOCROOT_DIR := $(ROOT)/tools/thd/etc/tile-thd/html
THD_HTML_BIN_DIR = $(THD_HTML_DOCROOT_DIR)/bin

# Application sources:
MAIN_EXE      = tile-thd

MAIN_SOURCES = \
  src/server/HTTPRequest.cc \
  src/server/HTTPResponse.cc \
  src/server/HTTPServer.cc \
  src/server/Parameter.cc \
  src/server/Server.cc \
  src/server/tile-thd.cc \
  src/server/URL.cc

MAIN_OBJECTS = $(MAIN_SOURCES:%.cc=%.o)


CHIP_INFO_EXE = getchipinfo

CHIP_INFO_SOURCES = \
  src/tools/getchipinfo.cc \
  src/tools/CPUInfo.cc \
  src/tools/TaskInfo.cc

CHIP_INFO_OBJECTS = $(CHIP_INFO_SOURCES:%.cc=%.o)


PROC_INFO_EXE = getprocinfo

PROC_INFO_SOURCES = \
  src/tools/getprocinfo.cc \
  src/tools/CPUInfo.cc \
  src/tools/TaskInfo.cc

PROC_INFO_OBJECTS = $(PROC_INFO_SOURCES:%.cc=%.o)


PAGE_INFO_EXE = getpageinfo

PAGE_INFO_SOURCES = \
  src/tools/getpageinfo.cc \
  src/tools/PageInfo.cc \
  src/tools/MemoryInfo.cc

PAGE_INFO_OBJECTS = $(PAGE_INFO_SOURCES:%.cc=%.o)


UTILS_SOURCES = \
  src/utils/utils.cc \
  src/utils/io_utils.cc \
  src/utils/string_utils.cc \
  src/utils/Pathname.cc

UTILS_OBJECTS = $(UTILS_SOURCES:%.cc=%.o)

OBJECTS= \
  $(MAIN_OBJECTS) \
  $(CHIP_INFO_OBJECTS) \
  $(PROC_INFO_OBJECTS) \
  $(PAGE_INFO_OBJECTS) \
  $(UTILS_OBJECTS)

EXES = \
  $(MAIN_EXE) \
  $(CHIP_INFO_EXE) \
  $(PROC_INFO_EXE) \
  $(PAGE_INFO_EXE)

# Application defs/includes/libs
APP_SOURCE_DIR=$(TOP)/tools/thd/src
APP_DEFS =
APP_INCS = \
  -I$(APP_SOURCE_DIR)/server \
  -I$(APP_SOURCE_DIR)/tools \
  -I$(APP_SOURCE_DIR)/utils
APP_LIBS =

# Other defs/includes/libs:
OTHER_DEFS =
OTHER_INCS =
OTHER_LIBS =

# All defs/includes/libs:
DEFS = $(APP_DEFS) $(OTHER_DEFS)
INCS = $(APP_INCS) $(OTHER_INCS)
LIBS = $(APP_LIBS) $(OTHER_LIBS)

# Compilers:
CC  = gcc -m32
CXX = g++ -m32

# Compiler flags:
CFLAGS = -g -O0 -Wall -Werror
LDFLAGS = 

# Dependency generation:
DFLAGS = -MD -MP
DEP_FILES = $(OBJECTS:%.o=%.d)


# -----------------------------------------------------------------------------
# Build Rules
# -----------------------------------------------------------------------------

.PHONY: default all
default: all

# Include dependency files created by $(DFLAGS) below.
-include $(DEP_FILES)

%.o: %.c
	$(SET_AND_MKDIR); \
	$(CC)  $(CFLAGS) $(DFLAGS) $(DEFS) $(INCS) -c -std=gnu99 -o $@ $<

%.o: %.cc
	$(SET_AND_MKDIR); \
	$(CXX) $(CFLAGS) $(DFLAGS) $(DEFS) $(INCS) -c -o $@ $<


$(MAIN_EXE): $(MAIN_OBJECTS) $(UTILS_OBJECTS)
	$(SET_AND_MKDIR); \
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)


$(CHIP_INFO_EXE): $(CHIP_INFO_OBJECTS) $(UTILS_OBJECTS)
	$(SET_AND_MKDIR); \
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

$(THD_HTML_BIN_DIR)/$(CHIP_INFO_EXE): \
  $(CHIP_INFO_EXE)
	$(SET_AND_MKDIR); \
	$(CP) $< $@


$(PROC_INFO_EXE): $(PROC_INFO_OBJECTS) $(UTILS_OBJECTS)
	$(SET_AND_MKDIR); \
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

$(THD_HTML_BIN_DIR)/$(PROC_INFO_EXE): \
  $(PROC_INFO_EXE)
	$(SET_AND_MKDIR); \
	$(CP) $< $@


$(PAGE_INFO_EXE): $(PAGE_INFO_OBJECTS) $(UTILS_OBJECTS)
	$(SET_AND_MKDIR); \
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

$(THD_HTML_BIN_DIR)/$(PAGE_INFO_EXE): \
  $(PAGE_INFO_EXE)
	$(SET_AND_MKDIR); \
	$(CP) $< $@


all: \
  $(EXES) \
  $(THD_HTML_BIN_DIR)/$(CHIP_INFO_EXE) \
  $(THD_HTML_BIN_DIR)/$(PROC_INFO_EXE) \
  $(THD_HTML_BIN_DIR)/$(PAGE_INFO_EXE)

.PHONY: clean
clean:
	$(RM) $(EXES) $(OBJECTS) $(DEP_FILES)
	$(RM) $(THD_HTML_BIN_DIR)/$(CHIP_INFO_EXE)
	$(RM) $(THD_HTML_BIN_DIR)/$(PROC_INFO_EXE)
	$(RM) $(THD_HTML_BIN_DIR)/$(PAGE_INFO_EXE)


# -----------------------------------------------------------------------------
# Run Rules (for development testing only)
# -----------------------------------------------------------------------------

TILE_MONITOR=$(TILERA_ROOT)/bin/tile-monitor
TILE_CONSOLE=$(TILERA_ROOT)/bin/tile-console

TARGET=--dev wc-27

PORT=8080

HERE = $(shell pwd)

.PHONY: reboot upload start stop start_local stop_local resume console
reboot:
	$(TILE_MONITOR) $(TARGET) \
	  --hvx 'dataplane=24-29 network_cpus=30-35' \
	  --quit

upload:
	$(TILE_MONITOR) $(TARGET) --resume \
	  --upload-tile /usr/bin/tile-thd \
	  --upload-tile /etc/tile-thd/conf \
	  --upload-tile /etc/tile-thd/html \
	  --upload-tile /etc/tile-thd/netd \
	  --upload-tile /usr/lib/libstdc++.so \
	  --upload-tile /usr/lib/libstdc++.so.6 \
	  --upload-tile /lib/libm.so.6 \
	  --upload-tile /lib/libm-2.12.so \
	  --upload-tile /usr/lib/libstdc++.so.6.0.13 \
	  --quit

start:
	$(TILE_MONITOR) $(TARGET) --resume \
	  --here \
	  --run -+- /usr/bin/tile-thd -p $(PORT) \
	            -c /etc/tile-thd/conf \
	 	    -d /etc/tile-thd/html -+-

stop:
	$(TILE_MONITOR) $(TARGET) --resume \
	  --here \
	  --run -+- killall tile-thd -+- \
	  --quit

start_local:
	rm -f /tmp/thd*
	tile-thd -p $(PORT) -c etc/tile-thd/conf -d etc/tile-thd/html &

stop_local:
	killall tile-thd

resume:
	$(TILE_MONITOR) $(TARGET) --resume \
	  --here \

console:
	$(TILE_CONSOLE) $(TARGET)

busy:
	$(TILE_MONITOR) $(TARGET) --resume \
	  --here \
	  --run -+- /u/bswanson/dev/main_gx/tools/thd/test/busywork -+- \
	  --quit
