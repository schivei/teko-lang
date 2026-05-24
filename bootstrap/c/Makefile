CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2

BUILD_DIR := build
TARGET := $(BUILD_DIR)/teko-bootstrap

CORE_SRCS := \
	core/backend_c.c \
	core/compile.c \
	core/diagnostics.c \
	core/lexer.c \
	core/parser.c \
	core/strings.c

PLATFORM_SRCS := platform/filesystem.c
CLI_SRCS := cli/main.c
SRCS := $(CORE_SRCS) $(PLATFORM_SRCS) $(CLI_SRCS)
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Icore -Iplatform -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
