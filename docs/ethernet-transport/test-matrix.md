# Ethernet transport — hardware test matrix

> **Status: DRAFT** · Scope: trusted-firmware · Last verified: 2026-10-16
> What the hardware run of [the ADR](adr-wire-protocol-over-w5x00.md) covers on
> each board, now and when the ATECC608C (MIKROE-6774 Click board) arrives. The
> Mega's software tier ran on 2026-10-15 as far as serial reaches
> ([the Mega run](#the-mega-run-2026-10-15)). On 2026-10-16 the part read
> erased, and `tfw-boot` did not go in. Its two blocks were then re-provisioned
> over ISP and its application flashed again ([over ISP](#the-mega-over-isp-2026-10-16)).
> The Nano has not run.

## Values the run writes

Each board's network block is written over serial with `sdk/go`'s
`WriteNetConfig` before the Ethernet link is tested. None of these values is
in the firmware.

The addresses below are RFC 5737 documentation addresses standing in for the
bench network's. The values the bench uses come from the operator's local
configuration and are not recorded in this repository; a result recorded here
reads the same with either.

| Board | IP / mask | Gateway | Endpoint | MAC |
|---|---|---|---|---|
| Nano | `198.51.100.21/24` | none (`0.0.0.0`) | `198.51.100.1`, at the port the endpoint listens on | the shield's sticker address if supplied, else `tfw.DerivedMAC` |
| Mega | `198.51.100.22/24` | none (`0.0.0.0`) | `198.51.100.1`, at the port the endpoint listens on | the shield's sticker address if supplied, else `tfw.DerivedMAC` |

## Matrix

| Board | Port | Shield / controller | ATECC608C (MIKROE-6774 Click board) | Tier | What is tested |
|---|---|---|---|---|---|
| Mega (ATmega2560) | USB serial; Ethernet on the bench network | W5100 (probed, [docs/development/wiznet-probe.md](../development/wiznet-probe.md)) | Not fitted (ordered) | Software (`CRYPTO=soft ETH=1`) | Now: everything below except the element tier and enrolment. |
| Mega (ATmega2560) | USB serial; Ethernet on the bench network | W5100 (probed) | Fitted: Click SDA to D20, SCL to D21 | Element (`CRYPTO=atecc608a ETH=1`) | The day the ATECC608C (MIKROE-6774 Click board) arrives: the element tier and device-attest-01 enrolment. |
| Nano (ATmega328P) | USB serial; Ethernet on the bench network | W5100 expected; not probed (no serial bootloader answers; Optiboot goes in by ISP first) | Not fitted (ordered) | Software (`CRYPTO=soft ETH=1`) | Now, once Optiboot is burned: everything below except the element tier and enrolment. The software-tier Ethernet image fits the 328P's flash budget. |
| Nano (ATmega328P) | USB serial; Ethernet on the bench network | W5100 expected; not probed (no serial bootloader answers; Optiboot goes in by ISP first) | Fitted: Click SDA to A4, SCL to A5 | Element (`CRYPTO=atecc608a ETH=1`) | Its Ethernet image waits for the owner's budget choice: it is over the 328P's flash budget today ([the options](adr-wire-protocol-over-w5x00.md#the-328p-budget)). The day the ATECC608C (MIKROE-6774 Click board) arrives and that choice is made: the element tier and device-attest-01 enrolment. |
| ATmega32U4, ATtiny85 | — | None | — | — | Covered by the simulator. |

## What is tested now, on each board

1. **Flash.** The Ethernet image is built for the board's shield and flashed;
   its size-check passes.
2. **Firmware tests over serial.** The serial suite runs as on any image, with
   the shield's pins reported in use and tone refused.
3. **The Ethernet link with a static address.** The block above is written over
   serial. The device brings the link up from it without a reset.
4. **TCP to the control plane's endpoint.** The device opens the connection to
   `198.51.100.1` and a `tfw.Listener` there accepts it.
5. **Attestation commands over TCP on the tier the board has.** `CAPS` reports
   the software tier. `ATTEST` returns evidence over the host's nonce, and it
   verifies under the device's key. `ATTEST_CERT` and `ATTEST_CSR` answer
   "unknown command" on this tier, as they do over serial.
6. **The off-allow-list refusal.** `PING` and `EEPROM_WRITE` over TCP answer
   "unknown command", and the EEPROM is unchanged; the same commands work over
   serial.
7. **The same identity after a reset.** After a reset the device reconnects
   with the same MAC. Its `ATTEST` evidence carries the same DeviceID and
   verifies under the same key.

## The Mega run, 2026-10-15

The Mega 2560 with its W5100 shield, on `/dev/ttyACM0`, through its stock
STK500v2 bootloader. The images were built with the toolchain
[the probe records](../development/wiznet-probe.md#toolchain). There were two
passes that day. The first ran items 1 and 2 and stopped at the network block,
which needs the device's serial. The second gave the part its platform block
and ran what serial reaches.

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=soft`, first pass | 13290 / 765 bytes | `d636f84bfa700cfdcde308db7fe49209036519ef96347b3602be0c715dc0cc3a` |
| `BOARD=atmega2560 CRYPTO=soft ETH=1` (W5100), first pass | 15254 / 785 bytes | `181b5eda8de8ce21052796d6fe0605ec4e07badf0cef1772f9249507cb959858` |
| `BOARD=atmega2560 CRYPTO=soft ETH=1` (W5100), second pass | 15262 / 785 bytes | `923646184b196a7ac1873e1ab42448cfac3cde791b99b7ff33812137bb0b26bb` |

The second pass's platform block is `tfwprov --bake-eeprom --serial
54465700000000000000000000002560`, with no pubkey hash and no operator key,
which the software tier never reads. The SHA-256 of its `.eep` is
`1f1d2e349937a2f436f240d2069356e3beb08e10ce7116d0d4e50dd5aeed8c29`.

| # | What ran | Result |
|---|---|---|
| 1 | The images built, size-checked, and flashed with `make flash` (avrdude wrote and verified) | Pass, both passes |
| 2 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`: the whole cross-implementation suite, over serial. In the first pass, on both images, and by `tfwctl` on the Ethernet image: writing D4, D10 and D50–D53, PWM on D10 and `SPI_XFER` with D10 as chip select | Pass. First pass, on both images: each shield pin answers "pin is reserved", `SPI_XFER` answers "target is platform-reserved", and tone is refused; D9 and D22 still work. Second pass, on the Ethernet image with the platform block in place: pass, and the block was unchanged afterwards |
| — | The platform block at `0x000`, second pass | Written through the stock bootloader by `avrdude -c wiring -p m2560 -D -U eeprom:w:platform.eep:i`. avrdude reported `verification mismatch`, `device 0x54 != input 0x00 at addr 0x001c`. The application's `EEPROM_READ` then read all 192 bytes back equal to the file, the CRC included. The bootloader's EEPROM read is at fault, not its write ([provisioning](../device-provisioning/engineering.md)). The first pass misread the block the same way. The part did not hold a torn write of this layout. It held a layout-1 block with the pre-rename magic `'A','G','P','R'`, which a current build rejects by its magic. The bench bake image was not needed |
| 3 | The network block (`198.51.100.22/24`, gateway `0.0.0.0`, endpoint `198.51.100.1:7020`, `tfw.DerivedMAC`), second pass | Written over serial with `WriteNetConfig`. The MAC is `tfw.DerivedMAC` of the serial, `5a:ed:09:10:bf:59`. Read back over serial, it equals `MarshalBinary`'s 32 bytes. The bench's own addresses were not available to the run, so these documentation values are what the device holds; the bench network needs the block written again with its own. Over serial there is no way to see whether the device brings the link up; item 4 is that test |
| — | Reading back the W5100's `GAR`, `SUBR`, `SHAR` and `SIPR` | **Not run.** No diagnostic exposes them. The firmware writes those registers from a valid network block and never reads them back, and `SPI_XFER` refuses the shield's chip select |
| 4 | TCP between the device and the lane's container on the bench host | **Not run as specified**, both passes. The container has only its Docker bridge address and loopback, with no address on the board's network. In the second pass, a connect to `198.51.100.22:7020` with a 5 s timeout timed out unanswered. Reaching the board's network is Infrastructure's |
| 5–6 | Attestation over TCP, the off-allow-list refusal | **Not run.** They need item 4 |
| 7 | The same identity after a reset, over serial, second pass | **Partly run.** After a soft `RESET`, and the reset each port open causes, the serial at `0x034` and the MAC in the network block read back unchanged. The evidence half cannot run on this image. Behind a stock bootloader the software tier has no UDS (`src/crypto/soft/uds_flash.c`), so `ATTEST` answers "no provisioned identity" whatever the platform block holds. Evidence needs `tfw-boot` handing off a CDI, which needs ISP to install, or the element tier |

## The Mega over ISP, 2026-10-16

A USBasp on the Mega's ICSP header ran the first steps toward `tfw-boot`,
which item 7's evidence needs
([the ISP route](../development/isp-route.md#the-mega-2026-10-16)). Nothing
was written to the Mega.

| What ran | Result |
|---|---|
| Signature and fuses over ISP | `0x1E9801`; fuses `0xFF` (low), `0xD8` (high), `0xFD` (extended); lock `0xCF`. The same before and after the run |
| The EEPROM, read out over ISP | All `0xFF`. The platform block and network block of the second pass above are gone, and so is the application. The STK500v2 bootloader is intact. They were erased between that pass and this run, by something this run did not see |
| `make boot-flash BOARD=atmega2560 ISP_PROG=usbasp ISP_PORT=usb` | **Did not build.** The default row (soft, ed25519-donna) does not fit the 8 KB boot section, and avrdude never ran. The supported element row builds, but needs the ATECC608C and a provisioned EEPROM |
| `make hw-test`, the identity and the `tfw-boot` evidence | **Not run.** There is no application on the part and no `tfw-boot` |
| Re-provisioning over ISP, later that day (Company Agent m233: the erase was an Arduino IDE "Burn Bootloader") | **Pass.** The platform block came from `tfwprov --bake-eeprom --serial 54465700000000000000000000002560` (the same `.eep` as the second pass). The network block came from `NetConfig.MarshalBinary` with the second pass's values. Both were written with `avrdude -c usbasp -p m2560 -D -U eeprom:w:…:i`: no erase, no fuse, no lock. Read back over ISP, `0x000`–`0x0FF` equals the second pass's dump byte for byte |
| The second pass's application, through the stock bootloader | **Pass.** `make flash BOARD=atmega2560 CRYPTO=soft ETH=1` wrote the same `.hex` (`92364618…`). `CAPS` answers atmega2560 on the software tier, and `tfwctl eeprom-read` reads `0x000`–`0x0DF` equal to the image, the serial and the MAC included. `make hw-test` and `ATTEST` were not run ([the record](../development/isp-route.md#the-mega-re-provisioned-over-isp-2026-10-16)) |

## The day the ATECC608C (MIKROE-6774 Click board) arrives

The Click carries three ATECC608 parts on one I2C bus: an ATECC608C-TNGTLS at
`0x35`, an ATECC608C-TFLXTLS at `0x36` and an ECC608-TMNGTLS at `0x38`, with
4.7 kΩ pull-ups (R5, R6) to the Click's VCC
([ECC608 Trust Click schematic v100](https://download.mikroe.com/documents/add-on-boards/click/ecc608-trust-click/ecc608-trust-click-schematic.pdf)).
The element tier talks to the TFLXTLS at `0x36`, its address as shipped
(DS40002515B §3.1,
[ATECC608C-TFLXTLS data sheet](https://ww1.microchip.com/downloads/aemDocuments/documents/SCBU/ProductDocuments/DataSheets/ATECC608C-TFLXTLS-CryptoAuthentication-Data-Sheet-DS40002515.pdf)),
at 100 kHz. Both board profiles carry the address, the bus speed and the TWI
pins.

Wiring, by flying leads from the Click's mikroBUS header:

- **JP1 (VCC SEL) to 5V.** The schematic does not show which way the jumper
  ships, so set it before power. The part takes a 2.0–5.5 V supply and
  1.8–5.5 V I/O (DS40002515B, page 1), and the pull-ups then sit at the AVR's
  5 V.
- **SDA and SCL** to A4 and A5 on the Nano, D20 and D21 on the Mega. They do not
  collide with the shield's D10 and D4, or D50–D53 on the Mega.
- **+5V and GND** to the board. CS (the ClickID chip) stays unconnected.

The TFLXTLS ships with its configuration and data zones locked and its slot
policies set by Microchip (DS40002515B §3). Its slot map is not the one the
provisioning flow writes, so the element tier answers `NOT_PROVISIONED` until
the owner chooses a slot map for it
([provisioning](../device-provisioning/engineering.md#the-atecc608c-mikroe-6774-click-board);
the options and the bench reads that come first are in
[the slot-map ADR](../device-provisioning/adr-trustflex-608c-slot-map.md)).

Once it is provisioned, the element tier and device-attest-01 enrolment run
on each board whose element image builds:

- `CAPS` reports the element tier.
- `ATTEST`, `ATTEST_CERT` and `ATTEST_CSR` work over TCP and verify under the
  DeviceID key.
- The device enrols through device-attest-01 over the TCP connection.
- The MAC, unless a sticker address was supplied, is re-derived from the
  DeviceID public key and rewritten. The element tier's DeviceID differs from
  the software tier's serial-based one.

## What the simulator covers today

`make sdk-test` runs the element-tier firmware, compiled for the host, as an
Ethernet device on a loopback socket (`tfwsim --eth`). It covers `CAPS`,
`ATTEST`, `ATTEST_CERT` and `ATTEST_CSR` over TCP, the off-allow-list refusal,
and serial and Ethernet answering side by side.
