### Fixed

- **ESP32 and Ethernet transport docs match main** (#640).
  `docs/esp32/engineering.md`'s Row status points its seven make, CI and
  `mk/host.mk` line references at the targets and jobs they name, and lists the
  five wired ESP32 HAL tests it left out; `docs/ethernet-transport/architecture.md`
  says `Accept` returns a `*tfw.TCPDevice` and that a network block lost at
  renewal drops the link, retried at once; `docs/ethernet-transport/test-matrix.md`
  quotes the Mega's six `BOARD_ROWS`.
