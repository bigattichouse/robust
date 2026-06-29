# Robust toolkit — top-level build.
# Builds the shared common/ core (libdoe) and the tool binaries that exist so
# far. Tools are added per the DESIGN.md roadmap (M2: morris, M3: sobol).

CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -std=c99 -pedantic -O2 -g -fPIC
LDFLAGS = -lm

BUILD       = build
BIN         = $(BUILD)/bin
COMMON_DIR  = common
COMMON_INC  = -I$(COMMON_DIR)/include
COMMON_SRC  = $(wildcard $(COMMON_DIR)/src/*.c)
COMMON_OBJ  = $(COMMON_SRC:$(COMMON_DIR)/src/%.c=$(BUILD)/common/%.o)
COMMON_LIB  = $(BUILD)/libdoe.a

# morris tool
MORRIS_INC      = -Imorris/include
MORRIS_LIB_SRC  = $(wildcard morris/src/lib/*.c)
MORRIS_LIB_OBJ  = $(MORRIS_LIB_SRC:morris/src/lib/%.c=$(BUILD)/morris/lib/%.o)
MORRIS_CLI_SRC  = $(wildcard morris/src/cli/*.c)
MORRIS_CLI_OBJ  = $(MORRIS_CLI_SRC:morris/src/cli/%.c=$(BUILD)/morris/cli/%.o)
MORRIS_BIN      = $(BIN)/morris
MORRIS_TEST_SRC = $(wildcard morris/tests/*.c)
MORRIS_TEST_BIN = $(BUILD)/test_morris

# sobol tool
SOBOL_INC       = -Isobol/include
SOBOL_LIB_SRC   = $(wildcard sobol/src/lib/*.c)
SOBOL_LIB_OBJ   = $(SOBOL_LIB_SRC:sobol/src/lib/%.c=$(BUILD)/sobol/lib/%.o)
SOBOL_CLI_SRC   = $(wildcard sobol/src/cli/*.c)
SOBOL_CLI_OBJ   = $(SOBOL_CLI_SRC:sobol/src/cli/%.c=$(BUILD)/sobol/cli/%.o)
SOBOL_BIN       = $(BIN)/sobol
SOBOL_TEST_SRC  = $(wildcard sobol/tests/*.c)
SOBOL_TEST_BIN  = $(BUILD)/test_sobol

# core test suite
CORE_TEST_SRC = $(wildcard $(COMMON_DIR)/tests/*.c)
CORE_TEST_BIN = $(BUILD)/test_doe

.PHONY: all common morris sobol taguchi tools test test-taguchi test-all clean

all: common morris sobol taguchi

# ---- common core --------------------------------------------------------
common: $(COMMON_LIB)

$(COMMON_LIB): $(COMMON_OBJ)
	ar rcs $@ $^

$(BUILD)/common/%.o: $(COMMON_DIR)/src/%.c | $(BUILD)/common
	$(CC) $(CFLAGS) $(COMMON_INC) -c $< -o $@

# ---- morris -------------------------------------------------------------
morris: $(MORRIS_BIN)

$(MORRIS_BIN): $(MORRIS_CLI_OBJ) $(MORRIS_LIB_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD)/morris/lib/%.o: morris/src/lib/%.c | $(BUILD)/morris/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(MORRIS_INC) -c $< -o $@

$(BUILD)/morris/cli/%.o: morris/src/cli/%.c | $(BUILD)/morris/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(MORRIS_INC) -c $< -o $@

# ---- sobol --------------------------------------------------------------
sobol: $(SOBOL_BIN)

$(SOBOL_BIN): $(SOBOL_CLI_OBJ) $(SOBOL_LIB_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD)/sobol/lib/%.o: sobol/src/lib/%.c | $(BUILD)/sobol/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(SOBOL_INC) -c $< -o $@

$(BUILD)/sobol/cli/%.o: sobol/src/cli/%.c | $(BUILD)/sobol/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(SOBOL_INC) -c $< -o $@

# ---- taguchi (vendored peer tool, builds with its own Makefile) ---------
taguchi:
	$(MAKE) -C taguchi

# ---- tests --------------------------------------------------------------
test: $(CORE_TEST_BIN) $(MORRIS_TEST_BIN) $(SOBOL_TEST_BIN)
	./$(CORE_TEST_BIN)
	./$(MORRIS_TEST_BIN)
	./$(SOBOL_TEST_BIN)
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "Running valgrind..."; \
		valgrind --leak-check=full --error-exitcode=1 ./$(CORE_TEST_BIN)   >/dev/null 2>&1 && echo "  test_doe: clean"; \
		valgrind --leak-check=full --error-exitcode=1 ./$(MORRIS_TEST_BIN) >/dev/null 2>&1 && echo "  test_morris: clean"; \
		valgrind --leak-check=full --error-exitcode=1 ./$(SOBOL_TEST_BIN)  >/dev/null 2>&1 && echo "  test_sobol: clean"; \
	else \
		echo "valgrind not found, skipping memory check."; \
	fi

$(CORE_TEST_BIN): $(CORE_TEST_SRC) $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) -I$(COMMON_DIR)/tests $(CORE_TEST_SRC) $(COMMON_OBJ) -o $@ $(LDFLAGS)

$(MORRIS_TEST_BIN): $(MORRIS_TEST_SRC) $(MORRIS_LIB_OBJ) $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) $(MORRIS_INC) -I$(COMMON_DIR)/tests $(MORRIS_TEST_SRC) $(MORRIS_LIB_OBJ) $(COMMON_OBJ) -o $@ $(LDFLAGS)

$(SOBOL_TEST_BIN): $(SOBOL_TEST_SRC) $(SOBOL_LIB_OBJ) $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) $(SOBOL_INC) -I$(COMMON_DIR)/tests $(SOBOL_TEST_SRC) $(SOBOL_LIB_OBJ) $(COMMON_OBJ) -o $@ $(LDFLAGS)

# ---- aggregate targets --------------------------------------------------
tools:
	@echo "Built: morris, sobol, taguchi. Pending: robust, ofat, grid, report (see DESIGN.md)."

test-taguchi:
	$(MAKE) -C taguchi test

test-all: test test-taguchi

# ---- housekeeping -------------------------------------------------------
$(BUILD) $(BIN) $(BUILD)/common $(BUILD)/morris/lib $(BUILD)/morris/cli $(BUILD)/sobol/lib $(BUILD)/sobol/cli:
	mkdir -p $@

clean:
	rm -rf $(BUILD)
