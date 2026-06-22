# =============================================================================
# Makefile
# =============================================================================

# -----------------------------------------------------------------------------
# Definitions
# -----------------------------------------------------------------------------

EXE_NAME ?= tile-op2xml

# Commands:
SET_AND_MKDIR=set -e; mkdir -p $(@D)
CP=cp -f
RM=rm -f

ifdef TOP
ROOT ?= /undefined-ROOT
VPATH = $(TOP)/tools/op2xml
else
TOP = ../..
ROOT = ../..
endif

BINUTILS_SOURCE ?= $(TOP)/tools/binutils
BINUTILS_BUILD  ?= $(ROOT)/tools/binutils

OPROFILE_SOURCE ?= $(TOP)/tools/oprofile
OPROFILE_BUILD  ?= $(ROOT)/tools/oprofile

# Application sources:
SOURCES = \
  Binary.cc \
  CallGraph.cc \
  CallGraphNodeFactory.cc \
  Frame.cc \
  MonitorDataFile.cc \
  OutputWriter.cc \
  Process.cc \
  Sample.cc \
  SampleFile.cc \
  SampleFilePathname.cc \
  SampleLocation.cc \
  StatisticDescriptor.cc \
  StatisticDescriptorIndex.cc \
  SymbolFile.cc \
  SymbolFileManager.cc \
  arguments.cc \
  global_options.cc \
  main.cc \
  utils/AllocTester.cc \
  utils/Pathname.cc \
  utils/Time.cc \
  utils/XMLDocument.cc \
  utils/XMLElement.cc \
  utils/XMLPrinter.cc \
  utils/XMLReader.cc \
  utils/XMLUtils.cc \
  utils/options.cc \
  utils/string_utils.cc \
  utils/utils.cc \
  utils/xml.cc \

OBJECTS = \
  $(SOURCES:%.cc=%.o)

# Application defs/includes/libs
APP_SOURCE_DIR=$(TOP)/tools/op2xml
APP_DEFS =
APP_INCS = \
  -I$(APP_SOURCE_DIR)/utils
APP_LIBS =

# OProfile defs/includes/libs
OPROFILE_DEFS    =
OPROFILE_INCS    = \
  -I$(OPROFILE_BUILD) \
  -I$(OPROFILE_SOURCE)/libdb \
  -I$(OPROFILE_SOURCE)/libop \
  -I$(OPROFILE_SOURCE)/libutil \
  -I$(OPROFILE_SOURCE)/libutil++ \
  -I$(OPROFILE_SOURCE)/libregex \
  -I$(OPROFILE_SOURCE)/libpp \

OPROFILE_LIBS    = \
  $(OPROFILE_BUILD)/libpp/libpp.a \
  $(OPROFILE_BUILD)/libregex/libop_regex.a \
  $(OPROFILE_BUILD)/libdb/libodb.a \
  $(OPROFILE_BUILD)/libutil++/libutil++.a \
  $(OPROFILE_BUILD)/libutil/libutil.a \
  $(OPROFILE_BUILD)/libopt++/libopt++.a \
  $(OPROFILE_BUILD)/libop/libop.a \

# binutils (bfd, etc.) defs/includes/libs:
# We can find what we need in /usr/include, /usr/lib.
BINUTILS_DEFS    =
BINUTILS_INCS    = \
  -I$(BINUTILS_SOURCE)/include \
  -I$(BINUTILS_BUILD)/bfd
BINUTILS_LIBS    = \
  -L$(BINUTILS_SOURCE) \
  -L$(BINUTILS_BUILD)/bfd \
  -L$(BINUTILS_BUILD)/libiberty \
  -lbfd -liberty -lz

# Other defs/includes/libs:
# We can find what we need in /usr/include, /usr/lib.
OTHER_DEFS =
OTHER_INCS =
OTHER_LIBS =

# All defs/includes/libs:
DEFS = $(APP_DEFS) $(OPROFILE_DEFS) $(BINUTILS_DEFS) $(OTHER_DEFS)
INCS = $(APP_INCS) $(OPROFILE_INCS) $(BINUTILS_INCS) $(OTHER_INCS)
LIBS = $(APP_LIBS) $(OPROFILE_LIBS) $(BINUTILS_LIBS) $(OTHER_LIBS)

# Compilers:
CC=gcc -m32
CXX=g++ -m32

# Compiler flags:
# WARNING: Building fails with "-Os"!
CFLAGS = -g -O2 -Wall -Werror
LDFLAGS = 

# Dependency generation:
DFLAGS = -MD -MP
DEP_FILES = $(OBJECTS:%.o=%.d)


# -----------------------------------------------------------------------------
# Rules
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

$(EXE_NAME): $(OBJECTS)
	$(SET_AND_MKDIR); \
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

all: $(EXE_NAME)

.PHONY: clean
clean:
	$(RM) $(EXE_NAME) $(OBJECTS)
