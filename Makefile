CC = gcc
#LIBMILL_INCLUDE_X = -I$(HOME)/opt/include
#LIBMILL_LIB_X = -L$(HOME)/opt/lib
CFLAGS = -Wall -c -O2 -g -I. $(LIBMILL_INCLUDE_X)
LIBS = $(LIBMILL_LIB_X) -lmill -lssl -lcrypto

all: ssl_test

ssl_test: mssl.o ssl_test.o
	$(CC) -o $@ $^ $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f *.o
	rm -f ssl_test
