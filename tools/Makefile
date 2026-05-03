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

$(TEST_D64): $(OUT_DIR)/hello.prg | $(OUT_DIR)
	$(PYTHON) $(TOOL_DIR)make_d64.py \
	    -o $@ \
	    -n "IEC TEST DISK" \
	    -i "01" \
	    $(OUT_DIR)/hello.prg
	@echo ""
	@echo "Test disk ready: $@"
	@echo "In emulator: LOAD\"*\",8  then RUN"

clean:
	rm -rf $(OUT_DIR)
