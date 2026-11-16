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
| The firmware core is a HAL port on ESP-IDF | partial | `src/hal/esp32/` (`hal_sys.c`, `hal_uart.c`, `hal_eth.c`, `hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_bus.c`, `hal_pulse_rmt.c`, `hal_eeprom.c`, `hal_handoff.c`, `hal_board.c`); `mk/arch/esp32.mk`, `mk/arch/esp32/` | `test_esp_wire`, `test_esp_ota`, `test_esp_pulse_rmt`, `test_esp_crypto_hmac_p256`, `test_esp_board`, `test_esp_hal_eeprom` (`hal_eeprom.c` over NOR partitions of `partitions.csv`'s sizes: a span past the end is `TFW_ERR_RANGE` and touches nothing, a write rewrites only the 4 KB sectors whose bytes change and reads back exactly, no partition is `TFW_ERR_UNSUPPORTED`, a refused read, erase or write is `TFW_ERR_INTERNAL`, a refused read or erase leaves the store as it was); CI's `esp32-build BOARD=esp32p4-poe` compiles the P4 image; `make esp32-qemu-test` runs it on the QEMU board | The P4 image ran on silicon in bench HIL-0 on 2026-10-21 (`../development/bench/esp32-p4.md`, "Runs": boot, `hal_eth.c` and the wire, `CRYPTO=none`); not yet exercised on the board: the pin peripherals (`hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_bus.c`, `hal_pulse_rmt.c`), `hal_eeprom.c` and a crypto tier |
| …and a boot-HAL port | met | `boot/hal/esp32/` (`boot_l0.c`, `boot_hal_esp32.c`, `measure_esp32.c`, `flash_span.c`) | `test_boot_measure_esp32`, `test_boot_measure_esp32_classic`, `test_boot_flash_span`, `test_boot_l0_esp32_sha`; `make esp32-qemu-test`; bench HIL-0 2026-10-21 step 4 (`../development/bench/esp32-p4.md`, "Runs": `tfw-l0: app at 0x20000 verified, measurement …`, then the app loaded) | — (the CDI on silicon is the CDI row's) |
| Secure boot v2 (RSA) composed, never re-implemented | partial | `mk/arch/esp32-targets.mk` (key, `espsecure.py` re-sign after the seal); `boot_l0.c` calls `esp_image_verify` | `make esp32-qemu-test`: the ROM verifies the bootloader and the bootloader the app, a flipped image is refused | Burning the key digest on the P4: owner (HIL-2) |
| Flash encryption composed | partial | `boards/esp32p4-poe/sdkconfig.defaults` (release mode) | CI's `esp32-build` builds with it | Enabling it on the P4: owner (HIL-3), and release vs development mode |
| Partition table composed | met | `boards/esp32p4-poe/partitions.csv` (`tfw_plat`, `tfw_id`, `tfw_eeprom`, `otadata`, `tfw_auth`, `ota_0`, `ota_1`) | CI's `esp32-build` builds the P4's; `make esp32-qemu-test` boots from the QEMU board's, the same layout; `test_board_esp32p4-poe` (`test_the_trailer_bound_is_16_mib_less_the_trailer`, the slot bound); bench HIL-0 2026-10-21 step 4 (`../development/bench/esp32-p4.md`, "Runs": the P4 booted from the seven-partition table at `0x11000`) | — |
| OTA composed on `esp_ota`, the floor from the eFuse secure version | partial | `src/hal/esp32/hal_ota.c`; `boot_l0.c` checks the `OTA_AUTH` authorization at reset | `test_esp_ota`, `test_esp_wire`; `make esp32-qemu-test` steps 7–10 | Each advance of the P4's eFuse floor: owner (HIL-4) |
| Layer 0 verifies trailer v1 and the SUIT manifest with ES256 before the CDI (m453, item 23) | met | `boot/hal/esp32/l0_gate.c`, `verify_p256_rom.c` on the P4, `l0_crypto.c`, `l0_seal.py` | `test_boot_l0_esp32`, `test_boot_l0_esp32_rom`, `test_boot_p256_rom`; `make esp32-qemu-test` steps 5–6 | On silicon: bench HIL-0, run 2026-10-21 (`../development/bench/esp32-p4.md`, "Runs") |
| The CDI is `HMAC(UDS, measurement)` from the eFuse UDS through the HMAC peripheral | partial | `boot_l0.c` (`derive_cdi`), handed off through `hal_handoff.c` | `make esp32-qemu-test` on the esp32c3 (CDI fingerprint against the host's HMAC); `test_esp_crypto_hmac_p256` (the provider takes only a CDI handed off for its tier, and not an all-zero one) | Burning the P4's UDS: owner (HIL-1) |
| The DeviceID is ECDSA P-256, derived from the same UDS, never stored | met | `src/hal/esp32/crypto_hmac_p256.c` over `src/crypto/deviceid_p256.c` | `test_esp_crypto_hmac_p256` (the point and the ES256 signature of UDS `0x00..0x1f` are `test_deviceid_p256`'s literals, the key block the peripheral is asked for is the `HMAC_UP` one found, the DeviceID does not move with the measurement, no UDS or a refusing peripheral is `NOT_PROVISIONED`); `test_deviceid_p256`; `make esp32-qemu-test` (`wirecheck -deviceid`) | On silicon after HIL-1 |
| The DS peripheral is not used; no RSA DeviceID | met | No DS source; `crypto_hmac_p256.c` | `test_board_esp32p4-poe` (`test_attest_is_on_with_the_p256_deviceid`) | — |
| No key leaves the part in the clear | met | `src/hal/esp32/prov_uds.c` burns the UDS read- and write-protected; the scalar is zeroed after each use (`crypto_hmac_p256.c`, `deviceid_p256.c`) | `test_provision_uds`; `test_deviceid_p256` (`test_signing_zeroes_the_scalar_and_the_rfc_state`); `make esp32-qemu-test` step 11 | The burn on the P4: owner (HIL-1) |
| The wire protocol over Ethernet | met | `src/hal/esp32/hal_eth.c` (EMAC and IP101, DHCP or a static address), `hal_uart.c` (TCP on port 21574) | `test_esp_wire`; `make esp32-qemu-test` (`wirecheck` over the emulated Ethernet); bench HIL-0 2026-10-21 step 4 (`../development/bench/esp32-p4.md`, "Runs": IP101 link up, `wirecheck: ok` over tcp/21574) | — |
| The C6 stays off, its radio never enabled | partial | `src/hal/esp32/hal_board.c` holds the C6 enable low | `test_board_esp32p4-poe` (`test_the_coprocessor_enable_is_gpio_54`); `test_esp_board` (against the P4's profile: driven low before it is an output, pulled down, held, never driven high; a refused step is `TFW_ERR_INTERNAL` and nothing after it runs) | Seen held in reset on the board: the 2026-10-21 HIL-0 run does not check the C6 |
| Per-architecture code only behind the HAL, the boot HAL and the board profiles | met | `src/hal/esp32/`, `boot/hal/esp32/`, `boards/esp32p4-poe/` | `make lint-conditionals` | — |
| Board id `0x80`, the first in the ESP32 range | met | `boards/esp32p4-poe/board_profile.h` | `test_board_esp32p4-poe` (`test_the_board_is_the_first_esp32_id`); `sdk/go/boardid_test.go` | — |

Counts: 9 met, 6 partial, none not yet. Every partial line is partial for
the same reason: what it needs runs on the host and in QEMU, and is left only
on silicon, behind a bench run or an owner's eFuse burn.

Where the names resolve. The application tests are `test/unit/<name>.c`; the
boot tests are `test/unit/boot/<name>.c`, and three of them are builds of
another file with other sources: `test_boot_l0_esp32_rom` and
`test_boot_l0_esp32_sha` of `test_boot_l0_esp32.c` (`mk/boot.mk:681`,
`mk/boot.mk:797`), `test_boot_measure_esp32_classic` of
`test_boot_measure_esp32.c` (`mk/boot.mk:791`); all are in `make host-test`'s
list (`mk/host.mk:1215`–`1219`). `make esp32-build` is
`mk/arch/esp32-targets.mk:49`, `make esp32-qemu-test`
`mk/arch/esp32-targets.mk:115`, `make lint-conditionals` `mk/host.mk:1711`.
`derive_cdi` is `boot/hal/esp32/boot_l0.c:105` and its `esp_image_verify` call
`boot_l0.c:215`. The silicon runs are the three of the bench page's "Runs"
(2026-10-18, 2026-10-19, 2026-10-21); only the last booted our image.

Audited against main for #485: every file, test binary, test function and
`make` target the table names exists, and each status matches the code and
the bench record. No line changed status.

### Remaining slices

Nothing in this row is left that runs on the host or in QEMU. What remains is
on the P4, in this order; the row ships when the last of them is recorded on
the bench page and each partial line above turns met:

1. **Bench HIL-0b: the rest of the image on silicon, no eFuse burned** (the
   bench; flashing is ours, m346). The HIL-0 image again
   (`boards/esp32p4-poe/sdkconfig.hil0`, `CRYPTO=none`), and on it: the C6
   seen held in reset (its enable, GPIO 54, read low, and no C6 output on its
   UART); the pin peripherals of `hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`,
   `hal_tone.c`, `hal_bus.c` and `hal_pulse_rmt.c` driven over the wire on the
   pins `boards/esp32p4-poe/board.c` maps; and `hal_eeprom.c`'s
   `EEPROM_READ`/`EEPROM_WRITE` read back across a reset. Files: the steps and
   the run in `docs/development/bench/esp32-p4.md`; this table's first and C6
   lines. Proven by the run's record, then `make board-restore` and the
   factory SHA-256 again. Turns the C6 line met, and leaves the first line
   only its crypto tier.
2. **HIL-1: the UDS burn and the `hmac-p256` tier on silicon** (owner, an
   irreversible burn). `make esp32-build BOARD=esp32p4-poe` with its default
   tier, `prov_uds.c` burning the UDS into an `HMAC_UP` key block,
   read-protected; then the checks `make esp32-qemu-test` makes on the esp32c3
   repeated on the board: the CDI fingerprint against the host's HMAC, the
   DeviceID stable across a reset and a new image, ES256 `ATTEST` verified by
   `wirecheck -deviceid`, and the UDS unreadable. Files: the bench page. Turns
   the CDI and "no key leaves the part" lines met, the DeviceID line's silicon
   note, and the first line's crypto tier, so the first line is met.
3. **HIL-2: secure boot v2 enabled** (owner; ends the factory restore). The
   key digest and `SECURE_BOOT_EN` burned; the board boots only the image
   `mk/arch/esp32-targets.mk` re-signs, and refuses a flipped one, as
   `make esp32-qemu-test` shows on QEMU. Files: the bench page. Turns the
   secure-boot line met.
4. **HIL-3: flash encryption enabled** (owner, who also chooses release or
   development mode). The image built from `boards/esp32p4-poe/sdkconfig.defaults`
   without the HIL-0 overlay boots encrypted; a plaintext image does not.
   Files: the bench page, and `sdkconfig.defaults` only if the owner chooses
   development mode. Turns the flash-encryption line met.
5. **HIL-4: one OTA round with a floor advance** (owner: the release policy,
   design section 2.3). `OTA_AUTH`, `OTA_ACTIVATE` and `OTA_CONFIRM` over
   Ethernet to the board, the trial confirmed and the eFuse `SECURE_VERSION`
   advanced, then an image below it refused, as steps 7–10 of
   `make esp32-qemu-test` do on QEMU. Files: the bench page. Turns the OTA
   line met; with it every line is met and the row's PR marks it shipped in
   `docs/SPEC.md`.

Not in this row: HIL-5 (download mode and JTAG off, design section 4) is the
owner's and no requirement of the row needs it.
