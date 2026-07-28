CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -pedantic
CPPFLAGS += -Iinclude

SOURCES = src/divx3_decode.c src/divx3_avi.c

.PHONY: all clean

all: divx3dec

divx3dec: tools/divx3dec.c $(SOURCES) include/divx3.h include/divx3_avi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tools/divx3dec.c $(SOURCES)

test_decode: tests/test_decode.c $(SOURCES) include/divx3.h include/divx3_avi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_decode.c $(SOURCES)

test_stream_decode: tests/test_stream_decode.c $(SOURCES) include/divx3.h include/divx3_avi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/test_stream_decode.c $(SOURCES)

clean:
	$(RM) divx3dec test_decode test_stream_decode
