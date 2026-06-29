CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99 -pedantic -O2 -g -fPIC
LDFLAGS = -lm

SRC_DIR = src
LIB_DIR = $(SRC_DIR)/lib
CLI_DIR = $(SRC_DIR)/cli
TEST_DIR = tests
BUILD_DIR = build
INCLUDE_DIR = include

# Library sources (core logic)
LIB_SOURCES = $(wildcard $(LIB_DIR)/*.c)
LIB_OBJECTS = $(LIB_SOURCES:$(LIB_DIR)/%.c=$(BUILD_DIR)/lib/%.o)

# CLI sources (main + CLI-specific)
CLI_SOURCES = $(wildcard $(CLI_DIR)/*.c)
CLI_OBJECTS = $(CLI_SOURCES:$(CLI_DIR)/%.c=$(BUILD_DIR)/cli/%.o)

# Test sources (excluding integration test which has its own main)
TEST_SOURCES = $(filter-out $(TEST_DIR)/test_integration.c,$(wildcard $(TEST_DIR)/*.c))
TEST_OBJECTS = $(TEST_SOURCES:$(TEST_DIR)/%.c=$(BUILD_DIR)/test/%.o)

# Integration test (standalone with its own main)
INTEGRATION_TEST_SRC = $(TEST_DIR)/test_integration.c
INTEGRATION_TEST_OBJ = $(BUILD_DIR)/test/test_integration.o
INTEGRATION_TEST_TARGET = $(BUILD_DIR)/test/integration_test

# Targets (platform-specific defaults set below)
TEST_TARGET = $(BUILD_DIR)/test/test_runner

# Platform-specific library extension
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SHARED_LIB_BASE = libtaguchi.dylib
    SHARED_FLAGS = -dynamiclib -install_name @rpath/$(SHARED_LIB_BASE)
else ifeq ($(OS),Windows_NT)
    SHARED_LIB_BASE = taguchi.dll
    SHARED_FLAGS = -shared
else
    SHARED_LIB_BASE = libtaguchi.so
    SHARED_FLAGS = -shared -Wl,-soname,$(SHARED_LIB_BASE)
endif
SHARED_LIB ?= $(BUILD_DIR)/$(SHARED_LIB_BASE)
STATIC_LIB ?= $(BUILD_DIR)/libtaguchi.a
CLI_TARGET ?= $(BUILD_DIR)/taguchi

.PHONY: all lib cli test test-python test-all check install install-cli reinstall uninstall clean

all: lib cli

# Build shared library
lib: $(SHARED_LIB) $(STATIC_LIB)

$(SHARED_LIB): $(LIB_OBJECTS)
	$(CC) $(SHARED_FLAGS) $(LIB_OBJECTS) -o $@ $(LDFLAGS)

$(STATIC_LIB): $(LIB_OBJECTS)
	ar rcs $@ $(LIB_OBJECTS)

$(BUILD_DIR)/lib/%.o: $(LIB_DIR)/%.c | $(BUILD_DIR)/lib
	$(CC) $(CFLAGS) -I. -I$(INCLUDE_DIR) -c $< -o $@

# Build CLI (statically linked — no runtime dependency on libtaguchi.so)
cli: $(CLI_TARGET)

$(CLI_TARGET): $(CLI_OBJECTS) $(STATIC_LIB)
	$(CC) $(CLI_OBJECTS) $(STATIC_LIB) -o $@ $(LDFLAGS)

$(BUILD_DIR)/cli/%.o: $(CLI_DIR)/%.c | $(BUILD_DIR)/cli
	$(CC) $(CFLAGS) -I. -I$(INCLUDE_DIR) -I$(LIB_DIR) -c $< -o $@

# Build tests
# The unit test runner links lib objects directly (no shared lib needed).
# The integration test uses the shared lib, so it still needs LD_LIBRARY_PATH.
test: $(TEST_TARGET) $(INTEGRATION_TEST_TARGET) $(CLI_TARGET)
	./$(TEST_TARGET)
	@echo "Running integration test..."
	LD_LIBRARY_PATH=$(BUILD_DIR) ./$(INTEGRATION_TEST_TARGET)
	@echo "Running CSV multi-column metric tests..."
	@bash $(TEST_DIR)/test_csv_multicolumn.sh
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "Running valgrind..."; \
		valgrind --leak-check=full --error-exitcode=1 ./$(TEST_TARGET); \
	else \
		echo "Warning: valgrind not found, skipping memory check."; \
	fi

$(TEST_TARGET): $(TEST_OBJECTS) $(LIB_OBJECTS)
	$(CC) $(TEST_OBJECTS) $(LIB_OBJECTS) -o $@ $(LDFLAGS)

$(INTEGRATION_TEST_TARGET): $(INTEGRATION_TEST_OBJ) $(LIB_OBJECTS)
	$(CC) $(INTEGRATION_TEST_OBJ) $(LIB_OBJECTS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/test/%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)/test
	$(CC) $(CFLAGS) -I. -I$(INCLUDE_DIR) -c $< -o $@

# Static analysis
check: test
	@echo "Running static analysis..."
	cppcheck --enable=all --suppress=missingIncludeSystem $(LIB_DIR) $(CLI_DIR)

# Create build directories
$(BUILD_DIR)/lib $(BUILD_DIR)/cli $(BUILD_DIR)/test:
	mkdir -p $@

# Installation
PREFIX ?= /usr/local
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include
BINDIR = $(PREFIX)/bin

# install-cli: install only the static binary — no library dependency required.
#   Use this when you just want the 'taguchi' command-line tool.
install-cli: cli
	install -d $(BINDIR)
	install -m 755 $(CLI_TARGET) $(BINDIR)/

# install: full installation — static binary + shared lib + static lib + header.
#   Use this when building language bindings (Python, Node) that link against
#   libtaguchi at runtime.
install: lib cli
	install -d $(LIBDIR) $(INCDIR) $(BINDIR)
	install -m 755 $(SHARED_LIB) $(LIBDIR)/$(SHARED_LIB_BASE)
	install -m 644 $(STATIC_LIB) $(LIBDIR)/
	install -m 644 $(INCLUDE_DIR)/taguchi.h $(INCDIR)/
	install -m 755 $(CLI_TARGET) $(BINDIR)/
	@if command -v ldconfig >/dev/null 2>&1; then ldconfig; fi

# Reinstall (uninstall then install)
reinstall: uninstall install

# Uninstall
uninstall:
	rm -f $(LIBDIR)/$(SHARED_LIB_BASE)
	rm -f $(LIBDIR)/libtaguchi.a
	rm -f $(INCDIR)/taguchi.h
	rm -f $(BINDIR)/taguchi

# Clean
clean:
	rm -rf $(BUILD_DIR)

# Python bindings
.PHONY: python-install python-test test-python test-all
python-install: lib
	cd bindings/python && python3 setup.py install --user

python-test: python-install
	cd bindings/python && python3 test_taguchi.py

# Run the pytest-based Python test suite (requires: pip install pytest in bindings/python/.venv)
test-python: lib cli
	@if [ -f bindings/python/.venv/bin/pytest ]; then \
		cd bindings/python && .venv/bin/pytest tests/ -v; \
	elif command -v pytest >/dev/null 2>&1; then \
		cd bindings/python && pytest tests/ -v; \
	else \
		echo "No pytest found. Run: cd bindings/python && python3 -m venv .venv && .venv/bin/pip install -e '.[dev]'"; \
		exit 1; \
	fi

# Run both C and Python test suites
test-all: test test-python

# Node bindings
.PHONY: node-install node-test
node-install: lib
	cd bindings/node && npm install

node-test: node-install
	cd bindings/node && npm test

# Documentation
.PHONY: docs
docs:
	doxygen Doxyfile
