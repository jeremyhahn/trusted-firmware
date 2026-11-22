### Fixed

- **The runner-label test reads every `runs-on:` form GitHub accepts** (#615).
  `scripts/test_runner_labels.py` now reads the labels of a `runs-on:` mapping,
  in block form (`group:` and `labels:` keys, `labels:` a scalar, flow list or
  block list) and flow form (`{group: g, labels: codex}`), where it used to pass
  a misspelled label in the block form and report a valid flow mapping as one
  unknown label; `group:` is not a label. It reads `.github/workflows/*.yaml`
  as well as `*.yml`, as actionlint does.
