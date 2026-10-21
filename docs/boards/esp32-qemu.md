# esp32-qemu (Espressif's QEMU)

> **Status: INFORMATIVE** · SPEC rows: `esp32`, `esp32-p4` (stand-in) · Scope:
> `boards/esp32-qemu/` · Last verified: 2026-10-21
>
> A summary of what the board files say, with each value cited to where the
> build reads it. The normative contract is [`adding-a-board.md`](adding-a-board.md);
> where this page and a board file disagree, the board file is right and this
> page is the defect.

The emulated machine Espressif ships, with the OpenCores Ethernet MAC it models.
It is a test target, not a part: the ESP32-P4 port is exercised here on both of
its machines until the PoE board arrives
([`esp32p4-poe.md`](esp32p4-poe.md)) (`board_profile.h:4`–`11`). The decision
is [`../multi-arch/adr-esp-idf-integration.md`](../multi-arch/adr-esp-idf-integration.md).

## The machine

One board id for both of the emulated machines — ids are wire values, and the
fewest are taken — because CAPS already tells a host the one fact that differs
between them, the crypto tier (`board_profile.h:9`–`11`).
The machine is chosen by `ESP_TARGET` (`profile.mk:22`); the comment
naming the two machines and their tiers is `profile.mk:6`–`14`. The image's
`ARCH` is `esp32` (`profile.mk:19`), so the toolchain is ESP-IDF's, pinned to
`v5.5.5` (`mk/arch/esp32.mk:20`).

| | Value | Source |
|---|---|---|
| Machines | `esp32c3` (RISC-V, like the P4; emulates the HMAC peripheral; tier `hmac-p256`, the default) and `esp32` (the original Xtensa ESP32; no HMAC; tier `none`, no identity) | `profile.mk:8`–`10`, `:11`–`13`, `:28`–`29` |
| Ethernet | the OpenCores MAC QEMU emulates, `CONFIG_ETH_USE_OPENETH=y` | `sdkconfig.defaults:4`, `:9` |
| Flash | 4 MB, the flash the emulator is given | `partitions.csv:4`; `sdkconfig.defaults:4`–`5`, `:11` |
| Flash geometry | `TFW_FLASH_TOTAL` 16 MiB, the trailer parser's bound, past any slot | `board_profile.h:55`–`61`, `:62` |
| EEPROM | 4 096 bytes, the `tfw_eeprom` partition | `board_profile.h:33`; `partitions.csv:10` |
| Anti-rollback | on the C3 machine only, where the test's eFuse is a file QEMU writes: OTA_CONFIRM raises its secure-version floor; the P4 board never sets it | `profile.mk:35`–`40` |

Layer 0 verifies the ES256 trailer with the AVR rows' p256-soft over ESP-IDF's
micro-ecc; neither machine's ROM has ECDSA (`profile.mk:31`–`33`). Flash
encryption stays off: the subject of the test is secure boot and the identity,
not the encryption ESP-IDF performs, which the P4 board turns on
(`sdkconfig.defaults:5`–`7`; see [`esp32p4-poe.md`](esp32p4-poe.md)). The original ESP32's build pins chip revision 3,
which secure boot v2 needs, and the test's eFuses state it
(`sdkconfig.defaults.esp32:4`–`6`).

The OTA trial deadline is 60 s, as each AVR board with a boot section
(`board_profile.h:67`–`70`).

## The partitions

`partitions.csv`, the 4 MB of flash the emulator is given: two OTA slots of
1 966 080 bytes and ESP-IDF's OTA data, plus the four `tfw_*` data partitions
this firmware's, `tfw_plat`, `tfw_id`, `tfw_eeprom` and `tfw_auth`
(`src/hal/esp32/hal_esp32.h`). `tfw_plat` and `tfw_eeprom` carry the
`encrypted` flag, which takes effect only when flash encryption is on
(`partitions.csv:6`, `:8`–`14`).

## Board id

`0x81` (`board_profile.h:22`), in the ESP32 range of the
[board-id registry](board-id-registry.md). The SUIT class id is
UUIDv5 `1023e2f4-fdd0-5f12-9902-c62181fa5681` (`board_profile.h:24`–`27`).

## Pins in summary

| | Value | Source |
|---|---|---|
| Pins | 5: logical 0–4 on GPIO 2, 4, 5, 18 and 19 — the GPIOs both machines have and neither spends on its flash or its console | `board_profile.h:13`–`14`, `:29`; `board.c:14`–`22` |
| Ports | 1: GPIO 0–31; the bit is the GPIO number | `board_profile.h:30`; `board.c:9`–`12` |
| PWM | 4 units, LEDC channels 0–3 on timer 0; the tone generator takes the last timer and channel | `board_profile.h:31`; `board.c:24`–`33` |
| ADC | none: neither machine models an ADC this port uses | `board_profile.h:15`–`16`, `:32` |
| Tone | GPIO19, the one pin with no PWM channel, through LEDC | `board_profile.h:41`–`43` |
| Buses | I2C on GPIO 4/5, SPI on GPIO 18/19/2; neither machine models the peripherals, so the pins are never driven in QEMU | `board_profile.h:45`–`51` |

QEMU models GPIO levels and little else, so the table exists to keep the board
honest, not to drive loads (`board_profile.h:14`–`15`).

## What is tested, and how

Not a `make firmware` image, and never in `make ci`, whose image has no
ESP-IDF (`profile.mk:15`–`17`). The build is
`make esp32-build BOARD=esp32-qemu [ESP_TARGET=esp32]`
(`mk/arch/esp32-targets.mk:42`–`100`), and the test is
`make esp32-qemu-test` (`mk/arch/esp32-targets.mk:102`–`127`), which needs the
pinned QEMU, `esp_develop_9.2.2_20260417` (`mk/arch/esp32.mk:21`). It builds
the C3 row, a `PROVISION=1` C3 row and the original-ESP32 row
(`mk/arch/esp32-targets.mk:114`–`116`), then runs the two machines side by side
in QEMU. The C3 run carries the identity — the CDI and the P-256 DeviceID
checked against the literal point of a fixed UDS, its ATTEST verified as ES256
— and the esp32 run, with no HMAC peripheral, checks secure boot, layer 0's
ES256 gate, the wire over Ethernet, and the ATTEST refusal. Both check layer 0's
gate and refuse tampered and mis-sealed images and manifests; the C3 run also
boots the `PROVISION=1` image on a part with no UDS
(`mk/arch/esp32-targets.mk:102`–`110`). `ATTEST` signs with the P-256 DeviceID
the `hmac-p256` tier derives from the eFuse UDS; a tier with no UDS answers it
NOT_PROVISIONED or UNSUPPORTED, as an AVR board without an element does
(`board_profile.h:35`–`39`).

## Not met yet

This is an emulated test target: nothing in the tree runs on silicon. The
port's physical board is [`esp32p4-poe.md`](esp32p4-poe.md), which has not yet
booted this firmware (bench HIL-0,
[`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md)).
