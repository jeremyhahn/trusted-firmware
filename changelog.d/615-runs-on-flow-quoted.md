### Fixed

- **The runner-label test reads a quoted `runs-on` key and a flow value across
  lines** (#615). `scripts/test_runner_labels.py` now reads `'runs-on':` and
  `"runs-on":` keys, and a flow list or mapping that spans lines
  (`runs-on: [self-hosted,` then `qwne]`), where it used to skip the quoted key
  and take the first line of a multi-line flow value as one label, so a
  misspelled label in either form passed.
