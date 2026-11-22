### Fixed

- **The runner-label test reads a value on the line after an empty key**
  (#615). `scripts/test_runner_labels.py` now reads a scalar, flow list or
  flow mapping written on the next, deeper-indented line after an empty
  `runs-on:` or `labels:` key (`runs-on:` then `  qwne`), as YAML and GitHub
  read it, so a misspelled label written there no longer passes.
