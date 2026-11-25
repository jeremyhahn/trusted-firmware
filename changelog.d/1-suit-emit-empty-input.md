### Fixed

- **`suit-emit` names an empty input file and reports one fault once** (#1).
  An empty image or manifest now prints "is empty" instead of nothing or
  "encoding overflowed", and a missing or oversized manifest is no longer
  followed by a false "encoding overflowed".
