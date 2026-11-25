### Fixed

- **The runner-label test does not read a line inside a multi-line quoted
  scalar as a key** (#615).
  `scripts/test_runner_labels.py` used to report the middle line of a step
  such as `run: "echo` then `runs-on: qwne` then `done"` as an unknown
  runner label, so a valid workflow failed `make scripts-test`. It now skips
  the continuation lines of every multi-line single- or double-quoted
  scalar, up to the line its quote closes on, as it skips block scalar
  content; a quote later in a plain scalar (`run: echo "a`) opens none.
