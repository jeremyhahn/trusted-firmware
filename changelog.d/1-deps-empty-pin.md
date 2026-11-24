### Fixed

- **`deps-check`'s refusal of a version check with no number is tested on both
  sides** (#1). An empty pin is now tested as well as an empty upstream answer,
  directly and through `deps-check toolchain`, and each refusal is tested for
  the side it names ("the pin" or "upstream's answer").
