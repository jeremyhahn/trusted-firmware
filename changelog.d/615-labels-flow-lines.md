### Fixed

- **The runner-label test reads a flow list across lines as a `labels` or
  next-line value** (#615). `scripts/test_runner_labels.py` now joins a flow
  list or mapping that does not close on its line when it is a `labels:` value
  or the value on the line after an empty `runs-on:` or `labels:` key, as it
  already did for the value on the `runs-on:` line, so `[self-hosted,` is no
  longer reported as a label and a misspelled label on the following lines no
  longer passes.
