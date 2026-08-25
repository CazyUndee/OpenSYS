# Plan 0 Test Suite Makefile
#
# Copyright (C) 2026 CazyUndee

# Tools
CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99 -I../include

# Directories
TESTDIR = .
UNITDIR = $(TESTDIR)/unit
INTEGRATIONDIR = $(TESTDIR)/integration
MOCKDIR = $(TESTDIR)/mocks
OBJDIR = obj
BINDIR = bin

# Source files
TEST_FRAMEWORK_SRCS = test_framework.c test_runner.c
UNIT_TEST_SRCS = $(UNITDIR)/test_memory.c $(UNITDIR)/test_filesystem.c $(UNITDIR)/test_process.c $(UNITDIR)/test_nl_parser.c $(UNITDIR)/test_vfile.c $(UNITDIR)/test_ramfs.c $(UNITDIR)/test_part.c $(UNITDIR)/test_ns.c $(UNITDIR)/test_fs_volume.c
INTEGRATION_TEST_SRCS = $(INTEGRATIONDIR)/test_kernel_integration.c
MOCK_SRCS = $(MOCKDIR)/mock_kernel.c $(MOCKDIR)/mock_hardware.c

# Real implementations compiled into the host tests (no kernel dependencies)
REAL_SRCS = ../src/ui/nl_parser.c ../src/kernel/kstring.c ../src/fs/vfile.c ../src/fs/ramfs.c ../src/fs/vfs.c ../src/fs/fs_vfs.c ../src/drivers/part.c ../src/fs/ns.c ../src/fs/volume.c

ALL_SRCS = $(TEST_FRAMEWORK_SRCS) $(UNIT_TEST_SRCS) $(INTEGRATION_TEST_SRCS) $(MOCK_SRCS) $(REAL_SRCS)

# Object files
TEST_FRAMEWORK_OBJS = $(TEST_FRAMEWORK_SRCS:%.c=$(OBJDIR)/%.o)
UNIT_TEST_OBJS = $(UNIT_TEST_SRCS:%.c=$(OBJDIR)/%.o)
INTEGRATION_TEST_OBJS = $(INTEGRATION_TEST_SRCS:%.c=$(OBJDIR)/%.o)
MOCK_OBJS = $(MOCK_SRCS:%.c=$(OBJDIR)/%.o)
REAL_OBJS = $(REAL_SRCS:../src/%.c=$(OBJDIR)/real/%.o)

ALL_OBJS = $(TEST_FRAMEWORK_OBJS) $(UNIT_TEST_OBJS) $(INTEGRATION_TEST_OBJS) $(MOCK_OBJS) $(REAL_OBJS)

# Targets
TEST_RUNNER = $(BINDIR)/test_runner

# Phony targets
.PHONY: all clean test unit integration help

# Default target
all: $(TEST_RUNNER)

# Create directories
$(OBJDIR):
	@mkdir -p $(OBJDIR)
	@mkdir -p $(OBJDIR)/unit
	@mkdir -p $(OBJDIR)/integration
	@mkdir -p $(OBJDIR)/mocks

$(BINDIR):
	@mkdir -p $(BINDIR)

# Object files
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/unit/%.o: $(UNITDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/integration/%.o: $(INTEGRATIONDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/mocks/%.o: $(MOCKDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/real/%.o: ../src/%.c | $(OBJDIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Test runner executable
$(TEST_RUNNER): $(ALL_OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(ALL_OBJS)
	@echo "Test runner built: $@"

# Run all tests
test: $(TEST_RUNNER)
	@echo "Running all tests..."
	@$(TEST_RUNNER)

# Run unit tests only
unit: $(TEST_RUNNER)
	@echo "Running unit tests..."
	@$(TEST_RUNNER) --suite "Memory Management"
	@$(TEST_RUNNER) --suite "Filesystem"
	@$(TEST_RUNNER) --suite "Process Management"

# Run integration tests only
integration: $(TEST_RUNNER)
	@echo "Running integration tests..."
	@$(TEST_RUNNER) --suite "Kernel Integration"

# Run specific test suite
test-memory: $(TEST_RUNNER)
	@echo "Running memory tests..."
	@$(TEST_RUNNER) --suite "Memory Management"

test-filesystem: $(TEST_RUNNER)
	@echo "Running filesystem tests..."
	@$(TEST_RUNNER) --suite "Filesystem"

test-process: $(TEST_RUNNER)
	@echo "Running process tests..."
	@$(TEST_RUNNER) --suite "Process Management"

test-integration: $(TEST_RUNNER)
	@echo "Running integration tests..."
	@$(TEST_RUNNER) --suite "Kernel Integration"

# Verbose test output
test-verbose: $(TEST_RUNNER)
	@echo "Running all tests (verbose)..."
	@$(TEST_RUNNER) --verbose

# Quiet test output
test-quiet: $(TEST_RUNNER)
	@echo "Running all tests (quiet)..."
	@$(TEST_RUNNER) --quiet

# List available test suites
list-tests: $(TEST_RUNNER)
	@$(TEST_RUNNER) --list

# Help target
help:
	@echo "Plan 0 Test Suite Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build test runner (default)"
	@echo "  test             - Run all tests"
	@echo "  unit             - Run unit tests only"
	@echo "  integration      - Run integration tests only"
	@echo "  test-memory      - Run memory management tests"
	@echo "  test-filesystem  - Run filesystem tests"
	@echo "  test-process     - Run process management tests"
	@echo "  test-integration - Run integration tests"
	@echo "  test-verbose     - Run all tests with verbose output"
	@echo "  test-quiet       - Run all tests with quiet output"
	@echo "  list-tests       - List available test suites"
	@echo "  clean            - Clean build artifacts"
	@echo "  help             - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make test                    # Run all tests"
	@echo "  make test-memory             # Run memory tests only"
	@echo "  make test-verbose            # Run with verbose output"
	@echo "  make clean && make test      # Clean and rebuild"

# Clean build artifacts
clean:
	@rm -rf $(OBJDIR) $(BINDIR)
	@echo "Cleaned test build artifacts"

# Show build configuration
info:
	@echo "Plan 0 Test Suite Configuration"
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "Test runner: $(TEST_RUNNER)"
	@echo "Source files: $(ALL_SRCS)"
	@echo "Object files: $(ALL_OBJS)"

# Dependencies (simple approach for now)
$(OBJDIR)/test_framework.o: test_framework.c test_framework.h
$(OBJDIR)/test_runner.o: test_runner.c test_framework.h
$(OBJDIR)/unit/test_memory.o: $(UNITDIR)/test_memory.c test_framework.h ../include/memory.h
$(OBJDIR)/unit/test_filesystem.o: $(UNITDIR)/test_filesystem.c test_framework.h ../include/fs.h
$(OBJDIR)/unit/test_process.o: $(UNITDIR)/test_process.c test_framework.h ../include/process.h
$(OBJDIR)/unit/test_nl_parser.o: $(UNITDIR)/test_nl_parser.c test_framework.h ../include/nl_parser.h
$(OBJDIR)/real/ui/nl_parser.o: ../src/ui/nl_parser.c ../include/nl_parser.h ../include/kstring.h
$(OBJDIR)/real/kernel/kstring.o: ../src/kernel/kstring.c ../include/kstring.h
$(OBJDIR)/integration/test_kernel_integration.o: $(INTEGRATIONDIR)/test_kernel_integration.c test_framework.h ../include/memory.h ../include/process.h ../include/fs.h
$(OBJDIR)/mocks/mock_kernel.o: $(MOCKDIR)/mock_kernel.c ../include/memory.h ../include/pmm.h ../include/kheap.h
$(OBJDIR)/mocks/mock_hardware.o: $(MOCKDIR)/mock_hardware.c $(MOCKDIR)/mock_hardware.h
