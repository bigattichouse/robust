# Robust toolkit — top-level build.
# Builds the shared common/ core (libdoe) and the tool binaries. Tools land per
# the DESIGN.md roadmap (M2: morris, M3: sobol, M4: robust orchestrator).

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

# robust orchestrator (links the morris + sobol libs to drive the funnel in-process)
ROBUST_INC      = -Irobust/include $(MORRIS_INC) $(SOBOL_INC)
ROBUST_LIB_SRC  = $(wildcard robust/src/lib/*.c)
ROBUST_LIB_OBJ  = $(ROBUST_LIB_SRC:robust/src/lib/%.c=$(BUILD)/robust/lib/%.o)
ROBUST_CLI_SRC  = $(wildcard robust/src/cli/*.c)
ROBUST_CLI_OBJ  = $(ROBUST_CLI_SRC:robust/src/cli/%.c=$(BUILD)/robust/cli/%.o)
ROBUST_BIN      = $(BIN)/robust
ROBUST_TEST_SRC = $(wildcard robust/tests/*.c)
ROBUST_TEST_BIN = $(BUILD)/test_robust
ROBUST_DEPS     = $(ROBUST_LIB_OBJ) $(MORRIS_LIB_OBJ) $(SOBOL_LIB_OBJ) $(COMMON_OBJ)

# core test suites (one binary per test file — each has its own main())
CORE_TEST_BIN = $(BUILD)/test_doe
SEC_TEST_BIN  = $(BUILD)/test_security

.PHONY: all common morris sobol robust taguchi tools test test-taguchi test-all clean

all: common morris sobol robust taguchi

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

# ---- robust orchestrator ------------------------------------------------
robust: $(ROBUST_BIN)

$(ROBUST_BIN): $(ROBUST_CLI_OBJ) $(ROBUST_DEPS) | $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD)/robust/lib/%.o: robust/src/lib/%.c | $(BUILD)/robust/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(ROBUST_INC) -c $< -o $@

$(BUILD)/robust/cli/%.o: robust/src/cli/%.c | $(BUILD)/robust/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(ROBUST_INC) -c $< -o $@

# ---- taguchi (vendored peer tool, builds with its own Makefile) ---------
# It builds to taguchi/build/taguchi via its own Makefile, so copy it into
# $(BIN) alongside morris/sobol/robust. Without this, taguchi is the only tool
# NOT in build/bin/, and downstream consumers that hardcode a path break when the
# layout moves — which is exactly what happened to the gluesticks experiments
# after the umbrella restructure.
taguchi:
	$(MAKE) -C taguchi
	@mkdir -p $(BIN)
	@cp -f taguchi/build/taguchi $(BIN)/taguchi
	@echo "  taguchi -> $(BIN)/taguchi"

# ---- tests --------------------------------------------------------------
test: $(CORE_TEST_BIN) $(SEC_TEST_BIN) $(MORRIS_TEST_BIN) $(SOBOL_TEST_BIN) $(ROBUST_TEST_BIN)
	./$(CORE_TEST_BIN)
	./$(SEC_TEST_BIN)
	./$(MORRIS_TEST_BIN)
	./$(SOBOL_TEST_BIN)
	./$(ROBUST_TEST_BIN)
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "Running valgrind..."; \
		valgrind --leak-check=full --error-exitcode=1 ./$(CORE_TEST_BIN)   >/dev/null 2>&1 && echo "  test_doe: clean"; \
		valgrind --leak-check=full --error-exitcode=1 ./$(SEC_TEST_BIN)    >/dev/null 2>&1 && echo "  test_security: clean"; \
		valgrind --leak-check=full --error-exitcode=1 ./$(MORRIS_TEST_BIN) >/dev/null 2>&1 && echo "  test_morris: clean"; \
		valgrind --leak-check=full --error-exitcode=1 ./$(SOBOL_TEST_BIN)  >/dev/null 2>&1 && echo "  test_sobol: clean"; \
		valgrind --leak-check=full --error-exitcode=1 ./$(ROBUST_TEST_BIN) >/dev/null 2>&1 && echo "  test_robust: clean"; \
	else \
		echo "valgrind not found, skipping memory check."; \
	fi

$(CORE_TEST_BIN): $(COMMON_DIR)/tests/test_doe.c $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) -I$(COMMON_DIR)/tests $(COMMON_DIR)/tests/test_doe.c $(COMMON_OBJ) -o $@ $(LDFLAGS)

$(SEC_TEST_BIN): $(COMMON_DIR)/tests/test_security.c $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) -I$(COMMON_DIR)/tests $(COMMON_DIR)/tests/test_security.c $(COMMON_OBJ) -o $@ $(LDFLAGS)

$(MORRIS_TEST_BIN): $(MORRIS_TEST_SRC) $(MORRIS_LIB_OBJ) $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) $(MORRIS_INC) -I$(COMMON_DIR)/tests $(MORRIS_TEST_SRC) $(MORRIS_LIB_OBJ) $(COMMON_OBJ) -o $@ $(LDFLAGS)

$(SOBOL_TEST_BIN): $(SOBOL_TEST_SRC) $(SOBOL_LIB_OBJ) $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) $(SOBOL_INC) -I$(COMMON_DIR)/tests $(SOBOL_TEST_SRC) $(SOBOL_LIB_OBJ) $(COMMON_OBJ) -o $@ $(LDFLAGS)

$(ROBUST_TEST_BIN): $(ROBUST_TEST_SRC) $(ROBUST_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) $(ROBUST_INC) -I$(COMMON_DIR)/tests $(ROBUST_TEST_SRC) $(ROBUST_DEPS) -o $@ $(LDFLAGS)

# ---- aggregate targets --------------------------------------------------
tools:
	@echo "Built: morris, sobol, robust, taguchi. Pending: ofat, grid, report (see DESIGN.md)."

test-taguchi:
	$(MAKE) -C taguchi test

test-all: test test-taguchi

# ---- housekeeping -------------------------------------------------------
$(BUILD) $(BIN) $(BUILD)/common $(BUILD)/morris/lib $(BUILD)/morris/cli $(BUILD)/sobol/lib $(BUILD)/sobol/cli $(BUILD)/robust/lib $(BUILD)/robust/cli:
	mkdir -p $@

clean:
	rm -rf $(BUILD)
