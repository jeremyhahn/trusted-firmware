# ESP32-P4 row: engineering status

The `esp32-p4` row of [SPEC.md](../SPEC.md) (#48), requirement by requirement,
against the code at the commit that last changed this table. The design and
its slices are [`esp32-p4-design.md`](esp32-p4-design.md); the integration
boundary is the ADR, [`adr-esp-idf-integration.md`](adr-esp-idf-integration.md).

## Requirements

One line per requirement of the row's text (`docs/SPEC.md`, row `esp32-p4`),
audited against main on 2026-11-16 (#485). Every file and test named below is
on main; a test is a file under `test/unit/` or `test/unit/boot/`, or a test
binary `mk/boot.mk` builds from one (`test_boot_l0_esp32_rom`,
`test_boot_l0_esp32_sha` from `test_boot_l0_esp32.c`,
`test_boot_measure_esp32_classic` from `test_boot_measure_esp32.c`), or a
`make` target. The slices still open follow the table.

### Row status

Done: in the code and pinned by a host test, by `make esp32-qemu-test`, or by
both. Partial: done on the host and in QEMU, with the silicon run or an owner
decision still to come, named in the last column. Not started: not in the
code; no requirement is in that state. "Bench" is the bench host's P4 (design
section 4); "owner" is a decision only the owner takes, an eFuse burn among
them.

| Requirement | Status | Implemented at | Pinned by | Left |
|---|---|---|---|---|
| The firmware core is a HAL port on ESP-IDF | partial | `src/hal/esp32/` (`hal_sys.c`, `hal_uart.c`, `hal_eth.c`, `hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_bus.c`, `hal_pulse_rmt.c`, `hal_eeprom.c`, `hal_handoff.c`, `hal_board.c`); `mk/arch/esp32.mk`, `mk/arch/esp32/` | `test_esp_wire`, `test_esp_ota`, `test_esp_pulse_rmt`, `test_esp_crypto_hmac_p256`, `test_esp_board`, `test_esp_hal_eeprom` (`hal_eeprom.c` over NOR partitions of `partitions.csv`'s sizes: a span past the end is `TFW_ERR_RANGE` and touches nothing, a write rewrites only the 4 KB sectors whose bytes change and reads back exactly, no partition is `TFW_ERR_UNSUPPORTED`, a refused read, erase or write is `TFW_ERR_INTERNAL`, a refused read or erase leaves the store as it was); CI's `esp32-build BOARD=esp32p4-poe` compiles the P4 image; `make esp32-qemu-test` runs it on the QEMU board | The P4 image ran on silicon in bench HIL-0 on 2026-10-21 (`../development/bench/esp32-p4.md`, "Runs": boot, `hal_eth.c` and the wire, `CRYPTO=none`), again 2026-11-14 with a static address; not yet exercised on the board: the pin peripherals (`hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_bus.c`, `hal_pulse_rmt.c`), `hal_eeprom.c` and a crypto tier |
| …and a boot-HAL port | done | `boot/hal/esp32/` (`boot_l0.c`, `boot_hal_esp32.c`, `measure_esp32.c`, `flash_span.c`) | `test_boot_measure_esp32`, `test_boot_measure_esp32_classic`, `test_boot_flash_span`, `test_boot_l0_esp32_sha`; `make esp32-qemu-test`; bench HIL-0 2026-10-21 step 4 (`../development/bench/esp32-p4.md`, "Runs": `tfw-l0: app at 0x20000 verified, measurement …`, then the app loaded), again on main's image 2026-11-13 ("HIL-0 on main's image") | — (the CDI on silicon is the CDI row's) |
| Secure boot v2 (RSA) composed, never re-implemented | partial | `mk/arch/esp32-targets.mk` (key, `espsecure.py` re-sign after the seal); `boot_l0.c` calls `esp_image_verify` | `make esp32-qemu-test`: the ROM verifies the bootloader and the bootloader the app, a flipped image is refused | Burning the key digest on the P4: owner (HIL-2) |
| Flash encryption composed | partial | `boards/esp32p4-poe/sdkconfig.defaults` (release mode) | CI's `esp32-build` builds with it | Enabling it on the P4: owner (HIL-3), and release vs development mode |
| Partition table composed | done | `boards/esp32p4-poe/partitions.csv` (`tfw_plat`, `tfw_id`, `tfw_eeprom`, `otadata`, `tfw_auth`, `ota_0`, `ota_1`) | CI's `esp32-build` builds the P4's; `make esp32-qemu-test` boots from the QEMU board's, the same layout; `test_board_esp32p4-poe` (`test_the_trailer_bound_is_16_mib_less_the_trailer`, the slot bound); bench HIL-0 2026-10-21 step 4 (`../development/bench/esp32-p4.md`, "Runs": the P4 booted from the seven-partition table at `0x11000`; again 2026-11-13, "HIL-0 on main's image", the table at `0x10000`) | — |
| OTA composed on `esp_ota`, the floor from the eFuse secure version | partial | `src/hal/esp32/hal_ota.c`; `boot_l0.c` checks the `OTA_AUTH` authorization at reset | `test_esp_ota`, `test_esp_wire`; `make esp32-qemu-test` steps 7–10 | Each advance of the P4's eFuse floor: owner (HIL-4) |
| Layer 0 verifies trailer v1 and the SUIT manifest with ES256 before the CDI (m453, item 23) | done | `boot/hal/esp32/l0_gate.c`, `verify_p256_rom.c` on the P4, `l0_crypto.c`, `l0_seal.py` | `test_boot_l0_esp32`, `test_boot_l0_esp32_rom`, `test_boot_p256_rom`; `make esp32-qemu-test` steps 5–6 | On silicon: bench HIL-0, run 2026-10-21 and again on main's image 2026-11-13 (`../development/bench/esp32-p4.md`, "Runs"); and on 2026-11-14 with a static address ("Row 48: the wire over Ethernet on VLAN 101"), layer 0 `verified` before the app ran; with no UDS burned, the CDI half is HIL-1's |
| The CDI is `HMAC(UDS, measurement)` from the eFuse UDS through the HMAC peripheral | partial | `boot_l0.c` (`derive_cdi`), handed off through `hal_handoff.c` | `make esp32-qemu-test` on the esp32c3 (CDI fingerprint against the host's HMAC); `test_esp_crypto_hmac_p256` (the provider takes only a CDI handed off for its tier, and not an all-zero one) | Burning the P4's UDS: owner (HIL-1) |
| The DeviceID is ECDSA P-256, derived from the same UDS, never stored | done | `src/hal/esp32/crypto_hmac_p256.c` over `src/crypto/deviceid_p256.c` | `test_esp_crypto_hmac_p256` (the point and the ES256 signature of UDS `0x00..0x1f` are `test_deviceid_p256`'s literals, the key block the peripheral is asked for is the `HMAC_UP` one found, the DeviceID does not move with the measurement, no UDS or a refusing peripheral is `NOT_PROVISIONED`); `test_deviceid_p256`; `make esp32-qemu-test` (`wirecheck -deviceid`) | On silicon after HIL-1 |
| The DS peripheral is not used; no RSA DeviceID | done | No DS source; `crypto_hmac_p256.c` | `test_board_esp32p4-poe` (`test_attest_is_on_with_the_p256_deviceid`) | — |
| No key leaves the part in the clear | done | `src/hal/esp32/prov_uds.c` burns the UDS read- and write-protected; the scalar is zeroed after each use (`crypto_hmac_p256.c`, `deviceid_p256.c`) | `test_provision_uds`; `test_deviceid_p256` (`test_signing_zeroes_the_scalar_and_the_rfc_state`); `make esp32-qemu-test` step 11 | The burn on the P4: owner (HIL-1) |
| The wire protocol over Ethernet | done | `src/hal/esp32/hal_eth.c` (EMAC and IP101, DHCP or a static address), `hal_uart.c` (TCP on port 21574) | `test_esp_wire`; `make esp32-qemu-test` (`wirecheck` over the emulated Ethernet); bench HIL-0 2026-10-21 step 4 (`../development/bench/esp32-p4.md`, "Runs": IP101 link up, `wirecheck: ok` over tcp/21574); again 2026-11-14 on the development VLAN at a static address ("Row 48: the wire over Ethernet on VLAN 101": PING, CAPS, SYS_INFO, ATTEST refused on the none tier) | — (ES256 `ATTEST` over the wire on the P4 waits on HIL-1) |
| The C6 stays off, its radio never enabled | partial | `src/hal/esp32/hal_board.c` holds the C6 enable low | `test_board_esp32p4-poe` (`test_the_coprocessor_enable_is_gpio_54`); `test_esp_board` (against the P4's profile: driven low before it is an output, pulled down, held, never driven high; a refused step is `TFW_ERR_INTERNAL` and nothing after it runs) | Seen held in reset on the board: the 2026-10-21 HIL-0 run does not check the C6 |
| Per-architecture code only behind the HAL, the boot HAL and the board profiles | done | `src/hal/esp32/`, `boot/hal/esp32/`, `boards/esp32p4-poe/` | `make lint-conditionals` | — |
| Board id `0x80`, the first in the ESP32 range | done | `boards/esp32p4-poe/board_profile.h` | `test_board_esp32p4-poe` (`test_the_board_is_the_first_esp32_id`); `sdk/go/boardid_test.go` | — |

Two sentences of the row's text are not requirements on the code and have no
line: the P4 is "the first MCU onboarded beside the Raspberry Pi nodes", and
"the AVR gate (#7) is lifted for this row" (a ruling; the gate still holds the
other ESP32 parts, `../esp32/engineering.md`).

## Remaining slices

Every slice below needs the bench P4; slices 2 to 6 also need the owner's
written decision first, because an eFuse cannot be put back. Each run records
`espefuse summary` before and after on `../development/bench/esp32-p4.md`.

1. **Pin peripherals and the C6 on silicon** (hardware): a boot-time
   self-check of `hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`,
   `hal_bus.c`, `hal_pulse_rmt.c` and `hal_eeprom.c`, and the C6 enable read
   at its pad, in `src/hal/esp32/hal_selfcheck.c` and `hal_sys.c` (branch
   `row/48-esp32-p4-selfcheck`, not on main), proven on the host by
   `test_esp_selfcheck` and `test_esp_sys` and on the board by its bench run;
   it closes the "Left" cells of the HAL-port and C6 lines.
2. **HIL-1, the UDS burn** (owner, hardware): `src/hal/esp32/prov_uds.c`'s
   `PROVISION=1` image burns the `HMAC_UP` block, then a `CRYPTO=hmac-p256`
   build on the P4, proven by `wirecheck -deviceid` and ES256 `ATTEST` over
   the wire with the DeviceID stable across reboots, recorded on the bench
   page; it closes the CDI, DeviceID and no-key-in-the-clear lines.
3. **HIL-2, the secure-boot key digest** (owner, hardware):
   `mk/arch/esp32-targets.mk`'s signing key burned and
   `boards/esp32p4-poe/sdkconfig.defaults`' secure boot on, proven on the
   bench by the P4 refusing a flipped image as `make esp32-qemu-test` does.
4. **HIL-3, flash encryption** (owner, hardware): release or development
   mode in `boards/esp32p4-poe/sdkconfig.defaults`, proven on the bench by
   `SPI_BOOT_CRYPT_CNT` set and layer 0 still `verified` after the reflash.
5. **HIL-4, the eFuse floor** (owner, hardware): advancing `SECURE_VERSION`
   through `src/hal/esp32/hal_ota.c`, proven on the bench by an image below
   the floor refused as `test_esp_ota` and `make esp32-qemu-test` steps 7–10
   pin on the host and in QEMU.
6. **HIL-5, download mode and JTAG** (owner, hardware): whether to burn
   `DIS_DOWNLOAD_MODE`, `ENABLE_SECURITY_DOWNLOAD`, `DIS_PAD_JTAG` and
   `DIS_USB_JTAG` (design section 4); no file of this repository changes, the
   bench page records the summary. It may end all reflashing of the board.
7. **The root type for an on-die eFuse UDS** (another project): go-xkms item
   76, a root-type value in the DICE chain, done in go-xkms first; this
   repository then carries it in the alias certificate's evidence
   (`src/caps/alias.c`), proven by `test_alias` and go-xkms's verdict no
   longer `RootTypeUnreported` (row `dice-attestation`).

When slices 1 to 6 are run, the owner can mark the SPEC row shipped; slice 7
is shared with row `dice-attestation`.
