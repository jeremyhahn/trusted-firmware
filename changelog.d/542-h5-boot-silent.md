### Fixed

- **tfw-boot on Cortex-M names its refusal** (#542). A Cortex-M row built
  without OTA in the boot section (the soft row, the STM32H573I-DK's included)
  stopped in `recovery_halt.c` without a word, so a board booted with no
  platform block showed nothing on its console and looked dead. The halt now
  sends the reason as the EVENT frame the OTA recovery already sends: once on
  entry, and again for every whole frame that arrives, so a host that opens the
  console after the reset still learns it. It still answers no command and
  boots nothing. An unprovisioned board reports `TFW_ERR_NOT_PROVISIONED`.
  `make integration-test-cortex-m` gains an `unprovisioned` row, and
  wirecheck a `-refusal <status>` flag for `-silent`. The AVR halt is unchanged:
  the 328P's 4 KB section has no room for the framer.
