CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Itests
LDFLAGS ?=

# Prefer repo-local toolchain on Windows (after scripts/bootstrap-toolchain.ps1)
ifeq ($(OS),Windows_NT)
LOCAL_GCC := toolchain/mingw64/bin/gcc.exe
GLOBAL_GCC := /c/Program Files/Nerva/toolchain/mingw64/bin/gcc.exe
ifneq ("$(wildcard $(LOCAL_GCC))","")
CC := $(LOCAL_GCC)
else ifneq ("$(wildcard $(GLOBAL_GCC))","")
CC := $(GLOBAL_GCC)
endif
endif

BUILD_DIR = build
SRC_DIR = src
TEST_DIR = tests
TOOLS_DIR = tools

LIB_SRCS = \
	$(SRC_DIR)/nerva_graph.c \
	$(SRC_DIR)/nerva_engine.c \
	$(SRC_DIR)/nerva_event.c \
	$(SRC_DIR)/nerva_debug.c \
	$(SRC_DIR)/nerva_trace.c \
	$(SRC_DIR)/nerva_mutation.c \
	$(SRC_DIR)/nerva_learning.c \
	$(SRC_DIR)/nerva_prediction.c \
	$(SRC_DIR)/nerva_exception.c \
	$(SRC_DIR)/nerva_schema.c \
	$(SRC_DIR)/nerva_memory.c \
	$(SRC_DIR)/nerva_routing.c \
	$(SRC_DIR)/nerva_parse.c \
	$(SRC_DIR)/nerva_persist.c \
	$(SRC_DIR)/nerva_bench.c
LIB_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))

TEST_SRCS = \
	$(TEST_DIR)/test_runner.c \
	$(TEST_DIR)/test_graph.c \
	$(TEST_DIR)/test_event.c \
	$(TEST_DIR)/test_trace.c \
	$(TEST_DIR)/test_learning.c \
	$(TEST_DIR)/test_prediction.c \
	$(TEST_DIR)/test_exception.c \
	$(TEST_DIR)/test_schema.c \
	$(TEST_DIR)/test_memory.c \
	$(TEST_DIR)/test_routing.c \
	$(TEST_DIR)/test_parse.c \
	$(TEST_DIR)/test_persist.c \
	$(TEST_DIR)/test_bench.c \
	$(TEST_DIR)/nerva_test_fixtures.c
TEST_OBJS = $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/%.o,$(TEST_SRCS))

ifeq ($(OS),Windows_NT)
TEST_BIN = $(BUILD_DIR)/test_runner.exe
CLI_BIN = $(BUILD_DIR)/nerva_cli.exe
BENCH_BIN = $(BUILD_DIR)/nerva_bench.exe
else
TEST_BIN = $(BUILD_DIR)/test_runner
CLI_BIN = $(BUILD_DIR)/nerva_cli
BENCH_BIN = $(BUILD_DIR)/nerva_bench
endif

.PHONY: all test cli bench clean bootstrap-toolchain

all: test cli bench

bootstrap-toolchain:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bootstrap-toolchain.ps1

test: $(TEST_BIN)
	./$(TEST_BIN)

cli: $(CLI_BIN)

bench: $(BENCH_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) $(LIB_OBJS) $(TEST_OBJS) -o $@ $(LDFLAGS)

$(CLI_BIN): $(LIB_OBJS) $(TOOLS_DIR)/nerva_cli.c
	$(CC) $(CFLAGS) $(TOOLS_DIR)/nerva_cli.c $(LIB_OBJS) -o $@ $(LDFLAGS)

$(BENCH_BIN): $(LIB_OBJS) $(TOOLS_DIR)/nerva_bench.c
	$(CC) $(CFLAGS) $(TOOLS_DIR)/nerva_bench.c $(LIB_OBJS) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
