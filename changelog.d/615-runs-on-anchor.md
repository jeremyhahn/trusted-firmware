### Fixed

- **The runner-label test drops a YAML anchor or tag before a label and skips
  an alias** (#615). `scripts/test_runner_labels.py` now removes a leading
  anchor (`&r`) or tag (`!!str`) from a `runs-on:` value, a `labels:` value,
  the next-line value of an empty key and each list item before it reads the
  labels, and skips an alias (`*r`), whose labels are read at its anchor, so
  `runs-on: &r codex` no longer fails and a misspelled label after an anchor
  or tag, or in a block list under an anchored key, no longer passes.
