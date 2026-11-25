### Fixed

- **A quote in a trailing comment no longer hides the next job's runner
  label** (#615).
  `scripts/test_runner_labels.py` used to read the quote inside a comment
  such as `name: "CI" # "TODO` as the start of a multi-line quoted scalar,
  so every later line was skipped and an unknown label such as
  `runs-on: qwne` passed `make scripts-test`. It now tracks the quote on the
  value with its comment removed and stops at the line's closing quote, so a
  comment after a quoted scalar, on one line or on the closing line of a
  multi-line one, opens nothing.
