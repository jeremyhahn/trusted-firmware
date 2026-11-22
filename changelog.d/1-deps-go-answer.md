### Fixed

- **`deps-check toolchain` no longer says "ok go" when go.dev's answer holds
  no Go release** (#1). An answer with no version, or an empty one, is now a
  failed lookup and the later checks still run, and a version check with no
  number on either side fails instead of passing over nothing.
