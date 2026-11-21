### Added

- **`make scripts-test` holds every workflow's runner labels to the actionlint
  allowlist** (#615). `scripts/test_runner_labels.py` reads the labels under
  `self-hosted-runner:` in `.github/actionlint.yaml` and every plain-text
  `runs-on:` label in `.github/workflows/*.yml`, and fails naming file, line
  and label for one that is neither listed nor an actionlint built-in, so a
  misspelled self-hosted label no longer reaches main through a green gate
  while `make lint` does not run actionlint. Labels inside `${{ }}` are
  skipped, as actionlint skips them.
