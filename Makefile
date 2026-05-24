BOOTSTRAP_DIR := bootstrap/c
BOOTSTRAP_BIN := $(BOOTSTRAP_DIR)/build/teko-bootstrap
BUILD_DIR := build
CORE0_C := $(BUILD_DIR)/core0.c
CORE0_BIN := $(BUILD_DIR)/core0

CORE0_SOURCES := \
	examples/core0/Point.struct.teko \
	examples/core0/TokenKind.enum.teko \
	examples/core0/main.teko

.PHONY: all bootstrap example-core0 check clean

all: bootstrap

bootstrap:
	$(MAKE) -C $(BOOTSTRAP_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(CORE0_C): bootstrap $(CORE0_SOURCES) | $(BUILD_DIR)
	$(BOOTSTRAP_BIN) -o $(CORE0_C) $(CORE0_SOURCES)

$(CORE0_BIN): $(CORE0_C)
	$(CC) -std=c99 -Wall -Wextra -pedantic $(CORE0_C) -o $(CORE0_BIN)

example-core0: $(CORE0_BIN)

check: example-core0
	$(CORE0_BIN)

clean:
	$(MAKE) -C $(BOOTSTRAP_DIR) clean
	rm -rf $(BUILD_DIR)
