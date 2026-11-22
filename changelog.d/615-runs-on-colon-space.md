### Fixed

- **The runner-label test reads a key with spaces before its colon**
  (#615). `scripts/test_runner_labels.py` now reads a `runs-on`, `group` or
  `labels` key written with spaces before its colon (`runs-on : qwne`,
  `labels : qwne`), as YAML and GitHub read it, so a misspelled label written
  that way no longer passes.
