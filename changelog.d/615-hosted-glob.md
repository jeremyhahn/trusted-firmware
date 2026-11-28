### Fixed

- **The runner-label test accepts GitHub-hosted labels and allowlist globs**
  (#615). `scripts/test_runner_labels.py` now builds in the 29 GitHub-hosted
  runner labels actionlint v1.7.12 knows (`ubuntu-24.04`, `macos-14`,
  `windows-latest`), matched in any case, and matches each allowlist entry in
  `.github/actionlint.yaml` as a case-sensitive glob pattern (`gpu-*` accepts
  `gpu-a1`), so a workflow actionlint passes no longer turns the test red.
