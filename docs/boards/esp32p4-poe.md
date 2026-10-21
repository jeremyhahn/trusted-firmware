# esp32p4-poe (ESP32-P4 PoE Ethernet)

> **Status: INFORMATIVE** · SPEC row: `esp32-p4` · Scope: `boards/esp32p4-poe/`
> · Last verified: 2026-10-21
>
> A summary of what the board files say, with each value cited to where the
> build reads it. The normative contract is
> [`adding-a-board.md`](adding-a-board.md); where this page and a board file
> disagree, the board file is right and this page is the defect.

Waveshare's ESP32-P4-WIFI6-POE-ETH on the bench, with an ESP32-C6
co-processor this firmware holds in reset — no firmware of ours runs on it and
its radio is never enabled (`profile.mk:4`–`5`; the C6's enable line at
`board_profile.h:57`–`60`, held low from the first instruction of
`tfw_hal_init`). It is the first MCU onboarded beside the Raspberry Pi nodes;
the row is `esp32-p4` of [`../SPEC.md`](../SPEC.md). The board has not yet
booted this firmware: that is bench HIL-0,
[`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md)
(`board_profile.h:6`–`10`). Until then the port is exercised on Espressif's
QEMU ([`esp32-qemu.md`](esp32-qemu.md)).

## The part

| | Value | Source |
|---|---|---|
| MCU | ESP32-P4, `ESP_TARGET=esp32p4` | `profile.mk:14`–`15` |
| Ethernet | the P4's own EMAC and an IP101GRI PHY over RMII, the 50 MHz clock coming in on GPIO50 from the PHY, which makes it from its own 25 MHz crystal; PHY address 1, the PHY's strapping | `board_profile.h:42`–`55`; `sdkconfig.defaults:5`, `:15`–`17` |
| Flash | 32 MB — a GD25Q256 in the vendor schematic, JEDEC `0xC8` `0x4019` read on the bench | `sdkconfig.defaults:10`–`11`, `:18`; `partitions.csv:4` |
| Flash geometry | `TFW_FLASH_TOTAL` 16 MiB, the trailer parser's bound; the part is laid out in its first 16 MB because ESP-IDF v5.5.5 maps an app through the cache only below that on quad flash | `board_profile.h:79`–`84`, `:85`; `partitions.csv:4`, `:6`–`8` |
| EEPROM | 4 096 bytes, the `tfw_eeprom` partition | `board_profile.h:34`; `partitions.csv:15` |
| Secure boot | ESP-IDF's secure boot v2 with an RSA image signature, outside our DICE chain; layer 0 verifies the ES256 trailer with the ROM's ECDSA, because micro-ecc did not fit the P4 bootloader's iram_loader_seg | `profile.mk:19`–`22` |
| Flash encryption | on, in release mode, on top of secure boot v2; release mode is permanent once the first boot burns it | `sdkconfig.defaults:4`–`8`, `:28`–`29` |

The factory state — 33 554 432 bytes, SHA-256 `c2d4ac…0fde0`, read 2026-10-17
before the project's first write — is in `profile.mk:24`–`27`;
`make board-restore` writes it back. The bench record, including the eFuse
state (no security fuse burned) and the pin table checked against the vendor
schematic, is [`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md);
the schematic is listed by title and URL in
[`../reference/README.md`](../reference/README.md) ("Board sources").

The OTA trial deadline is 60 s, as each AVR board with a boot section
(`board_profile.h:74`–`77`).

## The partitions

`partitions.csv`, the 32 MB part laid out in its first 16 MB: two OTA slots of
6 MB and ESP-IDF's OTA data, plus the four `tfw_*` data partitions this
firmware's, `tfw_plat`, `tfw_id`, `tfw_eeprom` and `tfw_auth`
(`src/hal/esp32/hal_esp32.h`). With flash encryption off, as for bench HIL-0,
ESP-IDF ignores the `encrypted` flag. The last slot ends at `0xC20000`; the
space above stays free for data (`partitions.csv:4`–`10`, `:13`–`19`).

## Board id

`0x80`, the first id in the ESP32 range of the
[board-id registry](board-id-registry.md) (`board_profile.h:23`). The SUIT
class id is UUIDv5 `b7535d26-c463-598f-a55f-ecc936e674e1`
(`board_profile.h:25`–`28`).

## Pins in summary

| | Value | Source |
|---|---|---|
| Pins | 17, the GPIOs the board brings out on its 40-pin header less the reserved ones — GPIO 2, 3, 4, 5, 6, 7, 8, 20, 21, 22, 23, 32, 33, 46, 47, 48, 53 | `board_profile.h:30`; `board.c:15`–`38` |
| Ports | 2: GPIO 0–31 and 32–54; a pin's GPIO is base plus bit | `board_profile.h:31`; `board.c:9`–`13` |
| PWM | 4 units, LEDC channels 0–3 on timer 0 (the low-speed group only); the tone generator takes the last timer and channel | `board_profile.h:32`; `board.c:40`–`47` |
| ADC | 8 channels: ADC1 channels 0–7, GPIO 16+n; four of them on the header, 20–23 | `board_profile.h:33`; `board.c:19`, `:28`–`31` |
| Tone | GPIO6, the first pin with no PWM channel, through LEDC. It also runs to the C6's IO2, harmless while the C6 is held in reset | `board_profile.h:62`–`64`; `board.c:17`, `:25` |
| Buses | I2C on the header's GPIO 7/8, shared on the board with the ES8311 codec and the camera and display connectors; SPI on 46/47/48 | `board_profile.h:66`–`72`; `board.c:26`–`27`, `:34`–`36` |
| GPIO53 | also the speaker amplifier's enable: driving it high switches the amplifier on | `board.c:18`–`19`, `:37` |

**Reserved, so absent from the pin table:** the RMII lines and the IP101's
MDC/MDIO/reset (28–31, 34, 35, 49–52; 35 is also the BOOT key), the C6's SDIO
(14–19) and its enable (54), USB (24–27, on the header but the P4's
full-speed pins), the console UART0 (37, 38, to the CH343P bridge), strap 36,
the ES8311 codec's I2S (9–13), the SD card (39–45) and the 32 kHz crystal
(0, 1) (`board_profile.h:12`–`17`).

## Building and HIL-0

Built by `make esp32-build BOARD=esp32p4-poe` — never by `make firmware`, and
never in `make ci`, whose image has no ESP-IDF (`profile.mk:5`–`6`); the
default tier is the part's crypto tier, `hmac-p256` (`profile.mk:16`–`17`).
`ATTEST` signs with the P-256 DeviceID the `hmac-p256` tier derives from the
eFuse UDS; a tier with no UDS answers it NOT_PROVISIONED or UNSUPPORTED
(`board_profile.h:36`–`40`). The bench HIL-0 image is built `CRYPTO=none` with
the overlay `boards/esp32p4-poe/sdkconfig.hil0`, which turns off secure boot
and flash encryption so first boot burns no eFuse, while the app is still
built signed with the `--secure-pad-v2` segment that `l0_seal.py` grows for the
trailer and re-signs
([`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md)
"HIL-0"; `sdkconfig.hil0:10`–`32`).

## What is not met yet

The board has not yet run this firmware. The recorded bench runs have carried
out HIL-0's first two steps — the re-read of the factory state (positive) and
the build, an image sealed at version 1 — read-only; the write of that image
waits for the bench host to reach the board's network directly, which the
second run's record says was not met
([`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md),
"Runs"). The rows the SPEC `esp32-p4` requires on the part — secure boot and
OTA on the real eFuses, the wire over the P4's own EMAC, the identity keyed
from the UDS — are unverified there; nothing in the tree has been measured on
this board.
