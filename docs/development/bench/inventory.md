# Bench inventory

**Every slice that changes the bench updates this page** (Company Agent m1480,
item 6). It lists each board at the bench and what has been done with it, what
is ordered and what is still to buy. Each cell cites the page, section or
commit that says so. A part goes in **To buy** only when a page names it as
missing.

The bench boards need no owner permission for development or test use (Company
Agent m1480). Fuses, lock bits and eFuses stay under the rules on each board's
page.

**Exception: the NXP FRDM-IMX95.** Nothing on it is programmed, fused, closed
or written without the owner, an ordinary firmware flash included: these
boards become the HA HSM cluster (CA ruling 583,
[frdm-imx95.md](frdm-imx95.md)). Only read-only work is open on it.

## Boards

Facts as of 2026-11-16 16:02Z (the STM32H573I-DK row: 2026-11-17). "Connected
now" is the bench host's USB at that time: an STLINK-V3 `0483:374e`, a CH340
`1a86:7523`, a WCH Quad_Serial `1a86:55e8` and a WCH Single Serial
`1a86:55d3`. No Arduino Mega 2560 (`2341:0042`), no Pico 2 W (`2e8a:000f`), no
J-Link, no MCU-Link and no Atmel-ICE. A record that lives only on an open PR's
branch is marked "pending PR #N": it is not yet a fact on main.

| Board | Connected now | Probed | Flashed with our firmware | HIL passing |
|---|---|---|---|---|
| Arduino Mega 2560, the bench Mega (#7) | **Not connected**: no `2341:0042` on 11-16 | Yes: signature `0x1E9801` through the stock bootloader, read only ([mega2560.md](mega2560.md#the-mega-on-main-e3a07e05-2026-10-29), item 1); the Atmel-ICE ISP runs of 11-10 did not get past the signature read, and nothing was written ([mega2560.md](mega2560.md#the-atmel-ice-isp-run-2026-11-10-347)) | Yes: `make flash BOARD=atmega2560 CRYPTO=soft`, main e3a07e05, 2026-10-29, the last write ([mega2560.md](mega2560.md#the-mega-on-main-e3a07e05-2026-10-29), item 4) | **Passed, since regressed.** `make hw-test` 28 of 28 on main b0eab87, 2026-10-18 ([test-matrix.md](../../ethernet-transport/test-matrix.md#the-mega-on-main-b0eab87-2026-10-18)). Later: 41 of 45 `TestFirmware*` rows on main a0f9902a, 2026-10-26 ([mega2560.md](mega2560.md#the-mega-reflashed-with-main-a0f9902as-image-2026-10-26-52-item-9), item 3); 33 of 55 on main e3a07e05, 2026-10-29 ([mega2560.md](mega2560.md#the-mega-on-main-e3a07e05-2026-10-29), item 5) |
| Arduino Nano (ATmega328P, CH340) (#7, item 79) | CH340 `1a86:7523`, the Nano's adapter ([nano.md](nano.md#the-bench-change-2026-10-18)) | Partly: no bootloader answers, signature and fuses not read, they need ISP ([nano.md](nano.md#factory-values)) | No: nothing written ([nano.md](nano.md)) | Never run |
| ESP32-P4 PoE board, row `esp32-p4` (#48) | WCH Single Serial `1a86:55d3` (CH343), the P4's adapter ([nano.md](nano.md#the-bench-change-2026-10-18)) | Yes: `chip-id` ESP32-P4 revision v3.2 ([esp32-p4.md](esp32-p4.md#the-reads-2026-10-17)), again on 11-14 ([esp32-p4.md](esp32-p4.md#row-48-flash-layer-0-boot-check-wire-over-ethernet-2026-11-14), step 1) | Yes, several times; last row 48's image, `App version` `…-ge41807f7`, `CRYPTO=none`, 2026-11-14, layer 0 verified, no eFuse moved; the board is left on it ([esp32-p4.md](esp32-p4.md#row-48-the-wire-over-ethernet-on-vlan-101-2026-11-14)) | `make hw-test`: never run. HIL-0's `wirecheck` (PING, CAPS, SYS_INFO, ATTEST refused on the none tier) passed on 2026-10-21 ([esp32-p4.md](esp32-p4.md#runs)) and 2026-11-14 ([esp32-p4.md](esp32-p4.md#row-48-the-wire-over-ethernet-on-vlan-101-2026-11-14), step 4) |
| ESP32-C6, standalone | No record of its adapter; none of the 11-16 IDs is attributed to it | No record. The P4 board's on-board C6 is held in reset ([esp32-p4.md](esp32-p4.md#pins)) | No | Never run |
| ST STM32H573I-DK, row `stm32h5` (#126) | An STLINK-V3 `0483:374e` is on the host; no page records whether it is this board's or the NUCLEO's | Yes: 2026-11-17 (#535), factory record taken first ([stm32h573i-dk.md](../../boards/stm32h573i-dk.md#bench-run)) | Yes, 2026-11-17: main's image halts in tfw-boot without provisioning; the bench image (`make boot-flash BOOT_FLASH_HEX=<image>`) boots ([stm32h573i-dk.md](../../boards/stm32h573i-dk.md#bench-run)) | `make hw-test`: cannot run on the board (no row list, no pin facts); rows driven with `tfwctl` passed, 2026-11-17 ([stm32h573i-dk.md](../../boards/stm32h573i-dk.md#feature-matrix)) |
| ST NUCLEO-H563ZI, row `stm32h5` | An STLINK-V3 `0483:374e` is on the host; no page records whether it is this board's or the DK's | No ([nucleo-h563zi.md](../../boards/nucleo-h563zi.md): "built and host-tested, not yet run on the board", 2026-11-12) | No | Never run |
| NXP FRDM-MCXN947, row `mcxn947` | **Not connected**: no MCU-Link on 11-16 | No ([frdm-mcxn947.md](../../boards/frdm-mcxn947.md): "at the bench, ... not yet worked on") | No | Never run |
| NXP FRDM-IMX95, M7 core, row #428 | Probably the WCH Quad_Serial `1a86:55e8`: the board's J22 debug port is a CH9114 quad UART ([frdm-imx95.md](frdm-imx95.md), the Consoles row); no page records the ID against the board | Yes, read only on main: the factory record, ELE lifecycle OEM Open, SRK hashes not fused, nothing written ([frdm-imx95.md](frdm-imx95.md), eea442a4) | Pending PR #449: the M7 boot and console records are only on its branch. No write without the owner | Never run on main; pending PR #449 |
| Raspberry Pi Pico 2 W (RP2350), row `rp2350` (#128) | Unconfirmed (not enumerated in BOOTSEL; application does not drive USB): no `2e8a:000f` on 11-16, which is the BOOTSEL ID only; the write path holds the board in BOOTSEL and the port does not drive USB ([rp2350-pico2w.md](../../boards/rp2350-pico2w.md#the-port)), so a cabled Pico running its application would not show either. No record says whether it is cabled | Pending PR #455 (the factory record, read before any write) | Pending PR #455 (a HIL boot of main's default image) | Never run on main ([rp2350-pico2w.md](../../boards/rp2350-pico2w.md): "has never run on the board"); pending PR #455, whose feature check was not run |
| Nordic nRF54L15-DK, row `nrf54l15` (#129, #17) | **Not connected**: no J-Link on 11-16 | No ([nrf54l15-dk.md](../../boards/nrf54l15-dk.md): "nothing here has been measured on the board") | No | Never run |
| Infineon OPTIGA Trust M V3 shield, row `optiga-trust-m` (#127) | Not connected to a host board: no page records it wired | No ([SPEC](../../SPEC.md) row `optiga-trust-m`: "has arrived") | Not applicable: an element, not a target | Never run |
| NXP EdgeLock SE052F board, row `secure-element-se05x` | Not recorded: "Port on the bench host: not recorded" ([se052f.md](se052f.md)) | No: "Nothing has been probed, written or fused" ([se052f.md](se052f.md)) | Not applicable: an element, not a target | Never run |
| MIKROE-6774 Click board (ATECC608C), the AVR element tier (#7) | Not connected: the Mega it sat on is not on USB | Yes, no answer: no ACK at `0x35` or `0x38` on the Mega's I²C bus, 2026-10-16 ([mega2560.md](mega2560.md#first-write-2026-10-16-eesave-item-25)) | Not applicable: an element, not a target | Never run: [SPEC](../../SPEC.md) row `ethernet-transport` runs it once the owner wires it |

**Confirmed working:** the Arduino Mega 2560 only, the one board with a
passing `make hw-test`: 28 of 28 on main b0eab87, 2026-10-18
([test-matrix.md](../../ethernet-transport/test-matrix.md#the-mega-on-main-b0eab87-2026-10-18)),
since regressed (33 of 55 on main e3a07e05, 2026-10-29,
[mega2560.md](mega2560.md#the-mega-on-main-e3a07e05-2026-10-29)). The ESP32-P4
passes HIL-0's wire check but has never run `make hw-test`.

**Needs connecting** (no adapter on the bench host's USB on 11-16): the
Arduino Mega 2560, the NXP FRDM-MCXN947 (MCU-Link), the Nordic nRF54L15-DK
(J-Link), the MIKROE-6774 ATECC608C (on the Mega), the OPTIGA Trust M shield
and the SE052F board (each on a host board), and the Atmel-ICE. One of the two
ST boards also needs its STLINK-V3 on USB if the one seen is the other's. The
Raspberry Pi Pico 2 W only if it is not cabled: its connection is unconfirmed
(not enumerated in BOOTSEL; application does not drive USB), see its row.

## Programmers

| Part | What it is for | Status source |
|---|---|---|
| USBasp clone | The ISP programmer for the AVR boards (#7) | [isp-route.md](../isp-route.md#the-usbasp-path-taken-owner-2026-10-15); it cannot address the Mega's low 128 KB ([mega2560.md](mega2560.md#the-usbasp-cannot-see-the-low-128-kb)) |
| Atmel-ICE | An ISP programmer that reaches the whole ATmega2560 flash | At the bench: used on the Mega 2026-11-10; no run got past the signature read ([mega2560.md](mega2560.md#the-atmel-ice-isp-run-2026-11-10-347)); not on the bench host's USB on 11-16 |

## Ordered

| Part | What it is for | Status source |
|---|---|---|
| A second MIKROE-6774 Click board (ATECC608C) | Not recorded in the order. The Nano's only tfw-boot row needs the ATECC608C ([nano.md](nano.md#whose-reset-the-usbasp-holds)) | Company Agent m1480, item 6 |

## To buy

| Part | What it is for | Status source |
|---|---|---|
| SPI NOR part on the Mega's D8 | The external OTA store: `OTAAuthLetsTheCommandPastTheGate` in `make hw-test` (#7), and the staged OTA path on the bench (row `ota`) | [mega2560.md](mega2560.md#the-mega-on-main-d8d2297d-2026-10-26): "still no SPI NOR part on D8", and the hw-test table: "A NOR part on D8 (`external store did not answer` at `ota-stage`)" |
