### Added

- **tfw-boot on the nRF54L15-DK** (#17). `mk/boot.mk` gains the row
  `nrf54l15-dk:soft:soft:p256-soft:0:0`, which `bootloader-check` builds:
  soft hash and P-256 verify in software, no OTA, no store, and the soft
  DeviceID seed handed to the application as on `mps2-an505`.
  `boot/hal/cortex-m/nrf54l/` adds a polled UARTE20 console and the RRAM
  controller's write sequence (nrfx `nrfy_rramc.h`, with bounded waits) for
  the platform block. The Cortex-M core half now calls the SoC for code-memory
  writes (`tfw_cm_boot_code_write`), and `mps2-tz` keeps its copy. Host tests
  check the RRAM span check and the word/byte split. Nothing has run on the
  board yet. No wire command or vector changed.
