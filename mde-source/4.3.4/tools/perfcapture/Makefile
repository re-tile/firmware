# =============================================================================
# Makefile
# =============================================================================

# -----------------------------------------------------------------------------
# Definitions
# -----------------------------------------------------------------------------

# --- build commands ---

SET_AND_MKDIR = set -e; mkdir -p $(@D)
RM = rm -f
CP = cp -f
MKDIR = mkdir -p


# --- Linux source/build dirs ---

# Linux source tree, in which we can locate Perf sources
LINUX_SRC_DIR = linux_src_dir_undefined

# Linux include dir, in which we can locate Perf headers
# This defaults to empty string because when building on TILE
# we can use /usr/include
LINUX_INC_DIR = 


# --- binutils ---

# Note: To support cross-builds for development/testing we allow specifying
# MDE-based source/build paths. For native TILE builds we can find what
# we need in the normal /usr/include and /usr/lib directories.

# The binutils source tree, for access to header files.
BINUTILS_SRC_DIR   = /usr

# The binutils build tree, for access to internal libraries.
BINUTILS_BUILD_DIR = /usr

BINUTILS_DEFS = 

# Note: we need at least demangle.h and op_bfd.h.
BINUTILS_INCS = \
  -I$(BINUTILS_SRC_DIR)/include \
  -I$(BINUTILS_BUILD_DIR)/bfd

BINUTILS_LIBS = \
  -L$(BINUTILS_SRC_DIR) \
  -L$(BINUTILS_BUILD_DIR)/bfd \
  -L$(BINUTILS_BUILD_DIR)/libiberty \
  -lbfd -liberty


# --- perf events ---

# Perf (aka perf events) includes/libs.
# We need access to the tools/perf directory in
# the Linux kernel source tree for perf.h, etc.

# Perf sources are found under Linux sources
PERF_SRC_DIR = $(LINUX_SRC_DIR)/tools/perf

# PERF_SRC_INC_DIR is location in Linux sources where
# we can find headers for liblk.a, libtraceevent.a, etc.
PERF_SRC_INC_DIR = $(LINUX_SRC_DIR)/tools/lib

PERF_DEFS = -DNO_NEWT_SUPPORT -DLIBELF_NO_MMAP -DHAVE_DECL_BASENAME

PERF_INCS = \
  -I$(PERF_SRC_DIR) \
  -I$(PERF_SRC_DIR)/util \
  -I$(PERF_SRC_DIR)/util/include \
  -I$(PERF_SRC_INC_DIR) \
  -I$(LINUX_INC_DIR)

# PERF_LIB_DIR is set to -Ldirname only when we're cross-building
# on the host to specify where to find these libraries.
# For native builds, we assume we can find these libraries in /usr/lib.

PERF_LIB_DIR = -L/usr/lib

PERF_LIBS = \
  $(PERF_LIB_DIR) \
  -lperf -llk -ltraceevent 


# --- other libs, etc. ---

OTHER_DEFS =

OTHER_INCS =

OTHER_LIBS = \
   -lelf -lz


# --- project files ---

# SOURCE_DIR needs to be set to the perfcapture source tree
SOURCE_DIR = source_dir_undefined

# VPATH allows building in a different directory from the source.
VPATH = $(SOURCE_DIR)

# BUILD_DIR is where build output should go.
# (Only needed if we don't want build output to go in the source dir).
BUILD_DIR = 

EXE_NAME = tile-perfcapture
MAIN = $(EXE_NAME)

SRCS = \
  main.cc \
  Application.cc \
  bfd/bfd_api_c.c \
  bfd/bfd_api_cc.cc \
  perf/perf_api_c.c \
  perf/perf_api_cc.cc \
  model/Binary.cc \
  model/BinarySymbol.cc \
  model/Event.cc \
  model/Frame.cc \
  model/Location.cc \
  model/SampleData.cc \
  model/Task.cc \
  util/string_utils.cc \
  util/Pathname.cc \
  util/XMLDocument.cc \
  util/XMLElement.cc \
  util/XMLPrinter.cc \
  util/XMLReader.cc

MAIN_DEFS =

MAIN_INCS = \
  -I$(SOURCE_DIR) \
  -I$(SOURCE_DIR)/bfd \
  -I$(SOURCE_DIR)/perf \
  -I$(SOURCE_DIR)/model \
  -I$(SOURCE_DIR)/util

MAIN_LIBS =


# --- compilers and options ---

CC  = gcc
CXX = g++

# To compile unoptimized for debugging, pass OPT=-O0 to make.
OPT = -O2

BASE_OPTS = -g $(OPT) -Wall -Werror -pthread
CXX_OPTS  = $(BASE_OPTS)
CC_OPTS   = $(BASE_OPTS) -std=gnu99

# We normally build -static, so there's just one monolithic
# executable to upload to the TILE host.
STATIC = -static

# Uncomment this to debug library lookup in link phase
#DEBUG_LIB_LOOKUP = -Wl,--trace

LOPTS = $(STATIC) -Wall -Werror -pthread $(DEBUG_LIB_LOOKUP)

DOPTS = -MMD -MP

DEFS = \
  $(MAIN_DEFS) \
  $(PERF_DEFS) \
  $(BINUTILS_DEFS) \
  $(OTHER_DEFS)

INCS = \
  $(MAIN_INCS) \
  $(PERF_INCS) \
  $(BINUTILS_INCS) \
  $(OTHER_INCS)

LIBS = \
  $(MAIN_LIBS) \
  $(PERF_LIBS) \
  $(BINUTILS_LIBS) \
  $(OTHER_LIBS)


# --- build lists ---

CXX_SRCS = $(filter %.cc,$(SRCS))
CXX_OBJS = $(CXX_SRCS:%.cc=$(BUILD_DIR)%.o)

CC_SRCS  = $(filter %.c,$(SRCS))
CC_OBJS  = $(CC_SRCS:%.c=$(BUILD_DIR)%.o)

OBJS   = $(CXX_OBJS) $(CC_OBJS)

DFILES = $(OBJS:%.o=%.d)

EXES = $(BUILD_DIR)$(MAIN)


# -----------------------------------------------------------------------------
# Top-Level Rules
# -----------------------------------------------------------------------------

.PHONY: all
all: $(EXES)


# -----------------------------------------------------------------------------
# Build Rules
# -----------------------------------------------------------------------------

$(BUILD_DIR)%.o: %.c
	$(MKDIR) $(@D)
	$(CC) -c $(CC_OPTS) $(DOPTS) $(DEFS) $(INCS) -o $@ $<

$(BUILD_DIR)%.o: %.cc
	$(MKDIR) $(@D)
	$(CXX) -c $(CXX_OPTS) $(DOPTS) $(DEFS) $(INCS) -o $@ $<

$(BUILD_DIR)$(MAIN): $(OBJS)
	@echo "$(MAIN): Warning, if any, about use of mktemp in libpthread.a is expected/harmless." \
	  "Linking statically triggers this warning."
	$(CXX) $(LOPTS) -o $@ $(OBJS) $(LIBS)

# Include dependency files created by $(DOPTS)
-include $(DFILES)

.PHONY: clean
clean:
	$(RM) $(EXES) $(OBJS) $(DFILES)
