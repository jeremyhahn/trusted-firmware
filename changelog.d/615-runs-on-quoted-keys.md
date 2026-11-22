### Fixed

- **The runner-label test reads quoted keys inside a `runs-on` mapping**
  (#615). `scripts/test_runner_labels.py` now reads quoted `group:` and
  `labels:` keys (`'labels':`, `"group":`) in a block or flow `runs-on`
  mapping, where it used to stop at the quoted key or miss the flow entry, so
  a misspelled label behind a quoted key passed.
