### Added

- **`make hw-test PORT=tcp:<ip:port>`** (#536): the rows dial a board whose
  wire is a TCP server, as the ESP32 port's is (`src/hal/esp32/hal_uart.c`),
  with no DTR reset (`sdk/go/internal/peer`). The ESP32-P4 PoE has a row list
  and pin facts for the bench `CRYPTO=none` image.

### Changed

- **The ESP32-P4 PoE board page has a feature matrix** (#536): the hw-test
  rows run on the board 2026-11-17, each verified, failing with the issue to
  file, or waiting on the owner for an eFuse write (`docs/boards/esp32p4-poe.md`).
