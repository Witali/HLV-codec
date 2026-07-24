HLV_DIR := codecs/hlv
HLV_TARGETS := libhlv1.a hlvenc hlvdec hlvinfo hlvbenchdec hlvpeakdec \
	test_roundtrip test_errors

all: hlv

hlv:
	$(MAKE) -C $(HLV_DIR) all

$(HLV_TARGETS):
	$(MAKE) -C $(HLV_DIR) $@

test:
	$(MAKE) -C $(HLV_DIR) test

test-windowed-rate:
	$(MAKE) -C $(HLV_DIR) test-windowed-rate

test-threaded:
	$(MAKE) -C $(HLV_DIR) test-threaded

sanitize:
	$(MAKE) -C $(HLV_DIR) sanitize

clean:
	$(MAKE) -C $(HLV_DIR) clean

.PHONY: all hlv $(HLV_TARGETS) test test-windowed-rate test-threaded sanitize clean
