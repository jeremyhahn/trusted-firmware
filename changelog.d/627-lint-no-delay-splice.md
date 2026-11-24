### Fixed

- **`make lint-no-delay`** (#627) keeps a string, character literal or `//`
  comment open across a backslash-newline, so a delay call after a spliced
  string fails the gate and a `delay()` in a continued comment does not.
