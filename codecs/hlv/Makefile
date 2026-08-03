CC ?= cc
AR ?= ar
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic
THREADFLAGS ?= -pthread
CFLAGS += $(THREADFLAGS)
CPPFLAGS += -Iinclude -I../common/include
LDLIBS += -lm $(THREADFLAGS)

LIBSRC = src/hlv1_common.c src/hlv1_y4m.c src/hlv1_encode.c src/hlv1_decode.c
LIBOBJ = $(LIBSRC:.c=.o)
TOOLS = hlvenc hlvdec hlvinfo hlvbenchdec hlvpeakdec
IMAOBJ = ima_adpcm.o

all: libhlv1.a $(TOOLS)

libhlv1.a: $(LIBOBJ)
	$(AR) rcs $@ $^

ima_adpcm.o: ../common/src/ima_adpcm.c ../common/include/ima_adpcm.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

hlvenc: tools/hlvenc.o $(IMAOBJ) libhlv1.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

hlvdec: tools/hlvdec.o $(IMAOBJ) libhlv1.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

hlvinfo: tools/hlvinfo.o libhlv1.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

hlvbenchdec: tools/hlvbenchdec.o libhlv1.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

hlvpeakdec: tools/hlvpeakdec.o libhlv1.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_roundtrip: tests/test_roundtrip.o libhlv1.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_errors: tests/test_errors.o libhlv1.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: test_roundtrip test_errors
	./test_roundtrip
	./test_errors

test-windowed-rate: all
	python3 ../../scripts/test_windowed_two_pass.py --project .

test-threaded: all
	python3 ../../scripts/test_threaded_encode.py --encoder ./hlvenc

sanitize:
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined \
		-Iinclude -o test_roundtrip_san tests/test_roundtrip.c $(LIBSRC) $(LDLIBS)
	./test_roundtrip_san
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined \
		-Iinclude -o test_errors_san tests/test_errors.c $(LIBSRC) $(LDLIBS)
	./test_errors_san

clean:
	rm -f $(LIBOBJ) $(IMAOBJ) tools/*.o tests/*.o libhlv1.a $(TOOLS) test_roundtrip test_errors test_roundtrip_san test_errors_san

.PHONY: all test test-windowed-rate test-threaded sanitize clean
