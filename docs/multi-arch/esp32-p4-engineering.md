# ESP32-P4 row: engineering status

The `esp32-p4` row of [SPEC.md](../SPEC.md) (#48), requirement by requirement,
against the code at the commit that last changed this table. The design and
its slices are [`esp32-p4-design.md`](esp32-p4-design.md); the integration
boundary is the ADR, [`adr-esp-idf-integration.md`](adr-esp-idf-integration.md).

## Row status

Met: in the code and pinned by a host test, by `make esp32-qemu-test`, or by
both. Partial: met on the host and in QEMU, with the silicon run still to come,
named in the last column. Not yet: not in the code. "Bench" is the bench host's
P4 (design section 4); "owner" is a decision only the owner takes, an eFuse
burn among them.

| Requirement | Status | Implemented at | Pinned by | Left |
|---|---|---|---|---|
| The firmware core is a HAL port on ESP-IDF | partial | `src/hal/esp32/` (`hal_sys.c`, `hal_uart.c`, `hal_eth.c`, `hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_bus.c`, `hal_pulse_rmt.c`, `hal_eeprom.c`, `hal_handoff.c`, `hal_board.c`); `mk/arch/esp32.mk`, `mk/arch/esp32/` | `test_esp_wire`, `test_esp_ota`, `test_esp_pulse_rmt`, `test_esp_crypto_hmac_p256`, `test_esp_board`, `test_esp_hal_eeprom` (`hal_eeprom.c` over NOR partitions of `partitions.csv`'s sizes: a span past the end is `TFW_ERR_RANGE` and touches nothing, a write rewrites only the 4 KB sectors whose bytes change and reads back exactly, no partition is `TFW_ERR_UNSUPPORTED`, a refused read, erase or write is `TFW_ERR_INTERNAL`, a refused read or erase leaves the store as it was); CI's `esp32-build BOARD=esp32p4-poe` compiles the P4 image; `make esp32-qemu-test` runs it on the QEMU board | The P4 image on silicon: bench HIL-0 (slice 7), run 2026-10-21 (`../development/bench/esp32-p4.md`, "Runs") |
| …and a boot-HAL port | partial | `boot/hal/esp32/` (`boot_l0.c`, `boot_hal_esp32.c`, `measure_esp32.c`, `flash_span.c`) | `test_boot_measure_esp32`, `test_boot_measure_esp32_classic`, `test_boot_flash_span`, `test_boot_l0_esp32_sha`; `make esp32-qemu-test` | Bench HIL-0, run 2026-10-21 (`../development/bench/esp32-p4.md`, "Runs") |
| Secure boot v2 (RSA) composed, never re-implemented | partial | `mk/arch/esp32-targets.mk` (key, `espsecure.py` re-sign after the seal); `boot_l0.c` calls `esp_image_verify` | `make esp32-qemu-test`: the ROM verifies the bootloader and the bootloader the app, a flipped image is refused | Burning the key digest on the P4: owner (HIL-2) |
| Flash encryption composed | partial | `boards/esp32p4-poe/sdkconfig.defaults` (release mode) | CI's `esp32-build` builds with it | Enabling it on the P4: owner (HIL-3), and release vs development mode |
| Partition table composed | partial | `boards/esp32p4-poe/partitions.csv` (`tfw_plat`, `tfw_id`, `tfw_eeprom`, `otadata`, `tfw_auth`, `ota_0`, `ota_1`) | CI's `esp32-build` builds the P4's; `make esp32-qemu-test` boots from the QEMU board's, the same layout; `test_board_esp32p4-poe` (`test_the_trailer_bound_is_16_mib_less_the_trailer`, the slot bound) | Booting from it on the P4: bench HIL-0, run 2026-10-21 (`../development/bench/esp32-p4.md`, "Runs") |
| OTA composed on `esp_ota`, the floor from the eFuse secure version | partial | `src/hal/esp32/hal_ota.c`; `boot_l0.c` checks the `OTA_AUTH` authorization at reset | `test_esp_ota`, `test_esp_wire`; `make esp32-qemu-test` steps 7–10 | Each advance of the P4's eFuse floor: owner (HIL-4) |
| Layer 0 verifies trailer v1 and the SUIT manifest with ES256 before the CDI (m453, item 23) | met | `boot/hal/esp32/l0_gate.c`, `verify_p256_rom.c` on the P4, `l0_crypto.c`, `l0_seal.py` | `test_boot_l0_esp32`, `test_boot_l0_esp32_rom`, `test_boot_p256_rom`; `make esp32-qemu-test` steps 5–6 | On silicon: bench HIL-0, run 2026-10-21 (`../development/bench/esp32-p4.md`, "Runs") |
| The CDI is `HMAC(UDS, measurement)` from the eFuse UDS through the HMAC peripheral | partial | `boot_l0.c` (`derive_cdi`), handed off through `hal_handoff.c` | `make esp32-qemu-test` on the esp32c3 (CDI fingerprint against the host's HMAC); `test_esp_crypto_hmac_p256` (the provider takes only a CDI handed off for its tier, and not an all-zero one) | Burning the P4's UDS: owner (HIL-1) |
| The DeviceID is ECDSA P-256, derived from the same UDS, never stored | met | `src/hal/esp32/crypto_hmac_p256.c` over `src/crypto/deviceid_p256.c` | `test_esp_crypto_hmac_p256` (the point and the ES256 signature of UDS `0x00..0x1f` are `test_deviceid_p256`'s literals, the key block the peripheral is asked for is the `HMAC_UP` one found, the DeviceID does not move with the measurement, no UDS or a refusing peripheral is `NOT_PROVISIONED`); `test_deviceid_p256`; `make esp32-qemu-test` (`wirecheck -deviceid`) | On silicon after HIL-1 |
| The DS peripheral is not used; no RSA DeviceID | met | No DS source; `crypto_hmac_p256.c` | `test_board_esp32p4-poe` (`test_attest_is_on_with_the_p256_deviceid`) | — |
| No key leaves the part in the clear | met | `src/hal/esp32/prov_uds.c` burns the UDS read- and write-protected; the scalar is zeroed after each use (`crypto_hmac_p256.c`, `deviceid_p256.c`) | `test_provision_uds`; `test_deviceid_p256` (`test_signing_zeroes_the_scalar_and_the_rfc_state`); `make esp32-qemu-test` step 11 | The burn on the P4: owner (HIL-1) |
| The wire protocol over Ethernet | partial | `src/hal/esp32/hal_eth.c` (EMAC and IP101, DHCP or a static address), `hal_uart.c` (TCP on port 21574) | `test_esp_wire`; `make esp32-qemu-test` (`wirecheck` over the emulated Ethernet) | The IP101 link on the board: bench HIL-0, run 2026-10-21 (`../development/bench/esp32-p4.md`, "Runs") |
| The C6 stays off, its radio never enabled | partial | `src/hal/esp32/hal_board.c` holds the C6 enable low | `test_board_esp32p4-poe` (`test_the_coprocessor_enable_is_gpio_54`); `test_esp_board` (against the P4's profile: driven low before it is an output, pulled down, held, never driven high; a refused step is `TFW_ERR_INTERNAL` and nothing after it runs) | Seen held in reset on the board: bench HIL-0 |
| Per-architecture code only behind the HAL, the boot HAL and the board profiles | met | `src/hal/esp32/`, `boot/hal/esp32/`, `boards/esp32p4-poe/` | `make lint-conditionals` | — |
| Board id `0x80`, the first in the ESP32 range | met | `boards/esp32p4-poe/board_profile.h` | `test_board_esp32p4-poe` (`test_the_board_is_the_first_esp32_id`); `sdk/go/boardid_test.go` | — |
