# TODO

Items that are known limitations but not currently blocking use.

## Python Bindings

- [ ] **`suggest_array` duplicates C logic** (`bindings/python/taguchi/core.py`)
  The Python reimplementation of array auto-selection may diverge if the C
  library's algorithm changes. `taguchi suggest-array <file.tgu>` now exists
  as a CLI subcommand. The Python binding should be updated to call it instead
  of its own reimplementation.

- [ ] **`from_tgu` is a second parser** (`bindings/python/taguchi/experiment.py`)
  The `.tgu` format is also parsed by `src/lib/parser.c`. If the format gains
  new features, both parsers need updating. A `taguchi dump` or similar command
  that serialises a parsed experiment back to JSON would remove the need for
  the Python re-parser entirely.

- [x] **No `make test-python` target** — Added `make test-python` and `make test-all` targets.
  `make test-python` runs pytest in `bindings/python/tests/`; `make test-all` runs both suites.

- [ ] **Node.js examples have no tests**
  `examples/nodejs/` is reference-quality only. Add a test suite if the Node.js
  bindings are intended for production use.

## Infrastructure

- [ ] **No CI/CD pipeline**
  No automated test runs on push. A GitHub Actions workflow running
  `make test` and `pytest` on each push/PR would catch regressions early.
  Both test suites are fast enough for CI (<30 s total).
