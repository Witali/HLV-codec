NODE ?= node

SOURCES := \
	src/bpv1-codec.js \
	src/bpv1-auto-palette.js \
	src/bpv1-rate-distortion.js \
	tools/bpv1-file.js \
	tools/y4m.js \
	tools/bpv1enc.js \
	tools/bpv1dec.js \
	tools/bpv1info.js

all: check

check:
	@for file in $(SOURCES); do $(NODE) --check $$file || exit 1; done

test: check
	$(NODE) tests/bpv1-codec.test.js
	$(NODE) tests/bpv1-auto-palette.test.js
	$(NODE) tests/bpv1-rate-distortion.test.js
	$(NODE) tests/bpv1-cli.test.js

clean:
	@:

.PHONY: all check test clean
