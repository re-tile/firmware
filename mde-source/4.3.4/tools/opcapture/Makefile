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


# --- binutils ---

# The binutils source tree, for access to header files.
BINUTILS_SRC_DIR   ?= binutils_src_dir_undefined

# The binutils build tree, for access to internal libraries.
BINUTILS_BUILD_DIR ?= binutils_build_dir_undefined

BINUTILS_DEFS = 

# Note: we need at least demangle.h and op_bfd.h.
BINUTILS_INCS = \
  -I$(BINUTILS_SRC_DIR)/include \
  -I$(BINUTILS_BUILD_DIR)/bfd

BINUTILS_LIBS = \
  -L$(BINUTILS_BUILD_DIR)/libiberty \
  -lbfd -liberty -lz


# --- OProfile ---

# The oprofile source tree, for access to header files.
OPROFILE_SRC_DIR   ?= oprofile_src_dir_undefined

# The oprofile build tree, for access to internal libraries.
OPROFILE_BUILD_DIR ?= oprofile_build_dir_undefined

OPROFILE_DEFS =

# We need the headers for the various OProfile utility libs,
# plus config.h, which is in the build directory.
OPROFILE_INCS = \
  -I$(OPROFILE_SRC_DIR)/libdb \
  -I$(OPROFILE_SRC_DIR)/libop \
  -I$(OPROFILE_SRC_DIR)/libpp \
  -I$(OPROFILE_SRC_DIR)/libregex \
  -I$(OPROFILE_SRC_DIR)/libutil++ \
  -I$(OPROFILE_SRC_DIR)/libutil \
  -I$(OPROFILE_BUILD_DIR)

OPROFILE_LIBS = \
  $(OPROFILE_BUILD_DIR)/libpp/libpp.a \
  $(OPROFILE_BUILD_DIR)/libregex/libop_regex.a \
  $(OPROFILE_BUILD_DIR)/libdb/libodb.a \
  $(OPROFILE_BUILD_DIR)/libutil++/libutil++.a \
  $(OPROFILE_BUILD_DIR)/libutil/libutil.a \
  $(OPROFILE_BUILD_DIR)/libopt++/libopt++.a \
  $(OPROFILE_BUILD_DIR)/libop/libop.a \


# --- other libs, etc. ---

OTHER_DEFS =

OTHER_INCS =

OTHER_LIBS =


# --- project files ---

# SOURCE_DIR needs to be set to the opcapture source tree
SOURCE_DIR ?= source_dir_undefined

# VPATH allows building in a different directory from the source.
VPATH = $(SOURCE_DIR)

# BUILD_DIR is where build output should go.
# (Only needed if we don't want build output to go in the source dir).
BUILD_DIR ?= 

SRCS = \
  main.cc \
  Application.cc \
  model/Event.cc \
  model/Binary.cc \
  model/BinarySymbol.cc \
  model/Task.cc \
  model/Location.cc \
  model/Frame.cc \
  model/SampleData.cc \
  oprofile/Sample.cc \
  oprofile/SampleLocation.cc \
  oprofile/SampleFile.cc \
  oprofile/SampleFilePathname.cc \
  oprofile/Symbol.cc \
  oprofile/SymbolFile.cc \
  oprofile/SymbolFileManager.cc \
  util/string_utils.cc \
  util/Pathname.cc \
  util/XMLDocument.cc \
  util/XMLElement.cc \
  util/XMLPrinter.cc \
  util/XMLReader.cc

MAIN_DEFS =

MAIN_INCS = \
  -I$(SOURCE_DIR) \
  -I$(SOURCE_DIR)/model \
  -I$(SOURCE_DIR)/oprofile \
  -I$(SOURCE_DIR)/util

MAIN_LIBS =

EXE_NAME ?= tile-opcapture
MAIN = $(EXE_NAME)


# --- compilers and options ---

CC  ?= gcc
CXX ?= g++

# Uncomment only one of these
#DEBUG    = -g -O0
OPTIMIZED = -g -O3

COPTS    = $(DEBUG) $(OPTIMIZED) -Wall -Werror
CXX_OPTS = $(COPTS)
CC_OPTS  = $(COPTS) -std=gnu99

LOPTS = -static -Wall -Werror

DOPTS = -MMD -MP

DEFS = \
  $(MAIN_DEFS) \
  $(OPROFILE_DEFS) \
  $(BINUTILS_DEFS) \
  $(OTHER_DEFS)

INCS = \
  $(MAIN_INCS) \
  $(OPROFILE_INCS) \
  $(BINUTILS_INCS) \
  $(OTHER_INCS)

# Note: library order matters; first OProfile, then binutils
LIBS = \
  $(MAIN_LIBS) \
  $(OPROFILE_LIBS) \
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

.PHONY: default
default: all

.PHONY: all
all: $(EXES)


# -----------------------------------------------------------------------------
# Build Rules
# -----------------------------------------------------------------------------

$(BUILD_DIR)%.o: %.c
	$(SET_AND_MKDIR); \
	$(CC) -c $(CC_OPTS) $(DOPTS) $(DEFS) $(INCS) -o $@ $<

$(BUILD_DIR)%.o: %.cc
	$(SET_AND_MKDIR); \
	$(CXX) -c $(CXX_OPTS) $(DOPTS) $(DEFS) $(INCS) -o $@ $<

$(BUILD_DIR)$(MAIN): $(OBJS)
	$(SET_AND_MKDIR); \
	$(CXX) $(LOPTS) -o $@ $(OBJS) $(LIBS)

# Include dependency files created by $(DOPTS)
-include $(DFILES)

clean:
	$(RM) $(EXES) $(OBJS) $(DFILES)
