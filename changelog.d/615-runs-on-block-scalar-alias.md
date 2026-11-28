### Fixed

- **The runner-label test no longer reads `runs-on:` text inside a block
  scalar, and reads an alias at its anchor or fails closed** (#615).
  `scripts/test_runner_labels.py` skips the content lines of a `|` or `>`
  block scalar, so a `runs-on: qwne` line in a `run: |` script is no longer
  reported. An alias (`runs-on: *runner`) now yields the labels of its
  anchor, anywhere earlier in the file (`env: {RUNNER: &runner codex}`), and
  an alias it cannot resolve fails the test as `<unresolved alias *runner>`
  instead of passing unchecked.
