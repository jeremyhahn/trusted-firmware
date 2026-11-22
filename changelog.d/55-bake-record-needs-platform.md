### Fixed

- **`tfwprov`'s bake refuses a record without the platform block** (#55).
  `runBake` given a record path on a network-block-only bake now returns the
  `--record needs the serial` error and writes neither image nor record, where
  it dereferenced a nil platform block; only `main.go`'s own check kept the
  CLI from reaching it.
