# Bench inventory

**Every slice that changes the bench updates this page** (Company Agent m1480,
item 6). It lists what has arrived, what is in use, what is ordered and what is
still to buy. Each row cites the page or order that says so. A part goes in
**To buy** only when a page names it as missing.

The bench boards need no owner permission for development or test use (Company
Agent m1480). Fuses, lock bits and eFuses stay under the rules on each board's
page.

## In use

What a bench page records as wired at the bench and worked on.

| Part | What it is for | Status source |
|---|---|---|
| Arduino Mega 2560, the bench Mega | The AVR reference board, soft root, serial and Ethernet (#7) | [mega2560.md](mega2560.md): flashed and restored through its stock bootloader, latest pass on main d8d2297d, 2026-10-26 |
| Arduino Nano (ATmega328P, CH340) | The 328P reference board, element-only (#7, item 79) | [nano.md](nano.md): on USB at the bench, on its Ethernet base; nothing written yet, the factory record waits on an ISP read |
| USBasp clone | The ISP programmer for the AVR boards (#7) | [isp-route.md](../isp-route.md#the-usbasp-path-taken-owner-2026-10-15); it cannot address the Mega's low 128 KB ([mega2560.md](mega2560.md#the-usbasp-cannot-see-the-low-128-kb)) |
| ESP32-P4 PoE board (ESP32-C6 co-processor on board) | Row `esp32-p4` (#48) | [esp32-p4.md](esp32-p4.md#reflashed-with-2cfeb877s-image-2026-10-26-52-item-9): left on 2cfeb877's image, 2026-10-26 |

## Arrived

At the bench, not yet worked on from a lane.

| Part | What it is for | Status source |
|---|---|---|
| MIKROE-6774 Click board (ATECC608C) | The element tier on the AVR boards (#7) | [mega2560.md](mega2560.md#first-write-2026-10-16-eesave-item-25): did not answer on the I²C bus, 2026-10-16; [SPEC](../../SPEC.md) row `ethernet-transport`: runs once the owner wires it |
| NXP EdgeLock SE052F board | Row `secure-element-se05x` | [se052f.md](se052f.md): at the bench, nothing probed |
| ST STM32H573I-DK | Row `stm32h5` (#126) | [SPEC](../../SPEC.md) row `stm32h5`: "has arrived" |
| Infineon OPTIGA Trust M V3 shield | Row `optiga-trust-m` (#127) | [SPEC](../../SPEC.md) row `optiga-trust-m`: "has arrived" |
| Raspberry Pi Pico 2 W (RP2350) | Row `rp2350` (#128) | [SPEC](../../SPEC.md) row `rp2350`: "has arrived" |
| Nordic nRF54L15-DK | Row `nrf54l15` (#129, #17) | [SPEC](../../SPEC.md) row `nrf54l15`: "has arrived" |
| ESP32-C6 | Not recorded | Company Agent m1480, item 6 |
| ST NUCLEO-H563ZI | Row `stm32h5`, the secondary target: "it proves our own TrustZone secure world and software crypto on a part without ST's secure services" ([SPEC](../../SPEC.md)) | Company Agent ruling 226 item 7 (m1493) |
| NXP FRDM-MCXN947 | Row `mcxn947`: "the firmware core as a HAL port, with DICE evidence on the part's EdgeLock secure subsystem" ([SPEC](../../SPEC.md)) | Company Agent ruling 226 item 7 (m1493) |

## Ordered

| Part | What it is for | Status source |
|---|---|---|
| Atmel-ICE | An ISP programmer that reaches the whole ATmega2560 flash: "A USBasp firmware update, or another ISP programmer, comes first" ([mega2560.md](mega2560.md#the-usbasp-cannot-see-the-low-128-kb)) | Company Agent m1480, item 6 |
| A second MIKROE-6774 Click board (ATECC608C) | Not recorded in the order. The Nano's only tfw-boot row needs the ATECC608C ([nano.md](nano.md#whose-reset-the-usbasp-holds)) | Company Agent m1480, item 6 |

## To buy

| Part | What it is for | Status source |
|---|---|---|
| SPI NOR part on the Mega's D8 | The external OTA store: `OTAAuthLetsTheCommandPastTheGate` in `make hw-test` (#7), and the staged OTA path on the bench (row `ota`) | [mega2560.md](mega2560.md#the-mega-on-main-d8d2297d-2026-10-26): "still no SPI NOR part on D8", and the hw-test table: "A NOR part on D8 (`external store did not answer` at `ota-stage`)" |
