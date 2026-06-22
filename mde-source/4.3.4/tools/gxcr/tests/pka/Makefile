
ifdef TILERA_ROOT

CC = $(TILERA_ROOT)/bin/tile-gcc

else

ifeq ($(filter tile%,$(shell uname -m)),)
$(error The 'TILERA_ROOT' environment variable is not set.)
endif

CC = gcc

endif


OPT    = -O2
CFLAGS = -std=gnu99 -static -Wall -Werror -g $(OPT) -I../../include
LIBS   = -lgxcr -ltmc -lrt -lpthread -static

all: functional_test speed_test


pka_test_utils.o: pka_test_utils.c
	$(CC) $(CFLAGS) -c -o $@ pka_test_utils.c


functional_test.o: functional_test.c
	$(CC) $(CFLAGS) -c -o $@ functional_test.c

functional_test: functional_test.o pka_test_utils.o
	$(CC) $(LDFLAGS) -o $@ functional_test.o pka_test_utils.o $(LIBS)


speed_test.o: speed_test.c
	$(CC) $(CFLAGS) -c -o $@ speed_test.c

speed_test: speed_test.o pka_test_utils.o
	$(CC) $(LDFLAGS) -o $@ speed_test.o pka_test_utils.o -lm $(LIBS)

clean:
	rm -f *.o functional_test speed_test

.PHONY: all clean

