# ESP32 port — engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-25
> Conflicts with this document are defects. Fix them here first.

The design is in [multi-arch/](../multi-arch/): the integration ADR
([adr-esp-idf-integration.md](../multi-arch/adr-esp-idf-integration.md), #16)
and the first part's design
([esp32-p4-design.md](../multi-arch/esp32-p4-design.md), #48). This page is the
row's requirements and status.

## Requirements

The `esp32` row of [SPEC.md](../SPEC.md) keeps one line and links here; this is
its text.

The ESP-IDF architecture port, for the Xtensa and RISC-V ESP32 parts, the
ESP32-P4 first (row `esp32-p4`, #23, implemented by #48), with an ADR for its
integration boundary and transport (#16). Per-architecture code lives only
behind the HAL, the boot HAL and the board profiles. The AVR gate (#7) is
lifted for the ESP32-P4; the other ESP32 parts start only once AVR has every
primitive and a proven physical end-to-end run (#7). The ADR (#16) is
accepted, implemented by #48.

## Row status

The requirements above, one by one, against
the code at the commit that last changed this table. Shipped: in the code and
pinned by a test. Partial: in the code on some paths, with the rest named. Not
yet: not in the code. The `esp32-p4` row is its own row and is not repeated
here.

| Requirement | Status | Implemented at | Pinned by |
|---|---|---|---|
| ESP-IDF architecture port: the firmware core over a HAL on ESP-IDF | shipped | `src/hal/esp32/`; `mk/arch/esp32.mk`; the image is `make esp32-build` (`mk/arch/esp32-targets.mk:48`), which hands `mk/firmware.mk`'s source list to the ESP-IDF project in `mk/arch/esp32/` | `scripts/test_esp32_port.py:173` (`ImageObjectList`: every listed file exists, every HAL file is in an image, no other HAL or test file is, every `tfw_` function an image calls is defined in it, for each board, part and tier, and the `PROVISION=1` image); `test_esp_ota.c`, `test_esp_wire.c`; `make esp32-qemu-test` (`mk/arch/esp32-targets.mk:111`) in CI's `esp32-qemu-test` job (`.github/workflows/ci.yml:1339`), on a push to main and nightly, never on a PR (ruling 220(j)) |
| The boot HAL: layer 0 inside ESP-IDF's second-stage bootloader | shipped | `boot/hal/esp32/`; its object list in `boot/hal/esp32/bootloader_components/main/CMakeLists.txt`; the verify per board by `ESP_L0_VERIFY` | `scripts/test_esp32_port.py:228` (`LayerZeroObjectList`: every listed file exists, both verifies and every boot-HAL file are listed, every board names a known verify, every called function is defined with either verify); `test/unit/boot/test_boot_l0_esp32.c`, `test_boot_measure_esp32.c`, `test_boot_measure_esp32_flash.c`; `make esp32-qemu-test` |
| Xtensa parts | partial | the `esp32` machine of `boards/esp32-qemu` (`profile.mk:20`), `none` tier: no HMAC peripheral, so no DICE identity | `ImageObjectList`'s `esp32` image; the `esp32` run of `make esp32-qemu-test`: secure boot, the wire, the `ATTEST` refusal. No Xtensa board; the other parts wait on #7 (row below) |
| RISC-V parts | partial: the C3 on QEMU; the P4 is its own row | the `esp32c3` machine of `boards/esp32-qemu`, `hmac-p256` tier; `boards/esp32p4-poe` (row `esp32-p4`) | `ImageObjectList`'s `esp32c3`, `esp32c3` `PROVISION=1` and `esp32p4` images; the `esp32c3` run of `make esp32-qemu-test`: the CDI and P-256 DeviceID across reboots, ES256 `ATTEST`, provisioning; `make esp32-build BOARD=esp32p4-poe` in CI's `esp32-build` job (`.github/workflows/ci.yml:1279`) |
| The ESP32-P4 first | in progress | row `esp32-p4`, #23, implemented by #48 | that row's tests; `test_board_esp32p4-poe.c` |
| An ADR for the integration boundary and transport, accepted | shipped | [adr-esp-idf-integration.md](../multi-arch/adr-esp-idf-integration.md) (#16), `Status: ACCEPTED` | — (a document) |
| Per-architecture code only behind the HAL, the boot HAL and the board profiles | shipped | ESP-IDF headers and names only in `src/hal/esp32/`, `boot/hal/esp32/`, `mk/arch/esp32/` and the ESP32 board directories | `make lint-arch-leak` (`mk/host.mk:1004`): no ESP-IDF include or name in `src/` (bar `src/third_party/`), `boot/`, `include/`, `tools/` or any other board, the file named; `scripts/test_esp32_port.py:264` (`EspIdfStaysBehindTheHal`: the target passes this tree and fails a planted leak of each header prefix and each name, naming the file); `make lint-conditionals` (`mk/host.mk:895`) |
| The HAL's own host tests | shipped | every `src/hal/esp32/*.c` (#180) | per file, wired in `mk/host.mk`: `test_esp_adc.c`, `test_esp_adc_mutate.c`, `test_esp_adc_nounit_mutate.c`, `test_esp_board.c`, `test_esp_board_mutate.c`, `test_esp_board_nocoproc_mutate.c`, `test_esp_bus_mutate.c`, `test_esp_crypto_hmac_p256.c`, `test_esp_hmac_p256_mutate.c`, `test_esp_eeprom_mutate.c`, `test_esp_hal_eeprom.c`, `test_esp_eth_mutate.c`, `test_esp_gpio_mutate.c`, `test_esp_pins.c`, `test_esp_handoff.c`, `test_esp_handoff_mutate.c`, `test_esp_ota.c`, `test_esp_ota_mutate.c`, `test_esp_wire.c`, `test_esp_pulse_mutate.c`, `test_esp_pulse_rmt.c`, `test_esp_pwm_mutate.c`, `test_esp_sys.c`, `test_esp_sys_mutate.c`, `test_esp_tone_mutate.c`, `test_esp_uart_mutate.c`; and `test_esp_prov_uds_mutate.c` over `test/mock/esp32/prov/`: presence by the HMAC_UP block; the draw under ADC unit 1's lock with the entropy source on, busy refused; the burn, confirmation and each refusal; the HMAC key id |
| Boards are data | shipped | `boards/esp32-qemu/`, `boards/esp32p4-poe/`: `board.c`, `board_profile.h`, `profile.mk`; ids in the ESP32 range (0x80–0xBF), `esp32-qemu` 0x81 | `test_board_esp32-qemu.c`, `test_board_esp32p4-poe.c`; `make board-sweep` (`mk/host.mk:1277`) |
| The other ESP32 parts start only after the AVR gate (#7) | not yet, by rule | — | — |

Counts: 6 shipped, 2 partial, 1 in progress, 1 not yet by rule. Test paths are
under `test/unit/` unless named.

What is open, and in what order:

- **The ESP32-P4**, row `esp32-p4` (#48).
- **The other parts** (Xtensa and RISC-V boards beyond QEMU) once AVR has every
  primitive and a proven physical end-to-end run (#7).
