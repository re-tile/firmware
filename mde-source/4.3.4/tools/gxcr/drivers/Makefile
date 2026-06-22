
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

SRCS := pka_driver.c pka_alloc.c
OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

OPT = -O2
CFLAGS = -std=gnu99 -Wall -Werror -g $(OPT) -MMD \
	-I$(FROM)/../source -I$(FROM)/../include
LIBS = -ltmc -lrt -static

all: pka_driver

$(OBJS): %.o: $(FROM)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

pka_driver: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

clean:
	rm -f *.o pka_driver *.d

-include $(DEPS)

.PHONY: all clean
