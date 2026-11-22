### Fixed

- **The runner-label test accepts the labels actionlint accepts** (#615).
  `scripts/test_runner_labels.py` now skips a label with a `${{ }}`
  expression anywhere in it (`runner-${{ matrix.n }}`), where it used to skip
  only one that started with `${{`, and matches the built-in labels in any
  case (`Linux`, `X64`, `Self-Hosted`), so a workflow actionlint passes no
  longer turns the test red. The allowlist is still matched case-sensitively.
