# Robust toolkit — top-level build.
# Builds the shared core/ (libdoe) and the stage tool binaries. Stages follow
# the funnel: screen -> attribute -> resolve -> optimize, with analyze/ consuming
# results and orchestrate/ driving the whole thing. See README.md.
CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -std=c99 -pedantic -O2 -g -fPIC -MMD -MP
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

# regress tool (E1) — SRC/SRRC + R^2
REGRESS_CLI_SRC = $(wildcard analyze/regress/src/cli/*.c)
REGRESS_CLI_OBJ = $(REGRESS_CLI_SRC:analyze/regress/src/cli/%.c=$(BUILD)/regress/cli/%.o)
REGRESS_BIN     = $(BIN)/regress

# ofat + grid (M6) — the resolve stage: spend a few runs to confirm an effect
OFAT_CLI_SRC = $(wildcard resolve/ofat/src/cli/*.c)
OFAT_CLI_OBJ = $(OFAT_CLI_SRC:resolve/ofat/src/cli/%.c=$(BUILD)/ofat/cli/%.o)
OFAT_BIN     = $(BIN)/ofat
GRID_CLI_SRC = $(wildcard resolve/grid/src/cli/*.c)
GRID_CLI_OBJ = $(GRID_CLI_SRC:resolve/grid/src/cli/%.c=$(BUILD)/grid/cli/%.o)
GRID_BIN     = $(BIN)/grid

# uq tool (E1) — output distribution summary
UQ_CLI_SRC = $(wildcard analyze/uq/src/cli/*.c)
UQ_CLI_OBJ = $(UQ_CLI_SRC:analyze/uq/src/cli/%.c=$(BUILD)/uq/cli/%.o)
UQ_BIN     = $(BIN)/uq

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

# taguchi tool (optimize stage) — the original tool this project grew from.
# Built here like every other peer: no sub-make. Its suites join $(TEST_BINS),
# so they get the same valgrind/ASan discipline as everything else, which they
# did not while it built itself.
TAGUCHI_DIR       = optimize/taguchi
TAGUCHI_INC       = -I$(TAGUCHI_DIR) -I$(TAGUCHI_DIR)/include -I$(TAGUCHI_DIR)/src/lib
TAGUCHI_LIB_SRC   = $(wildcard $(TAGUCHI_DIR)/src/lib/*.c)
TAGUCHI_LIB_OBJ   = $(TAGUCHI_LIB_SRC:$(TAGUCHI_DIR)/src/lib/%.c=$(BUILD)/taguchi/lib/%.o)
TAGUCHI_CLI_SRC   = $(wildcard $(TAGUCHI_DIR)/src/cli/*.c)
TAGUCHI_CLI_OBJ   = $(TAGUCHI_CLI_SRC:$(TAGUCHI_DIR)/src/cli/%.c=$(BUILD)/taguchi/cli/%.o)
TAGUCHI_BIN       = $(BIN)/taguchi
# test_integration.c carries its own main(), so it links as a second binary.
TAGUCHI_TEST_SRC  = $(filter-out %test_integration.c,$(wildcard $(TAGUCHI_DIR)/tests/*.c))
TAGUCHI_TEST_BIN  = $(BUILD)/test_taguchi
TAGUCHI_INTEG_SRC = $(TAGUCHI_DIR)/tests/test_integration.c
TAGUCHI_INTEG_BIN = $(BUILD)/test_taguchi_integration
TAGUCHI_STATIC    = $(BUILD)/libtaguchi.a

# Shared library, for the ctypes bindings. Platform naming carried over from
# the Makefile this replaced.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    TAGUCHI_SONAME = libtaguchi.dylib
    TAGUCHI_SOFLAG = -dynamiclib -install_name @rpath/libtaguchi.dylib
else ifeq ($(OS),Windows_NT)
    TAGUCHI_SONAME = taguchi.dll
    TAGUCHI_SOFLAG = -shared
else
    TAGUCHI_SONAME = libtaguchi.so
    TAGUCHI_SOFLAG = -shared -Wl,-soname,libtaguchi.so
endif
TAGUCHI_SHARED = $(BUILD)/$(TAGUCHI_SONAME)

# core test suites (one binary per test file — each has its own main())
CORE_TEST_BIN   = $(BUILD)/test_doe
RUNNER_TEST_BIN = $(BUILD)/test_runner
SEC_TEST_BIN  = $(BUILD)/test_security

# validation — reproduces published results against closed-form ground truth.
# Separate from `test`: the unit suites pin our code, this pins the *claims*
# the roadmap rests on. See validation/README.md.
VALIDATION_SRC = $(wildcard validation/*.c)
VALIDATION_BIN = $(BUILD)/validate

# Header dependency tracking. -MMD -MP makes every compile emit a .d file
# listing the headers it included; including those here means editing a header
# rebuilds exactly what depends on it.
#
# This was missing until 2026-08-06, and the consequence was not theoretical:
# adding a field to doe_space_t in core/include/doe.h rebuilt some objects but
# not others, so the linked binary had two different ideas of the struct
# layout. sobol read `samples` from the wrong offset and reported 0. Nothing
# warned; only a `make clean` made it go away. Do not remove this.
DEPFILES = $(shell find $(BUILD) -name '*.d' 2>/dev/null)
-include $(DEPFILES)

.PHONY: all common morris sobol robust taguchi pareto regress uq ofat grid install install-cli uninstall coverage test run-tests test-asan fuzz test-taguchi test-all validate clean

all: common morris sobol robust pareto regress uq ofat grid taguchi

# ---- common core --------------------------------------------------------
common: $(COMMON_LIB)

$(COMMON_LIB): $(COMMON_OBJ)
	ar rcs $@ $^

$(BUILD)/common/%.o: $(COMMON_DIR)/src/%.c | $(BUILD)/common
	$(CC) $(CFLAGS) $(COMMON_INC) -c $< -o $@

# ---- morris -------------------------------------------------------------
morris: $(MORRIS_BIN)

$(MORRIS_BIN): $(MORRIS_CLI_OBJ) $(MORRIS_LIB_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/morris/lib/%.o: screen/morris/src/lib/%.c | $(BUILD)/morris/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(MORRIS_INC) -c $< -o $@

$(BUILD)/morris/cli/%.o: screen/morris/src/cli/%.c | $(BUILD)/morris/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(MORRIS_INC) -c $< -o $@

# ---- sobol --------------------------------------------------------------
sobol: $(SOBOL_BIN)

$(SOBOL_BIN): $(SOBOL_CLI_OBJ) $(SOBOL_LIB_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/sobol/lib/%.o: attribute/sobol/src/lib/%.c | $(BUILD)/sobol/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(SOBOL_INC) -c $< -o $@

$(BUILD)/sobol/cli/%.o: attribute/sobol/src/cli/%.c | $(BUILD)/sobol/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(SOBOL_INC) -c $< -o $@

# ---- robust orchestrator ------------------------------------------------
robust: $(ROBUST_BIN)

$(ROBUST_BIN): $(ROBUST_CLI_OBJ) $(ROBUST_DEPS) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/robust/lib/%.o: orchestrate/robust/src/lib/%.c | $(BUILD)/robust/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(ROBUST_INC) -c $< -o $@

$(BUILD)/robust/cli/%.o: orchestrate/robust/src/cli/%.c | $(BUILD)/robust/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(ROBUST_INC) -c $< -o $@

# ---- taguchi -------------------------------------------------------------
# CLI links the static lib, so the binary has no runtime .so dependency.
taguchi: $(TAGUCHI_BIN) $(TAGUCHI_SHARED) $(TAGUCHI_STATIC)

$(TAGUCHI_BIN): $(TAGUCHI_CLI_OBJ) $(TAGUCHI_STATIC) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TAGUCHI_STATIC): $(TAGUCHI_LIB_OBJ)
	ar rcs $@ $^

$(TAGUCHI_SHARED): $(TAGUCHI_LIB_OBJ)
	$(CC) $(CFLAGS) $(TAGUCHI_SOFLAG) $^ -o $@ $(LDFLAGS)

$(BUILD)/taguchi/lib/%.o: $(TAGUCHI_DIR)/src/lib/%.c | $(BUILD)/taguchi/lib
	$(CC) $(CFLAGS) $(TAGUCHI_INC) -c $< -o $@

$(BUILD)/taguchi/cli/%.o: $(TAGUCHI_DIR)/src/cli/%.c | $(BUILD)/taguchi/cli
	$(CC) $(CFLAGS) $(TAGUCHI_INC) -c $< -o $@

# Both suites link the lib objects directly. The integration test used to need
# LD_LIBRARY_PATH against the shared lib; linking the objects removes that.
$(TAGUCHI_TEST_BIN): $(TAGUCHI_TEST_SRC) $(TAGUCHI_LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(TAGUCHI_INC) $(TAGUCHI_TEST_SRC) $(TAGUCHI_LIB_OBJ) -o $@ $(LDFLAGS)

$(TAGUCHI_INTEG_BIN): $(TAGUCHI_INTEG_SRC) $(TAGUCHI_LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(TAGUCHI_INC) $(TAGUCHI_INTEG_SRC) $(TAGUCHI_LIB_OBJ) -o $@ $(LDFLAGS)

# ---- tests --------------------------------------------------------------
TEST_BINS = $(CORE_TEST_BIN) $(RUNNER_TEST_BIN) $(SEC_TEST_BIN) $(MORRIS_TEST_BIN) $(SOBOL_TEST_BIN) \
            $(ROBUST_TEST_BIN) $(PARETO_TEST_BIN) $(TAGUCHI_TEST_BIN) $(TAGUCHI_INTEG_BIN)

# Build + run the suites, nothing else. `test` adds valgrind on top; `test-asan`
# reuses this under sanitizers (valgrind and ASan cannot run together).
# The robust suite's H8 round-trip test invokes the taguchi binary, which the
# same build now produces, so no special ordering is needed.
run-tests: $(TEST_BINS) $(TAGUCHI_BIN) $(PARETO_BIN) \
           $(REGRESS_BIN) $(UQ_BIN) $(OFAT_BIN) $(GRID_BIN) $(MORRIS_BIN)
	./$(CORE_TEST_BIN)
	./$(RUNNER_TEST_BIN)
	./$(SEC_TEST_BIN)
	./$(MORRIS_TEST_BIN)
	./$(SOBOL_TEST_BIN)
	TAGUCHI_BIN=$(TAGUCHI_BIN) ./$(ROBUST_TEST_BIN)
	./$(PARETO_TEST_BIN)
	@PARETO=$(PARETO_BIN) bash analyze/pareto/tests/test_pareto_cli.sh
	./$(TAGUCHI_TEST_BIN)
	./$(TAGUCHI_INTEG_BIN)
	@TAGUCHI=$(TAGUCHI_BIN) bash $(TAGUCHI_DIR)/tests/test_csv_multicolumn.sh
	@TAGUCHI=$(TAGUCHI_BIN) bash $(TAGUCHI_DIR)/tests/test_cli.sh
	@BIN=$(BIN) bash analyze/tests/test_analyze_cli.sh
	@MORRIS=$(MORRIS_BIN) bash screen/morris/tests/test_morris_cli.sh

# The valgrind stage used to be a sequence of `valgrind ... && echo clean;`
# lines. Because each ended in `;`, only the LAST suite's exit status reached
# make, and every line sent its output to /dev/null -- so a leak in any of the
# first five suites produced no failure and no diagnostic, just a missing
# "clean" line. CI ran this and reported success. Do not go back to that shape.
#
# This loop propagates failure, prints the report for whichever suite failed,
# and iterates $(TEST_BINS), so a newly added suite is covered without editing
# a second list.
test: run-tests
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
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/pareto/lib/%.o: analyze/pareto/src/lib/%.c | $(BUILD)/pareto/lib
	$(CC) $(CFLAGS) $(COMMON_INC) $(PARETO_INC) -c $< -o $@

$(BUILD)/pareto/cli/%.o: analyze/pareto/src/cli/%.c | $(BUILD)/pareto/cli
	$(CC) $(CFLAGS) $(COMMON_INC) $(PARETO_INC) -c $< -o $@

$(PARETO_TEST_BIN): $(PARETO_TEST_SRC) $(PARETO_LIB_OBJ) $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) $(PARETO_INC) -I$(COMMON_DIR)/tests \
	      $(PARETO_TEST_SRC) $(PARETO_LIB_OBJ) $(COMMON_OBJ) -o $@ $(LDFLAGS)

# ---- regress ------------------------------------------------------------
regress: $(REGRESS_BIN)

$(REGRESS_BIN): $(REGRESS_CLI_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/regress/cli/%.o: analyze/regress/src/cli/%.c | $(BUILD)/regress/cli
	$(CC) $(CFLAGS) $(COMMON_INC) -c $< -o $@

# ---- uq -----------------------------------------------------------------
uq: $(UQ_BIN)

$(UQ_BIN): $(UQ_CLI_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/uq/cli/%.o: analyze/uq/src/cli/%.c | $(BUILD)/uq/cli
	$(CC) $(CFLAGS) $(COMMON_INC) -c $< -o $@

# ---- ofat + grid --------------------------------------------------------
ofat: $(OFAT_BIN)
grid: $(GRID_BIN)

$(OFAT_BIN): $(OFAT_CLI_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
$(GRID_BIN): $(GRID_CLI_OBJ) $(COMMON_OBJ) | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/ofat/cli/%.o: resolve/ofat/src/cli/%.c | $(BUILD)/ofat/cli
	$(CC) $(CFLAGS) $(COMMON_INC) -c $< -o $@
$(BUILD)/grid/cli/%.o: resolve/grid/src/cli/%.c | $(BUILD)/grid/cli
	$(CC) $(CFLAGS) $(COMMON_INC) -c $< -o $@

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

$(RUNNER_TEST_BIN): $(COMMON_DIR)/tests/test_runner.c $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(COMMON_INC) -I$(COMMON_DIR)/tests $(COMMON_DIR)/tests/test_runner.c $(COMMON_OBJ) -o $@ $(LDFLAGS)

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
test-asan:
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

# ---- coverage -----------------------------------------------------------
# Line coverage over the C suites, in its own object tree so it never disturbs
# a normal build. Reports with gcovr if available, else raw gcov totals.
#   make coverage            summary to stdout
#   make coverage COVHTML=1  also writes build/coverage/index.html (needs gcovr)
COVFLAGS = --coverage -O0 -g -DDOE_COVERAGE

coverage:
	@# Clear the previous run's counters first. .gcda files ACCUMULATE across
	@# runs by design, so without this a coverage report mixes executions of
	@# code that no longer exists -- and after a header edit that moves a
	@# static inline, gcovr fails outright ("function on multiple lines").
	@# Removing .gcda resets counters without forcing a recompile (.gcno,
	@# written at compile time, is what pairs with the objects).
	@find build/cov -name '*.gcda' -delete 2>/dev/null || true
	$(MAKE) BUILD=build/cov CFLAGS="-Wall -Wextra -std=c99 -pedantic $(COVFLAGS)" \
	        LDFLAGS="-lm --coverage" run-tests
	@mkdir -p build/coverage
	@if command -v gcovr >/dev/null 2>&1; then \
		gcovr --root . --exclude '.*/tests/.*' --exclude 'validation/.*' \
		      --print-summary --output build/coverage/summary.txt \
		      $(if $(COVHTML),--html-details build/coverage/index.html,) && \
		echo "  wrote build/coverage/summary.txt"; \
	elif command -v gcov >/dev/null 2>&1; then \
		echo "gcovr not installed — per-file line coverage via gcov:"; \
		echo "  (install gcovr for totals and HTML: sudo apt-get install gcovr)"; \
		cd build/coverage && \
		find ../cov -name '*.gcno' -printf '%p\n' | while read -r g; do \
			gcov -n -o "$$(dirname "$$g")" "$$g" 2>/dev/null; \
		done | awk '/^File /{f=$$2} /^Lines executed/{print "  " $$0 "  " f}' \
		     | grep -vE "tests/|validation/" | sort -u; \
	else \
		echo "Neither gcovr nor gcov found. Install one: sudo apt-get install gcovr"; \
		exit 1; \
	fi

# ---- install ------------------------------------------------------------
# Suite-wide, replacing the per-tool install that lived in taguchi's Makefile.
PREFIX ?= /usr/local

install: all
	install -d $(PREFIX)/bin $(PREFIX)/lib $(PREFIX)/include
	install -m 755 $(BIN)/* $(PREFIX)/bin/
	install -m 755 $(TAGUCHI_SHARED) $(PREFIX)/lib/
	install -m 644 $(TAGUCHI_STATIC)  $(PREFIX)/lib/
	install -m 644 $(TAGUCHI_DIR)/include/taguchi.h $(PREFIX)/include/
	install -m 644 $(COMMON_DIR)/include/doe.h      $(PREFIX)/include/
	@if command -v ldconfig >/dev/null 2>&1; then ldconfig; fi

install-cli: all
	install -d $(PREFIX)/bin
	install -m 755 $(BIN)/* $(PREFIX)/bin/

uninstall:
	rm -f $(PREFIX)/bin/morris $(PREFIX)/bin/sobol $(PREFIX)/bin/robust
	rm -f $(PREFIX)/bin/pareto $(PREFIX)/bin/taguchi
	rm -f $(PREFIX)/lib/libtaguchi.* $(PREFIX)/include/taguchi.h $(PREFIX)/include/doe.h

# ---- aggregate targets --------------------------------------------------
tools:
	@echo "Built: morris, sobol, robust, pareto, taguchi. Pending: ofat, grid, report, regress, uq (see DESIGN.md/EXPANSION.md)."

# Aliases: taguchi's suites are part of `test` now.
test-taguchi: $(TAGUCHI_TEST_BIN) $(TAGUCHI_INTEG_BIN)
	./$(TAGUCHI_TEST_BIN)
	./$(TAGUCHI_INTEG_BIN)
	@TAGUCHI=$(TAGUCHI_BIN) bash $(TAGUCHI_DIR)/tests/test_csv_multicolumn.sh
	@TAGUCHI=$(TAGUCHI_BIN) bash $(TAGUCHI_DIR)/tests/test_cli.sh
	@BIN=$(BIN) bash analyze/tests/test_analyze_cli.sh
	@MORRIS=$(MORRIS_BIN) bash screen/morris/tests/test_morris_cli.sh

test-all: test

# ---- housekeeping -------------------------------------------------------
$(BUILD) $(BIN) $(BUILD)/common $(BUILD)/morris/lib $(BUILD)/morris/cli $(BUILD)/sobol/lib $(BUILD)/sobol/cli $(BUILD)/robust/lib $(BUILD)/robust/cli $(BUILD)/pareto/lib $(BUILD)/pareto/cli $(BUILD)/taguchi/lib $(BUILD)/taguchi/cli $(BUILD)/regress/cli $(BUILD)/uq/cli $(BUILD)/ofat/cli $(BUILD)/grid/cli:
	mkdir -p $@

clean:
	rm -rf $(BUILD)
