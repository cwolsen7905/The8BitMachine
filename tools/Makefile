PYTHON   := python3
TOOL_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
OUT_DIR  := $(TOOL_DIR)out

.PHONY: all clean test_disk

all: test_disk

# ── Test disk ──────────────────────────────────────────────────────────────
TEST_D64 := $(OUT_DIR)/test.d64

test_disk: $(TEST_D64)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(OUT_DIR)/hello.prg: $(TOOL_DIR)test_disk/hello.bas | $(OUT_DIR)
	$(PYTHON) $(TOOL_DIR)basic_prg.py $< $@

$(OUT_DIR)/bigtest.prg: $(TOOL_DIR)test_disk/bigtest.bas | $(OUT_DIR)
	$(PYTHON) $(TOOL_DIR)basic_prg.py $< $@

# 2KB binary at load addr $C000, pattern byte[i] = i % 256
# Expected checksum: sum(i%256 for i in range(2048)) = 8 * 32640 = 261120
$(OUT_DIR)/bigdata.prg: | $(OUT_DIR)
	$(PYTHON) -c "import struct; data=bytes([i%256 for i in range(2048)]); open('$@','wb').write(struct.pack('<H',0xC000)+data)"

$(TEST_D64): $(OUT_DIR)/hello.prg $(OUT_DIR)/bigtest.prg $(OUT_DIR)/bigdata.prg | $(OUT_DIR)
	$(PYTHON) $(TOOL_DIR)make_d64.py \
	    -o $@ \
	    -n "IEC TEST DISK" \
	    -i "01" \
	    $(OUT_DIR)/hello.prg \
	    $(OUT_DIR)/bigtest.prg \
	    $(OUT_DIR)/bigdata.prg
	@echo ""
	@echo "Test disk ready: $@"
	@echo "Files on disk:"
	@echo "  HELLO    - quick IEC smoke test"
	@echo "  BIGTEST  - 2KB load + CRC verify (run after LOAD)"
	@echo "  BIGDATA  - raw 2KB data file (loaded by BIGTEST)"

clean:
	rm -rf $(OUT_DIR)
