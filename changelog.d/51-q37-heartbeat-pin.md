### Fixed

- **The example application drives pin 13 only on a board that has one, else
  pin 0** (#51), so it runs on esp32-qemu and mps2-an505 instead of failing
  init; a board that cannot take the pin fails the build.
