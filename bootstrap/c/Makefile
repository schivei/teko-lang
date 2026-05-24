CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2
DEPFLAGS := -MMD -MP

BUILD_DIR := build
TARGET := $(BUILD_DIR)/teko-bootstrap

CORE_SRCS := \
	core/backend_c.c \
	core/checker.c \
	core/compile.c \
	core/diagnostics.c \
	core/lexer.c \
	core/parser.c \
	core/strings.c

PLATFORM_SRCS := platform/filesystem.c
CLI_SRCS := cli/main.c
TEST_SRCS := tests/core_tests.c
SRCS := $(CORE_SRCS) $(PLATFORM_SRCS) $(CLI_SRCS)
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)
CORE_OBJS := $(CORE_SRCS:%.c=$(BUILD_DIR)/%.o)
TEST_OBJS := $(TEST_SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)
TEST_TARGET := $(BUILD_DIR)/teko-core-tests

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(TEST_TARGET): $(CORE_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(CORE_OBJS) $(TEST_OBJS)

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -Icore -Iplatform -c $< -o $@

test: $(TEST_TARGET)
	$(TEST_TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
