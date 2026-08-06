# Robust toolkit — top-level build.
# Builds the shared core/ (libdoe) and the stage tool binaries. Stages follow
# the funnel: screen -> attribute -> resolve -> optimize, with analyze/ consuming
# results and orchestrate/ driving the whole thing. See README.md.
CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -std=c99 -pedantic -O2 -g -fPIC
LDFLAGS = -lm

BUILD       = build
BIN         = $(BUILD)/bin
COMMON_DIR  = core
COMMON_INC  = -I$(COMMON_DIR)/include
COMMON_SRC  = $(wildcard $(COMMON_DIR)/src/*.c)
COMMON_OBJ  = $(COMMON_SRC:$(COMMON_DIR)/src/%.c=$(BUILD)/common/%.o)
COMMON_LIB  = $(BUILD)/libdoe.a

# morris tool
MORRIS_INC      = -Iscreen/morris/include
MORRIS_LIB_SRC  = $(wildcard screen/morris/src/lib/*.c)
MORRIS_LIB_OBJ  = $(MORRIS_LIB_SRC:screen/morris/src/lib/%.c=$(BUILD)/morris/lib/%.o)
MORRIS_CLI_SRC  = $(wildcard screen/morris/src/cli/*.c)
MORRIS_CLI_OBJ  = $(MORRIS_CLI_SRC:screen/morris/src/cli/%.c=$(BUILD)/morris/cli/%.o)
MORRIS_BIN      = $(BIN)/morris
MORRIS_TEST_SRC = $(wildcard screen/morris/tests/*.c)
MORRIS_TEST_BIN = $(BUILD)/test_morris

# sobol tool
SOBOL_INC       = -Iattribute/sobol/include
SOBOL_LIB_SRC   = $(wildcard attribute/sobol/src/lib/*.c)
SOBOL_LIB_OBJ   = $(SOBOL_LIB_SRC:attribute/sobol/src/lib/%.c=$(BUILD)/sobol/lib/%.o)
SOBOL_CLI_SRC   = $(wildcard attribute/sobol/src/cli/*.c)
SOBOL_CLI_OBJ   = $(SOBOL_CLI_SRC:attribute/sobol/src/cli/%.c=$(BUILD)/sobol/cli/%.o)
SOBOL_BIN       = $(BIN)/sobol
SOBOL_TEST_SRC  = $(wildcard attribute/sobol/tests/*.c)
SOBOL_TEST_BIN  = $(BUILD)/test_sobol

# robust orchestrator (links the morris + sobol libs to drive the funnel in-process)
ROBUST_INC      = -Iorchestrate/robust/include $(MORRIS_INC) $(SOBOL_INC)
ROBUST_LIB_SRC  = $(wildcard orchestrate/robust/src/lib/*.c)
ROBUST_LIB_OBJ  = $(ROBUST_LIB_SRC:orchestrate/robust/src/lib/%.c=$(BUILD)/robust/lib/%.o)
ROBUST_CLI_SRC  = $(wildcard orchestrate/robust/src/cli/*.c)
ROBUST_CLI_OBJ  = $(ROBUST_CLI_SRC:orchestrate/robust/src/cli/%.c=$(BUILD)/robust/cli/%.o)
ROBUST_BIN      = $(BIN)/robust
ROBUST_TEST_SRC = $(wildcard orchestrate/robust/tests/*.c)
ROBUST_TEST_BIN = $(BUILD)/test_robust
ROBUST_DEPS     = $(ROBUST_LIB_OBJ) $(MORRIS_LIB_OBJ) $(SOBOL_LIB_OBJ) $(COMMON_OBJ)

# pareto tool (E1) — frontier filter + .front store
PARETO_INC      = -Ianalyze/pareto/include
PARETO_LIB_SRC  = $(wildcard analyze/pareto/src/lib/*.c)
PARETO_LIB_OBJ  = $(PARETO_LIB_SRC:analyze/pareto/src/lib/%.c=$(BUILD)/pareto/lib/%.o)
PARETO_CLI_SRC  = $(wildcard analyze/pareto/src/cli/*.c)
PARETO_CLI_OBJ  = $(PARETO_CLI_SRC:analyze/pareto/src/cli/%.c=$(BUILD)/pareto/cli/%.o)
PARETO_BIN      = $(BIN)/pareto
# exclude the fuzz harness: it has its own main()
PARETO_TEST_SRC = $(filter-out %fuzz_pareto.c,$(wildcard analyze/pareto/tests/*.c))
PARETO_FUZZ_SRC = analyze/pareto/tests/fuzz_pareto.c
PARETO_TEST_BIN = $(BUILD)/test_pareto

# core test suites (one binary per test file — each has its own main())
CORE_TEST_BIN = $(BUILD)/test_doe
SEC_TEST_BIN  = $(BUILD)/test_security

# validation — reproduces published results against closed-form ground truth.
# Separate from `test`: the unit suites pin our code, this pins the *claims*
# the roadmap rests on. See validation/README.md.
VALIDATION_SRC = $(wildcard validation/*.c)
VALIDATION_BIN = $(BUILD)/validate

.PHONY: all common morris sobol robust taguchi tools pareto test run-tests test-asan fuzz test-taguchi test-all validate clean

all: common morris sobol robust pareto taguchi

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

$(BUILD)/morris/lib/%.o: screen/morris/src/lib/%.c | $(BUILD)/morris/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(MORRIS_INC) -c $< -o $@

$(BUILD)/morris/cli/%.o: screen/morris/src/cli/%.c | $(BUILD)/morris/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(MORRIS_INC) -c $< -o $@

# ---- sobol --------------------------------------------------------------
sobol: $(SOBOL_BIN)

$(SOBOL_BIN): $(SOBOL_CLI_OBJ) $(SOBOL_LIB_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD)/sobol/lib/%.o: attribute/sobol/src/lib/%.c | $(BUILD)/sobol/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(SOBOL_INC) -c $< -o $@

$(BUILD)/sobol/cli/%.o: attribute/sobol/src/cli/%.c | $(BUILD)/sobol/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(SOBOL_INC) -c $< -o $@

# ---- robust orchestrator ------------------------------------------------
robust: $(ROBUST_BIN)

$(ROBUST_BIN): $(ROBUST_CLI_OBJ) $(ROBUST_DEPS) | $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD)/robust/lib/%.o: orchestrate/robust/src/lib/%.c | $(BUILD)/robust/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(ROBUST_INC) -c $< -o $@

$(BUILD)/robust/cli/%.o: orchestrate/robust/src/cli/%.c | $(BUILD)/robust/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(ROBUST_INC) -c $< -o $@

# ---- taguchi (vendored peer tool, builds with its own Makefile) ---------
# It builds to optimize/taguchi/build/taguchi via its own Makefile, so copy it into
# $(BIN) alongside morris/sobol/robust. Without this, taguchi is the only tool
# NOT in build/bin/, and downstream consumers that hardcode a path break when the
# layout moves — which is exactly what happened to the gluesticks experiments
# after the umbrella restructure.
taguchi:
	$(MAKE) -C optimize/taguchi
	@mkdir -p $(BIN)
	@cp -f optimize/taguchi/build/taguchi $(BIN)/taguchi
	@echo "  taguchi -> $(BIN)/taguchi"

# ---- tests --------------------------------------------------------------
TEST_BINS = $(CORE_TEST_BIN) $(SEC_TEST_BIN) $(MORRIS_TEST_BIN) $(SOBOL_TEST_BIN) $(ROBUST_TEST_BIN) $(PARETO_TEST_BIN)

# Build + run the suites, nothing else. `test` adds valgrind on top; `test-asan`
# reuses this under sanitizers (valgrind and ASan cannot run together).
# The robust suite's H8 round-trip test invokes the taguchi binary, so `test`
# builds taguchi first; `test-asan` does the same at the top level, with normal
# flags, before recursing (so taguchi's own sub-make never sees sanitizer flags).
run-tests: $(TEST_BINS)
	./$(CORE_TEST_BIN)
	./$(SEC_TEST_BIN)
	./$(MORRIS_TEST_BIN)
	./$(SOBOL_TEST_BIN)
	./$(ROBUST_TEST_BIN)
	./$(PARETO_TEST_BIN)

# The valgrind stage used to be a sequence of `valgrind ... && echo clean;`
# lines. Because each ended in `;`, only the LAST suite's exit status reached
# make, and every line sent its output to /dev/null -- so a leak in any of the
# first five suites produced no failure and no diagnostic, just a missing
# "clean" line. CI ran this and reported success. Do not go back to that shape.
#
# This loop propagates failure, prints the report for whichever suite failed,
# and iterates $(TEST_BINS), so a newly added suite is covered without editing
# a second list.
test: taguchi run-tests
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "Running valgrind..."; \
		fail=0; \
		for t in $(TEST_BINS); do \
			if valgrind --leak-check=full --error-exitcode=1 "./$$t" \
			     >$(BUILD)/valgrind.log 2>&1; then \
				echo "  $$(basename $$t): clean"; \
			else \
				echo "  $$(basename $$t): FAILED"; \
				sed 's/^/    | /' $(BUILD)/valgrind.log; \
				fail=1; \
			fi; \
		done; \
		rm -f $(BUILD)/valgrind.log; \
		if [ $$fail -ne 0 ]; then echo "valgrind reported errors"; exit 1; fi; \
	else \
		echo "valgrind NOT FOUND — the memory check did not run."; \
		echo "This is a SKIP, not a pass. Install valgrind, or use 'make test-asan'."; \
	fi

# ---- pareto -------------------------------------------------------------
pareto: $(PARETO_BIN)

$(PARETO_BIN): $(PARETO_CLI_OBJ) $(PARETO_LIB_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD)/pareto/lib/%.o: analyze/pareto/src/lib/%.c | $(BUILD)/pareto/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(PARETO_INC) -c $< -o $@

$(BUILD)/pareto/cli/%.o: analyze/pareto/src/cli/%.c | $(BUILD)/pareto/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(PARETO_INC) -c $< -o $@

$(PARETO_TEST_BIN): $(PARETO_TEST_SRC) $(PARETO_LIB_OBJ) $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) $(PARETO_INC) -I$(COMMON_DIR)/tests \
	      $(PARETO_TEST_SRC) $(PARETO_LIB_OBJ) $(COMMON_OBJ) -o $@ $(LDFLAGS)

# ---- validation ---------------------------------------------------------
# `make validate` reproduces the published screening results the roadmap
# cites, against closed-form ground truth. Not part of `test`: it is slower
# (a 2M-sample Monte Carlo cross-check) and it validates claims rather than
# code. Run it when a source-derived claim changes.
validate: $(VALIDATION_BIN)
	./$(VALIDATION_BIN)

$(VALIDATION_BIN): $(VALIDATION_SRC) $(MORRIS_LIB_OBJ) $(SOBOL_LIB_OBJ) $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) $(MORRIS_INC) $(SOBOL_INC) -Ivalidation \
	      $(VALIDATION_SRC) $(MORRIS_LIB_OBJ) $(SOBOL_LIB_OBJ) $(COMMON_OBJ) -o $@ $(LDFLAGS)

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

# ---- sanitizers + fuzz (see SECURITY.md) ----------------------------------
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer
FUZZ_BIN        = $(BUILD)/fuzz_parsers
PARETO_FUZZ_BIN = $(BUILD)/fuzz_pareto

# Re-run every suite under ASan/UBSan in its own object tree (build/asan).
test-asan: taguchi
	$(MAKE) BUILD=build/asan CFLAGS="$(CFLAGS) $(SANFLAGS)" run-tests

# Deterministic random-input fuzz of every hand-rolled parser that reads
# untrusted input, under ASan/UBSan. Both are seedable and reproducible:
#   ./build/fuzz_parsers <seed> <iters>
#   ./build/fuzz_pareto  <seed> <iters>
# Add a target here whenever a new parser lands (SECURITY.md).
fuzz: | $(BUILD)
	$(CC) $(CFLAGS) $(SANFLAGS) $(COMMON_INC) $(COMMON_DIR)/tests/fuzz_parsers.c $(COMMON_SRC) -o $(FUZZ_BIN) $(LDFLAGS)
	./$(FUZZ_BIN)
	$(CC) $(CFLAGS) $(SANFLAGS) $(COMMON_INC) $(PARETO_INC) $(PARETO_FUZZ_SRC) $(PARETO_LIB_SRC) $(COMMON_SRC) -o $(PARETO_FUZZ_BIN) $(LDFLAGS)
	./$(PARETO_FUZZ_BIN)

# ---- aggregate targets --------------------------------------------------
tools:
	@echo "Built: morris, sobol, robust, pareto, taguchi. Pending: ofat, grid, report, regress, uq (see DESIGN.md/EXPANSION.md)."

test-taguchi:
	$(MAKE) -C optimize/taguchi test

test-all: test test-taguchi

# ---- housekeeping -------------------------------------------------------
$(BUILD) $(BIN) $(BUILD)/common $(BUILD)/morris/lib $(BUILD)/morris/cli $(BUILD)/sobol/lib $(BUILD)/sobol/cli $(BUILD)/robust/lib $(BUILD)/robust/cli $(BUILD)/pareto/lib $(BUILD)/pareto/cli:
	mkdir -p $@

clean:
	rm -rf $(BUILD)
