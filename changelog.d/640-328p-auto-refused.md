### Fixed

- **The ATmega328P refuses `CRYPTO=auto` as it refuses `CRYPTO=soft`** (#640).
  `make BOARD=atmega328p CRYPTO=auto` built the soft root beside the element
  on a board owner ruling m522 made element-only; it now fails at make time
  naming the board's one tier, `atecc608a`.
