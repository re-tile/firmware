# Default target.
all:

ifdef TILERA_ROOT

CC = $(TILERA_ROOT)/bin/tile-gcc

else

ifeq ($(filter tile%,$(shell uname -m)),)
$(error The 'TILERA_ROOT' environment variable is not set.)
endif

CC = gcc

endif

# Define "FROM" to allow building in an alternative directory.
FROM ?= .

OPT = -Os
CFLAGS = -std=gnu99 -Wall -Werror $(OPT) -pthread
LDFLAGS = $(if $(filter -m32, $(XFLAGS)), -static) -pthread
LIBS =

SRCS := 	\
 mpipe-link.c 	\
 mpipe-mdio.c 	\
 mpipe-stat.c 	\
 trio-stat.c	\
 mcstat.c	\
 memtest.c	\

memtest: LIBS += -ltmc
mpipe-link: LIBS += -lgxio
mpipe-mdio: LIBS += -lgxio
mpipe-stat: LIBS += -lgxio
trio-stat: LIBS += -lgxio

OBJS := $(SRCS:.c=.o)

TARGETS := $(OBJS:.o=)

$(OBJS): %.o: $(FROM)/%.c
	$(CC) $(CFLAGS) $(XFLAGS) -c $< -o $@

$(TARGETS): %: %.o
	$(CC) $(CFLAGS) $(XFLAGS) -o $@ $< $(LIBS)

all: $(TARGETS)

clean:
	rm -f $(OBJS) $(TARGETS)

.PHONY: all clean
