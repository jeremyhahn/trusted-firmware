### Changed

- **The `esp32-p4` row's audit** (#485). `docs/multi-arch/esp32-p4-engineering.md`'s
  Row status is re-checked against main, with where each named test and
  `make` target resolves, and a numbered "Remaining slices" list says what is
  left: a bench run of the rest of the image with no eFuse burned, then the
  owner's HIL-1 to HIL-4 burns, each with the lines it turns met. None of it
  runs on the host.
