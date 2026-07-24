HLV_DIR := codecs/hlv
BPV_DIR := codecs/bpv
HLV_TARGETS := libhlv1.a hlvenc hlvdec hlvinfo hlvbenchdec hlvpeakdec \
	test_roundtrip test_errors

all: hlv bpv

hlv:
	$(MAKE) -C $(HLV_DIR) all

bpv:
	$(MAKE) -C $(BPV_DIR) all

$(HLV_TARGETS):
	$(MAKE) -C $(HLV_DIR) $@

test:
	$(MAKE) -C $(HLV_DIR) test
	$(MAKE) -C $(BPV_DIR) test

test-bpv:
	$(MAKE) -C $(BPV_DIR) test

test-windowed-rate:
	$(MAKE) -C $(HLV_DIR) test-windowed-rate

test-threaded:
	$(MAKE) -C $(HLV_DIR) test-threaded

sanitize:
	$(MAKE) -C $(HLV_DIR) sanitize

clean:
	$(MAKE) -C $(HLV_DIR) clean
	$(MAKE) -C $(BPV_DIR) clean

.PHONY: all hlv bpv $(HLV_TARGETS) test test-bpv test-windowed-rate \
	test-threaded sanitize clean
