### Added

- **`tfw.ParseImage` reads the trailer spans `testdata/vectors.h` froze**
  (#629). A new Go test parses a stream around each frozen span: the board,
  flags, version, length and measurement it returns match the C side's bytes,
  and the span that names no board is refused with `ErrImageBoard`.
