### Added

- **The nRF54L15-DK's reset cause from `RESETREAS`** (#17). The Cortex-M reset
  handler now hands its reset marker to the SoC (`tfw_cm_soc_reset_cause`).
  `mps2-tz` decodes the marker alone, as before. `nrf54l` reads
  `RESET.RESETREAS`, decodes it and clears it by writing all ones: the reset
  pin is external, the two watchdog bits alone are watchdog, `SREQ` with the
  firmware's marker is software, nothing latched is power-on, and every other
  value is unknown. The new object is `src/hal/cortex-m/nrf54l/soc_reset.c`,
  in both tfw-boot and the application. A host test checks the decode and the
  clear against a fake word. Nothing has run on the board. No wire command or
  vector changed.
