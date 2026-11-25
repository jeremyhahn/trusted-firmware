### Fixed

- **A multi-line quoted scalar inside a flow mapping or list is no longer
  read as keys** (#615).
  `scripts/test_runner_labels.py` skipped the continuation lines of a quoted
  scalar only when the whole value started with a quote, so in
  `env: {NOTE: "first` / `runs-on: qwne` / `last"}` the text inside `NOTE`
  was read as a `runs-on:` key and `make scripts-test` failed on valid
  runner labels. It now walks a flow value to where it closes and skips each
  of its lines that starts inside a quoted scalar.
