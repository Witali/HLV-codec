NODE ?= node
CC ?= cc
CFLAGS ?= -O3 -Wall -Wextra -std=c11

SOURCES := \
	src/bpv1-codec.js \
	src/bpv1-auto-palette.js \
	src/bpv1-rate-distortion.js \
	tools/bpv1-file.js \
	tools/y4m.js \
	tools/bpv1enc.js \
	tools/bpv1dec.js \
	tools/bpv1info.js

all: bpv1enc test_bpv1_decoder check

bpv1enc: tools/bpv1enc.c
	$(CC) $(CFLAGS) -pthread -o $@ $< -lm

test_bpv1_decoder: tests/test_bpv1_decoder.c src/bpv1_decode.c include/bpv1.h
	$(CC) $(CFLAGS) -Iinclude -o $@ tests/test_bpv1_decoder.c src/bpv1_decode.c

check:
	@for file in $(SOURCES); do $(NODE) --check $$file || exit 1; done

test: check
	$(NODE) tests/bpv1-codec.test.js
	$(NODE) tests/bpv1-auto-palette.test.js
	$(NODE) tests/bpv1-rate-distortion.test.js
	$(NODE) tests/bpv1-cli.test.js

test-c: bpv1enc test_bpv1_decoder
	./test_bpv1_decoder
	BPV1ENC=./bpv1enc $(NODE) tests/bpv1-c-encoder.test.js

clean:
	rm -f bpv1enc test_bpv1_decoder

.PHONY: all check test test-c clean
