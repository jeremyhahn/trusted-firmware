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
| Mega (ATmega2560) | USB serial; Ethernet on the bench network | W5100 (probed, [docs/development/wiznet-probe.md](../development/wiznet-probe.md)) | Not fitted (ordered) | Software (`CRYPTO=soft`; `ETH=1` is the 2560's default since item 48) | Now: everything below except the element tier and enrolment. |
| Mega (ATmega2560) | USB serial; Ethernet on the bench network | W5100 (probed) | Fitted: Click SDA to D20, SCL to D21 | Element (`CRYPTO=atecc608a`, `ETH=1` by default) | The day the ATECC608C (MIKROE-6774 Click board) arrives: the element tier and device-attest-01 enrolment. Optional: the soft DICE root is the default and the element is never required. |
| Mega (ATmega2560) | USB serial only | None used: W5100 fitted in [the 10-15 first pass](#the-mega-run-2026-10-15), no shield in [fd7a696](#the-bench-mega-on-main-fd7a696-2026-10-16) and [train/ota75e](#the-bench-mega-on-trainota75e-2026-10-16) | Not used by this tier; did not answer in [fd7a696](#the-bench-mega-on-main-fd7a696-2026-10-16) | Software, serial only: the shieldless bench Mega, its own row (`CRYPTO=soft ETH=0`, item 48; before it, `ETH=0` was the default) | Run in [the 10-15 first pass](#the-mega-run-2026-10-15), [fd7a696](#the-bench-mega-on-main-fd7a696-2026-10-16) and [train/ota75e](#the-bench-mega-on-trainota75e-2026-10-16): built, size-checked, flashed with `make flash`, and `make hw-test`'s whole cross-implementation suite over serial, each a pass. In fd7a696 and train/ota75e: `0x000`–`0x0FF` read back unchanged across the flash, and the `OTA_*` commands and `tfwctl ota-stage`/`ota-status` answer "unknown command", as an image linking `ota_stage_none.c` does. No TCP item applies. |
| Nano (ATmega328P) | USB serial; Ethernet on the bench network | W5100 expected; not probed (no serial bootloader answers; Optiboot goes in by ISP first) | Not fitted (ordered) | Software, the soft-crypto Ethernet node (`CRYPTO=soft ETH=1 OTA_STAGE=0`, item 48) | Now, once Optiboot is burned: everything below except the element tier, enrolment and OTA staging. The row links no stager (one update path per image; the 328P's boot cannot take an image from the external store), so its update path is serial and the `OTA_*` commands answer "unknown command". With the stager it was 760 bytes over the flash budget. |
| Nano (ATmega328P) | USB serial; Ethernet on the bench network | W5100 expected; not probed (no serial bootloader answers; Optiboot goes in by ISP first) | Fitted: Click SDA to A4, SCL to A5 | Element (`CRYPTO=atecc608a ETH=1`) | Not a supported row (item 48: the 328P's Ethernet node is the software tier). Its Ethernet image waits for the owner's budget choice: it is over the 328P's flash budget today ([the options](adr-wire-protocol-over-w5x00.md#the-328p-budget)). The day the ATECC608C (MIKROE-6774 Click board) arrives and that choice is made: the element tier and device-attest-01 enrolment. |
| Nano (ATmega328P) | USB serial only | None | Not used by this tier | Software, serial only (`CRYPTO=soft`, no `ETH`) | Not run. It waits, as the Nano's other rows do, for Optiboot to go in by ISP first ([the Nano's page](../development/bench/nano.md)). |
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

Items 1 and 2 apply to the serial-only build too, with its image in place of
the Ethernet one; items 3 to 6 need the Ethernet link.

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
| `make boot-flash BOARD=atmega2560 ISP_PROG=usbasp ISP_PORT=usb` | **Did not build.** The default row (soft, ed25519-donna) does not fit the 8 KB boot section, and avrdude never ran. Since item 79 (owner ruling 2026-10-17) the default is the 2560 soft row, `p256-soft` with no OTA: the verifier is P-256 on every row. The supported element row builds, but needs the ATECC608C and a provisioned EEPROM |
| `make hw-test`, the identity and the `tfw-boot` evidence | **Not run.** There is no application on the part and no `tfw-boot` |
| Re-provisioning over ISP, later that day (Company Agent m233: the erase was an Arduino IDE "Burn Bootloader") | **Pass.** The platform block came from `tfwprov --bake-eeprom --serial 54465700000000000000000000002560` (the same `.eep` as the second pass). The network block came from `NetConfig.MarshalBinary` with the second pass's values. Both were written with `avrdude -c usbasp -p m2560 -D -U eeprom:w:…:i`: no erase, no fuse, no lock. Read back over ISP, `0x000`–`0x0FF` equals the second pass's dump byte for byte |
| The second pass's application, through the stock bootloader | **Pass.** `make flash BOARD=atmega2560 CRYPTO=soft ETH=1` wrote the same `.hex` (`92364618…`). `CAPS` answers atmega2560 on the software tier, and `tfwctl eeprom-read` reads `0x000`–`0x0DF` equal to the image, the serial and the MAC included. `make hw-test` and `ATTEST` were not run ([the record](../development/isp-route.md#the-mega-re-provisioned-over-isp-2026-10-16)) |

## The boot-flash EESAVE guard on the Mega, 2026-10-16

The same Mega, after the re-provisioning above, through a USBasp on ISP
(`-c usbasp -P usb`), with avrdude 7.1.
The run was read-only. A wrapper named `avrdude`, first on `PATH`, passed a
command to the real avrdude only when every `-U` was a read (`:r:`) and there
was no `-e`, `-t`, `-T` or `-x`. It logged and refused anything else. Before
the run it refused a fake `-n -U hfuse:w:0xD0:m`, an `-e`, a bare
`-U flash.hex` and an EEPROM write. No fuse, lock bit or memory was written.

| # | What ran | Result |
|---|---|---|
| 1 | `avrdude -p atmega2560 -c usbasp -P usb -q -q -U <fuse>:r:-:h` for `hfuse`, `lfuse`, `efuse` and `lock`, which is how the guard reads `hfuse`, with stdout and stderr captured apart | stdout is the byte and a newline, nothing else: `0xd8`, `0xff`, `0xfd`, `0xcf`. stderr is one line, `avrdude error: cannot set sck period; please check for usbasp firmware update`, and the exit status is 0. The guard's `$(...)` takes only stdout, so the old programmer firmware does not break the parse |
| 2 | `make boot-flash BOARD=atmega2560 BOOT_TIER=atecc608a BOOT_MEASURE=atecc608a BOOT_SIG=atecc608a BOOT_OTA=1`, the supported OTA row | Refused, exit 2: `boot-flash: REFUSED. EESAVE is not programmed (hfuse=0xd8, bit 3 reads 1)`. It printed the fix `avrdude -pm2560 -cusbasp -Pusb -U hfuse:w:0xd0:m` and the `BOOT_EEPROM_RESTORE=1` route. The wrapper saw one avrdude call, the `hfuse` read, and refused none |
| 3 | The fuses read again, and `-U eeprom:r:<file>:r` before and after item 2 | Fuses unchanged (`ff`/`d8`/`fd`, lock `cf`). The two 4096-byte reads are equal, SHA-256 `9860d5e87779c86952f36667f3e23e1bb384342452ac103c89ffd4ecc8581cff`. The 192-byte platform block at `0x000` equals, byte for byte, `tfwprov --bake-eeprom --serial 54465700000000000000000000002560`. That rebuilt `.eep` has the 10-15 SHA-256 above, so the block is the one the re-provisioning wrote |

`BOOT_EEPROM_RESTORE=1` was not run, because it writes by design. EESAVE stays
unprogrammed on this part until the owner decides otherwise.

## The Mega on main 9ee1db3, 2026-10-16

The same Mega and W5100 shield, on `/dev/ttyACM0`, through its stock
STK500v2 bootloader only: no ISP, no erase, no fuse or lock access. Main had
moved on since the passes above: OTA, the 2560's swap journal at
`0x0E0`–`0x0E9`, and the firmware's refusal of host writes there. The toolchain
is [the probe's](../development/wiznet-probe.md#toolchain), unpacked under
`/tmp`.

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=soft ETH=1` (W5100) at `9ee1db3` | 17494 / 797 bytes | `741a499bc5d55841faf60f4a5b598aea921ead2211c1768bac35947355bdb875` |

| # | What ran | Result |
|---|---|---|
| 1 | The image built, size-checked, and flashed with `make flash` (`avrdude -c wiring … -D`) | Pass. avrdude wrote and verified 17494 bytes |
| — | The platform block and network block, read back over serial with `tfwctl eeprom-read`, `0x000`–`0x0FF` | **Present; not re-provisioned.** The 192-byte platform block equals, byte for byte, `tfwprov --bake-eeprom --serial 54465700000000000000000000002560`, whose `.eep` has the 10-15 SHA-256 above. The network block at `0x0C0` holds the second pass's values: `198.51.100.22/24`, gateway `0.0.0.0`, endpoint `198.51.100.1:7020`, MAC `5a:ed:09:10:bf:59`. `0x0E0`–`0x0FF` read `0xFF` |
| 2 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`: the whole cross-implementation suite, over serial | Pass. The suite needs `make tfwsim tfwsim-app-example` first: without `build/host/tfwsim-app-heartbeat`, `TestAgainstFirmwareAppInfo` fails and says so |
| — | Host writes to the swap journal with `tfwctl eeprom-write`, each writing the bytes already there | `0x0E0` alone, `0x0E9` alone, and `0x0DE`–`0x0E1` across the network block's end: each refused with `TFW_ERR_PROTECTED_REGION` ("target is platform-reserved"). `0x0EA` alone: written (`0xFF` to `0x5A`), read back `0x5A`, and restored to `0xFF` |
| — | `0x000`–`0x0FF` read again after the suite and the writes | Equal to the first read, byte for byte |
| 4 | TCP between the lane's container and the board | **Not run as specified.** The container has `eth0` at `172.17.0.2/16`, a default route via the Docker bridge at `172.17.0.1`, and loopback; it has no address or route on the board's network. A connect to `198.51.100.22:7020` with a 5 s timeout timed out unanswered. The device also holds the documentation values above, not the bench network's. Reaching the board's network is Infrastructure's |
| 5–7 | Attestation over TCP, the off-allow-list refusal, the identity after a reset | **Not run.** Items 5 and 6 need item 4. Item 7's evidence still needs `tfw-boot` or the element tier |

## The Mega's network block, from the operator's configuration, 2026-10-16

The same Mega, over serial on `/dev/ttyACM0` only, with the application it
held and the bench network's addresses from the operator's configuration.
Nothing was flashed.

| # | What ran | Result |
|---|---|---|
| 3 | The network block read over serial, then written with `WriteNetConfig`: the bench network's addresses from the operator's configuration (static, /24, no gateway; the endpoint is the control plane on that network, port 7020), `tfw.DerivedMAC` of the serial | **Pass.** Before the write it held the documentation values of item 3 of [the 10-15 second pass](#the-mega-run-2026-10-15), with that pass's MAC. Read back after the write, the 32 bytes equal `MarshalBinary`'s, the MAC unchanged. After the reset a port open causes, they read back the same. [The run on `5f62610`](#the-mega-on-perf55-2560-ram-5f62610-2026-10-16) later found the block still holding these values |
| 4 | From the lane's container, a TCP connect to the device's bench address, port 7020, with a 5 s timeout, retried | **Fail.** It timed out unanswered, as the connect to the documentation address did [on `9ee1db3`](#the-mega-on-main-9ee1db3-2026-10-16). Whether the container has a route to the bench network was not established |

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

## The Mega on perf/55-2560-ram 5f62610, 2026-10-16

The same Mega and W5100 shield, on `/dev/ttyACM0`, through its stock
STK500v2 bootloader only: no ISP, no erase, no fuse or lock access. The branch
moves the command dispatch table, the auth domain and the unmeasured label to
PROGMEM, read back through `tfw_pgm_copy`; attest's FWHash fallback reuses the
handoff buffer; and the size gate counts `.data` as RAM. The toolchain is
[the probe's](../development/wiznet-probe.md#toolchain), unpacked under `/tmp`,
behind an `avrdude` wrapper that refused `-e`, `usbasp` and any fuse or lock
argument.

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=soft ETH=1` (W5100) at `5f62610` | 17532 / 797 bytes | `4d6bcd813d29d79dad8ff6180d8d3cb26da5143a0e1297d83ad8459444653670` |

The RAM figure now includes `.data`, which the gate at `9ee1db3` left out.

| # | What ran | Result |
|---|---|---|
| 1 | The image built, size-checked, and flashed with `make flash` (`avrdude -c wiring … -D`) | Pass. avrdude wrote and verified 17532 bytes |
| 2 | The platform block and network block, read back over serial with `tfwctl eeprom-read`, `0x000`–`0x0FF`, eight bytes a command | **Present; not re-provisioned.** The 192-byte platform block equals, byte for byte, `tfwprov --bake-eeprom --serial 54465700000000000000000000002560`, whose `.eep` has the 10-15 SHA-256 above. The network block at `0x0C0` is valid and holds the operator's configuration, no longer the documentation values, with MAC `5a:ed:09:10:bf:59`. `0x0E0`–`0x0FF` read `0xFF` |
| 3 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`: the whole cross-implementation suite, over serial, after `make tfwsim tfwsim-app-example` | Pass. Every command went through the PROGMEM dispatch table. `TestAgainstFirmwareUnknownCommandIsAnswered`, also run alone with `HW_TEST_RUN`, passed: command `0x7E` answers status 4, "unknown command" |
| 4 | One `ATTEST` round, `tfwctl attest` with a 16-byte nonce | **The FWHash claim was not observable.** `ATTEST` answers "no provisioned identity", as in item 7 of the 10-15 second pass: behind a stock bootloader the software tier has no UDS, so the signature fails and no evidence leaves the device. The FWHash fallback runs before the signature, but its 32 bytes are never sent. Seeing the claim needs `tfw-boot` or the element tier |
| 5 | Host writes to the swap journal with `tfwctl eeprom-write`, each writing the bytes already there | `0x0E0` alone, `0x0E9` alone, and `0x0DE`–`0x0E1` across the network block's end: each refused with `TFW_ERR_PROTECTED_REGION` ("target is platform-reserved"), and `0x0DE`–`0x0E1` read back unchanged. `0x0EA` alone: written (`0xFF` to `0x5A`), read back `0x5A`, and restored to `0xFF` |
| — | `0x000`–`0x0FF` read again after the suite and the writes | Equal to the first read, byte for byte |

## The bench Mega on main fd7a696, 2026-10-16

The bench Mega 2560 without the W5100 shield, with EESAVE recorded then as programmed (it was not: the high fuse reads `0xD8`, [corrected 2026-10-18](../development/bench/mega2560.md#first-write-2026-10-16-eesave-item-25)), on
`/dev/ttyACM0`, through its stock STK500v2 bootloader only: no ISP, no erase,
no fuse, lock or boot-section access. Main had moved on since
[`5f62610`](#the-mega-on-perf55-2560-ram-5f62610-2026-10-16): the OTA boot
margin and the Mega's network block record (#102), and the simavr store row,
with trial, confirm and rollback (#109). The ATECC608C does not answer, so no
boot row ran. The toolchain is [the probe's](../development/wiznet-probe.md#toolchain),
unpacked under `$HOME/avr`, behind an `avrdude` wrapper that refused `-e`,
`usbasp`, any fuse or lock argument and terminal mode.

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=soft` at `fd7a696`, no Ethernet: the board has no shield | 13328 / 765 bytes | `06d5215fd6733bd452444ac17069e5b777ab7555090b0e08e5a68ede1cc70b36` |

| # | What ran | Result |
|---|---|---|
| — | `0x000`–`0x0FF` read over serial before the flash, eight bytes a command on one connection | The 192-byte platform block equals, byte for byte, `tfwprov --bake-eeprom --serial 54465700000000000000000000002560`, whose `.eep` has the 10-15 SHA-256 above. The network block at `0x0C0` is valid and holds the operator's configuration. `0x0E0`–`0x0FF` read `0xFF` |
| 1 | The image built, size-checked, and flashed with `make flash` (`avrdude -c wiring … -D`) | Pass. avrdude wrote and verified 13328 bytes |
| 2 | `0x000`–`0x0FF` read again after the flash | **Equal to the read before, byte for byte; not re-provisioned** |
| 3 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`: the whole cross-implementation suite, over serial, after `make tfwsim tfwsim-app-example` | Pass: 28 top-level tests passed, none failed, none skipped. A first run also passed, but one EEPROM read from another process opened the port while it ran, so it was run again alone; the count is the second run's |
| 4 | `OTA_INFO` (`0x6C`) on a board `tfw-boot` never armed | Status 4, "unknown command", with no payload. Not GOOD/none: `0x6C` has no entry in `include/tfw/protocol.h` or `src/core/commands.def` at `fd7a696`, so no image answers it yet. `OTA_STATUS` (`0x6B`) answers status 4 too |
| 4 | `OTA_BEGIN` (`0x68`) with a total of 256 bytes, on a board with no store | Status 4, "unknown command". As [the stager](../ota/architecture.md#the-application-stager-75) says, only an image with an external store links `src/core/ota_stage.c`; this image links `src/core/ota_stage_none.c`, so `OTA_BEGIN`, `OTA_DATA`, `OTA_END` and `OTA_STATUS` are not in its dispatch table. `OTA_STATUS` and `OTA_INFO` read after it answer status 4 again. Nothing was written |
| — | `0x000`–`0x0FF` read again after the suite and the OTA commands | Equal to the first read, byte for byte. No byte was changed, so none was restored |

## The bench Mega on train/ota75e, 2026-10-16

The same board and the same rules as [the fd7a696 pass](#the-bench-mega-on-main-fd7a696-2026-10-16):
the Mega 2560 without the shield, with EESAVE recorded then as programmed (it was not: the high fuse reads `0xD8`, [corrected 2026-10-18](../development/bench/mega2560.md#first-write-2026-10-16-eesave-item-25)), on `/dev/ttyACM0`,
through its stock STK500v2 bootloader only, with no ISP, erase, fuse, lock or
boot-section access. The toolchain is [the probe's](../development/wiznet-probe.md#toolchain)
again, behind the same refusing `avrdude` wrapper. The train adds the host's
`tfwctl ota-stage` and `tfwctl ota-status` verbs ([cli.md](../host-sdk/cli.md)).
The firmware did not change: the image hashes as fd7a696's did.

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=soft` on train/ota75e, no Ethernet | 13328 / 765 bytes | `06d5215fd6733bd452444ac17069e5b777ab7555090b0e08e5a68ede1cc70b36`, fd7a696's |

| # | What ran | Result |
|---|---|---|
| — | `0x000`–`0x0FF` read with `tfwctl eeprom-read`, eight bytes a command (the verb's limit), before the flash | Read in full, no error. Kept outside the repository |
| 1 | The image built and flashed with `make flash` | Pass. avrdude wrote and verified 13328 bytes |
| 2 | `0x000`–`0x0FF` read again after the flash | **Equal to the read before, byte for byte; not re-provisioned** |
| 3 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`, after `make tfwsim tfwsim-app-example` | Pass: 28 top-level tests passed, none failed, none skipped |
| 4 | `tfwctl /dev/ttyACM0 ota-status`, before anything else | Status 4: prints `tfwctl: unknown command`, exit 1, with `-json` too. This image links `src/core/ota_stage_none.c`, so `OTA_STATUS` is not in its dispatch table ([the stager](../ota/architecture.md#the-application-stager-75)) |
| 4 | `tfwctl /dev/ttyACM0 ota-stage <image>`, board id `0x02`: the 13328-byte application, `0xFF`-padded to 13568 bytes (53 pages), trailer v1 in the last 128 bytes, version 1, unsigned | Status 4 at `OTA_BEGIN`: prints `tfwctl: unknown command`, exit 1, nothing else printed and nothing sent after it. `ota-status` after it: status 4 again, exit 1 |
| 4 | The same with board id `0x01` | The same: status 4, exit 1; `ota-status` status 4, exit 1. The board id is never reached |
| 5 | `0x000`–`0x0FF` read again after the suite and the OTA verbs; `ping` | Equal to the first read, byte for byte; `ping` answers. The board stays on this image |

Against the docs:

- `ota-stage`: matches [cli.md](../host-sdk/cli.md). A refusal prints its
  status and exits non-zero, and nothing follows the refused `OTA_BEGIN`.
- `ota-status`: [cli.md](../host-sdk/cli.md) names only the state and the
  received count. It does not say what an image without the stager answers.
  That answer is status 4 and exit 1, which [the stager](../ota/architecture.md#the-application-stager-75)
  predicts. This is a gap in cli.md, not a contradiction.
- **Finding:** [the OTA user guide](../ota/user.md) still says "`tfwctl` has no
  OTA verb; the steps use `sdk/go`". [The secure-boot user guide](../secure-boot/user.md)
  says the same twice, under "Install the application under tfw-boot" and
  "Update the application". All three predate the train's verbs.
- **Finding:** no trailer v1 tool exists. [The secure-boot user guide](../secure-boot/user.md#install-the-application-under-tfw-boot)
  says `make firmware` writes no trailer and the tree holds no tool that stamps
  one. So both images were made by a throwaway script outside the repository,
  laid out as `simImage` in `sdk/go/cmd/tfwctl/ota_stage_test.go` lays them out.
  On this board the device never reads the trailer, so the test does not depend
  on it.

## The bench Mega on main ec135de, 2026-10-16

The same board and the same rules as [the train/ota75e pass](#the-bench-mega-on-trainota75e-2026-10-16),
with the same toolchain behind the same refusing `avrdude` wrapper. The firmware
is main's at ec135de, which adds #110: `src/core/ota_trial.c`,
`src/hal/avr/hal_sys.c` and `src/main.c` re-arm and pet the watchdog and count
the trial deadline in the application. This is the first silicon run of that
code, on a board `tfw-boot` never armed.

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=soft` on train/ota75f, no Ethernet | 13672 / 771 bytes | `1105c64dc99c5711b07c6cb48d701c1afe1c6b61c2fa8408434dd27873b897f6` |

The image links `src/core/ota_trial.c`: the build's object directory holds
`src/core/ota_trial.o` and no `ota_trial_none.o`, and the ELF's symbol table
has `tfw_ota_trial_start`. That matches [the trial's two layers](../ota/architecture.md#the-ota-round-under-simavr-75-77):
only the 2560 links `ota_trial.c`.

| # | What ran | Result |
|---|---|---|
| — | `0x000`–`0x0FF` read before the flash, eight bytes a command, through the SDK's `EEPROMRead` (the call behind `tfwctl eeprom-read`) on one connection | Read in full, no error. Kept outside the repository |
| 1 | The image built and flashed with `make flash` | Pass. avrdude wrote and verified 13672 bytes |
| 2 | `0x000`–`0x0FF` read again after the flash | **Equal to the read before, byte for byte; not re-provisioned** |
| 3 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`, after `make tfwsim tfwsim-app-example` | Pass: 28 top-level tests passed, none failed, none skipped. Counted from a second run with `GOFLAGS=-json`; the first run passed too |
| 4 | No reset by itself: one connection held open, `SYS_INFO` and `OTA_INFO` (`0x6C`) sent, 200 s idle with nothing sent, then both sent again. 200 s is over 3x the 2560's `TFW_OTA_TRIAL_TIMEOUT_S` of 60 s ([architecture.md](../ota/architecture.md#the-ota-round-under-simavr-75-77)) | **No reset.** Uptime 6 ms at the first `SYS_INFO`, 3 min 20.3 s at the second, on the same session, with no unsolicited boot event between. Reset cause reads unknown both times |
| 4 | `OTA_INFO` (`0x6C`) state | Status 4, "unknown command", no payload, both times. `0x6C` still has no entry in `src/core/commands.def` at ec135de, so no image answers it; the same answer as the fd7a696 and train/ota75e passes |
| 5 | `0x000`–`0x0FF` read again after the suite and the idle check | Equal to the first read, byte for byte. No byte was changed, so none was restored. The board stays on this image |

Against the docs:

- The watchdog: [architecture.md](../ota/architecture.md#the-ota-round-under-simavr-75-77)
  says `ota_trial.c` re-arms the watchdog only when the platform block's
  `boot_state` is `TRIAL`. This board's block is not in `TRIAL`, and the board
  ran 200 s without a reset, so the application did not arm it. That matches.
- `OTA_INFO`: docs/ota names no state for a board with no trial that an image
  on main can report, because the command is not on the wire. The answer is
  status 4, as before. Not a contradiction; the state cannot be checked on
  silicon until `OTA_INFO` lands.

## The bench Mega on main 36f6aab, 2026-10-17

The same board and the same rules as [the ec135de pass](#the-bench-mega-on-main-ec135de-2026-10-16),
with the toolchain of [the WIZnet probe](../development/wiznet-probe.md#toolchain)
unpacked again under the lane's home directory, behind an `avrdude` wrapper that
refuses `-e`, any fuse, lock or EEPROM write, and any write without `-D`. The
firmware is main's at 36f6aab. Since ec135de main has changed 13 firmware files,
among them `src/core/suit.c`, #119's `tfw_boot_hash` offset in `boot/boot_store.c`,
and #132's Ethernet rows and the 2560's RAM gate at a quarter of SRAM (2048 bytes).

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=soft ETH=0` on main 36f6aab | 13672 / 771 bytes, RAM against the 2048-byte gate | `1105c64dc99c5711b07c6cb48d701c1afe1c6b61c2fa8408434dd27873b897f6` |

Since #132, `ETH=1` is the 2560's default, so the shieldless image needs
`ETH=0` on the command line. A plain `make firmware BOARD=atmega2560 CRYPTO=soft`
builds the W5100 image instead (17928 / 803 bytes). That image was built but
not flashed. [The bench page](../development/bench/mega2560.md) names no
Ethernet shield, so this pass has no `ETH=1` row.

The `.hex` is byte for byte the one ec135de built. The 13 changed files do not
reach it: the image's object directory holds no `suit.o`, because only the element
tier carries the SUIT reader, and `boot_store.c` is `tfw-boot`'s, not the
application's. So this pass checks main's build of the image on silicon. The
changed sources themselves have still not run on this board.

| # | What ran | Result |
|---|---|---|
| — | `0x000`–`0x0FF` read before the flash with `tfwctl eeprom-read`, eight bytes a command, one invocation (and so one reset) a read | Read in full, no error. Kept outside the repository |
| 1 | The image built and flashed with `make flash` | Pass. avrdude wrote and verified 13672 bytes |
| 2 | `0x000`–`0x0FF` read again after the flash | **Equal to the read before, byte for byte; not re-provisioned** |
| 3 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`, after `make tfwsim tfwsim-app-example`, with `GOFLAGS=-json` | Pass: 28 top-level tests passed, none failed, none skipped |
| 4 | `0x000`–`0x0FF` read again after the suite | Equal to the first read, byte for byte. No byte was changed, so none was restored. The board stays on this image |

## The bench Mega on main 99fee93, 2026-10-17

The same board and the same rules as [the 36f6aab pass](#the-bench-mega-on-main-36f6aab-2026-10-17),
with the toolchain of [the WIZnet probe](../development/wiznet-probe.md#toolchain)
unpacked again under the lane's home directory, behind the same `avrdude`
wrapper. The firmware is main's at 99fee93. Since 36f6aab main has changed 31
firmware files, among them #133's PREV restore and #134's `OTA_ACTIVATE`,
`OTA_CONFIRM` and `OTA_INFO` (`0x6C`–`0x6E`).

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=soft ETH=0` on main 99fee93 | 13672 / 771 bytes, RAM against the 2048-byte gate | `bb0da8960da544c3b9fb8cbbc8a861315fb221ae9263e61d4aaad01ffb451a11` |

The sizes are 36f6aab's, but the `.hex` is not. Four of the 31 changed files
reach this image: `src/core/dispatch.c`, `src/core/commands.def`,
`src/caps/sys.c` and `src/hal/avr/hal_eeprom.c`. The OTA sources do not: the
object directory holds `ota_stage_none.o` and `ota_trial.o`, and no
`ota_activate.o`, `ota_ctl.o` or `ota_prev.o`. So this pass runs the changed
dispatch, `sys` and EEPROM code on silicon. #133's PREV restore and #134's
three OTA commands are still not on this image. [The bench page](../development/bench/mega2560.md)
names no Ethernet shield, so this pass has no `ETH=1` row.

| # | What ran | Result |
|---|---|---|
| — | `0x000`–`0x0FF` read before the flash with `tfwctl eeprom-read`, eight bytes a command, one invocation (and so one reset) a read | Read in full, no error. Kept outside the repository |
| 1 | The image built and flashed with `make flash` | Pass. avrdude wrote and verified 13672 bytes |
| 2 | `0x000`–`0x0FF` read again after the flash | **Equal to the read before, byte for byte; not re-provisioned** |
| 3 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`, after `make tfwsim tfwsim-app-example`, with `GOFLAGS=-json` | Pass: 28 top-level tests passed, none failed, none skipped |
| 4 | `0x000`–`0x0FF` read again after the suite | Equal to the first read, byte for byte. No byte was changed, so none was restored. The board stays on this image |

## The bench Mega's OTA control plane on main 99fee93, 2026-10-17

The same board, rules and toolchain as [the 99fee93 pass](#the-bench-mega-on-main-99fee93-2026-10-17),
which found that its image links none of the OTA control plane. This pass
flashes the 2560 serial image that does, sends it `OTA_INFO`, `OTA_CONFIRM` and
`OTA_ACTIVATE`, runs the suite on it, and puts the soft image back.

**The image.** `ota_ctl.c` links only where the board has `BOOT_AB` and the
image stages (`mk/firmware.mk:320`), and a serial image stages only when asked
(`mk/firmware.mk:230`). On the 2560 (`BOOT_AB := 1`, `boards/atmega2560/profile.mk:67`)
that is:

```
make firmware BOARD=atmega2560 CRYPTO=soft ETH=0 OTA_STAGE=1
```

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=soft ETH=0 OTA_STAGE=1` on main 99fee93 (`build/atmega2560/soft-stage`) | 16978 / 815 bytes, RAM against the 2048-byte gate | `ce10ffd547ffde756447be8950d72f402d224723cc74e122496f8f900e84cf3d` |

It links `ota_ctl.o`, `ota_activate.o`, `ota_stage.o`, `ota_trial.o`,
`ota_floor_eeprom.o`, `ota_prev_none.o` and the `spi_nor.o` store driver
(`OTA_STORE := spi_nor`, `boards/atmega2560/profile.mk:62`). The bench has no
SPI NOR store and no tfw-boot. Neither stops the image: `tfw_stage_init`
discards the store's init status (`src/core/ota_stage.c:129`), and the
application is flashed at `0x00000` through the stock bootloader as the soft
image is. What the bench lacks decides only which answers can be checked: no
store means no staged image, and no tfw-boot means no `TRIAL` and no handoff.

**#133's PREV restore cannot run on any 2560 image.** The 2560 falls back to
its other slot, so every 2560 stager links `ota_prev_none.c`, not `ota_prev.c`
(`mk/firmware.mk:241`). PREV is the 328P's and the 32U4's, and is not
testable on this bench board.

`tfwctl` has no `ota-info` or `ota-confirm`, so the commands went through
`sdk/go`'s `OTAInfo`, `OTAConfirm`, `OTAActivate` and `OTAStatus`, from a
program kept outside the repository. `OTA_INFO`'s reply against
[decision 8](../ota/adr-ota-image-format-and-trust.md#8-wire-commands--accepted-status-code-rbac-ethernet-proposed-ids-and-layouts):

| Field | Slot 0 | Slot 1 | Reading |
|---|---|---|---|
| `slots` | 2 | 2 | the 2560's A/B |
| `active` | 0 | 0 | slot A |
| `flags` | `0x00` | `0x00` | no fallback, no refusal, not in `TRIAL` |
| `floor` | 0 | 0 | the platform block's floor (`ota_floor_eeprom.c`); nothing has raised it |
| `state` | 0 | 0 | slot 0: the platform block's boot state, which no tfw-boot has written; slot 1: no `MARK` in a store, so the block's OTA state |
| `image_version` | 0 | 0 | no staged image |
| `measurement` | 32 zero bytes | 32 zero bytes | zeroed for slot 0; slot 1 has no `MARK` |

| # | What ran | Result |
|---|---|---|
| — | `0x000`–`0x0FF` read on the soft image with `tfwctl eeprom-read`, as before | Read in full, no error. Kept outside the repository |
| 1 | The image above built and flashed with `make flash ... OTA_STAGE=1` | Pass. avrdude wrote and verified 16978 bytes |
| 2 | `OTA_INFO` slot 0 and slot 1 | Status 0, 44 bytes each, the table above |
| 3 | `OTA_INFO` slot 2 | `TFW_ERR_RANGE` (11) |
| 4 | `OTA_CONFIRM` | `TFW_ERR_BAD_STATE` (19): the documented answer, confirm outside `TRIAL` |
| 5 | `OTA_ACTIVATE`, mode 0 | `TFW_ERR_BAD_STATE` (19): the documented answer, activate with no verified image |
| 6 | `OTA_STATUS` | Status 0, idle, 0 bytes received |
| 7 | `0x000`–`0x0FF` read after the commands | Equal to the first read, byte for byte |
| 8 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0` on this image, with `GOFLAGS=-json` | Pass: 28 top-level tests passed, none failed, none skipped |
| 9 | `0x000`–`0x0FF` read after the suite | Equal to the first read, byte for byte |
| 10 | main 99fee93's `CRYPTO=soft ETH=0` image flashed back with `make flash` | Pass. avrdude wrote and verified 13672 bytes; `caps` answers board `0x02` |
| 11 | `0x000`–`0x0FF` read after the restore | Equal to the first read, byte for byte. No byte was changed, so none was restored |

Not checked here: an `OTA_ACTIVATE` that succeeds, an `OTA_CONFIRM` in
`TRIAL`, and `OTA_INFO`'s fallback flags all need a staged image in a store
and a tfw-boot that runs the trial, and the bench has neither.

[docs/ota/api.md](../ota/api.md#not-on-the-wire) still says the three
commands are not on the wire and answer `TFW_ERR_UNKNOWN_CMD`. On this image
they are, and they answer as decision 8 says. That page is out of date.

## The bench Mega's default CRYPTO=auto image on main a3d525d, 2026-10-17

The same board, rules and toolchain as [the 99fee93 passes](#the-bench-mega-on-main-99fee93-2026-10-17).
Since #135, `CRYPTO=auto` is the 2560's default: the image links both the
element and the soft provider and picks one at startup from what it finds. No
probe of the ATECC608C was sent by hand; what is recorded is what the image's
own startup check chose. The bench has no element fitted on the I²C bus.

```
make firmware BOARD=atmega2560 CRYPTO=auto ETH=0
```

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=auto ETH=0` on main a3d525d (`build/atmega2560/auto`) | 19652 / 1029 bytes, RAM against the 2048-byte gate | `4e840f249596b1dcf5576d0fa7b6fd972972429848608c1295a350dfb3be4834` |

`make firmware BOARD=atmega2560 ETH=0`, with no `CRYPTO`, builds the same
image. It links `src/crypto/auto/provider.o`, both tiers' providers and
`atecc.o`. It does not stage (`ETH=0`, no `OTA_STAGE=1`), so it links
`ota_stage_none.o` and no `ota_ctl.o` or `ota_activate.o`.

`tfwctl caps` on this image, field by field:

| Field | Value |
|---|---|
| protocol | 2.0 |
| firmware | 0.1.0 |
| board id | `0x02` (atmega2560) |
| pins | 70 |
| sched slots | 16 |
| max payload | 64 |
| root of trust | **software**: the soft tier |
| features | gpio sched analog pwm tone pulse eeprom i2c spi onewire attest **attest-cert auth attest-csr** |
| algorithms | hmac-sha256 |

The soft image on main 99fee93 answers the same, except that its features stop
at `attest`. The auto image chose the soft tier and still lists the element
tier's `attest-cert`, `auth` and `attest-csr`.

| # | What ran | Result |
|---|---|---|
| — | `tfwctl caps` and `0x000`–`0x0FF` read on 99fee93's soft image, as before | Read in full, no error. Kept outside the repository |
| 1 | The image above flashed with `make flash BOARD=atmega2560 CRYPTO=auto ETH=0` | Pass. avrdude wrote and verified 19652 bytes |
| 2 | `tfwctl caps` | The table above: root of trust `software` |
| 3 | `OTA_INFO` slots 0, 1 and 2, `OTA_CONFIRM`, `OTA_ACTIVATE` mode 0, `OTA_STATUS`, through `sdk/go` | All `TFW_ERR_UNKNOWN_CMD` (4). The image links none of the stager or the control plane, and [docs/ota/api.md](../ota/api.md#activate-confirm-and-info) gives 4 for an image without them. Not `TFW_ERR_CRYPTO` (14): with no `ota_ctl.o` no handoff rule is reached |
| 4 | `0x000`–`0x0FF` read after the commands | Equal to the first read, byte for byte |
| 5 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`, with `GOFLAGS=-json` | **Fail: 27 top-level tests passed, 1 failed, none skipped.** `TestAgainstFirmwareAuthCommandsAreAbsentOnTheSoftTier`: `tfwsim_test.go:241: AuthChallenge err = crypto provider failed, want unknown command` |
| 6 | `0x000`–`0x0FF` read after the suite | Equal to the first read, byte for byte |
| 7 | 99fee93's soft `.hex` (`bb0da896…b451a11`) written back through the stock bootloader with `-D` | Pass. avrdude wrote and verified 13672 bytes. `caps` answers as it did before the pass |
| 8 | `0x000`–`0x0FF` read after the restore | Equal to the first read, byte for byte. No byte was changed, so none was restored |

The failure is recorded, not fixed. On the soft tier the auto image answers
`AUTH_CHALLENGE` with `TFW_ERR_CRYPTO` (the provider failed) where the suite
expects the soft image's `TFW_ERR_UNKNOWN_CMD`, and its CAPS advertises `auth`.
Which of the image or the suite's soft-tier rule is right for an auto image
that fell back to soft is open.

The note above that `docs/ota/api.md` is out of date was true at 99fee93;
a3d525d's page (#136) documents the three commands.

## The bench Mega's CRYPTO=auto image with the OTA stager on main a3d525d, 2026-10-17

The same board, rules and toolchain as [the default auto pass](#the-bench-megas-default-cryptoauto-image-on-main-a3d525d-2026-10-17).
That pass found the default image links no `ota_ctl.o`. `OTA_STAGE=1` is the
variable that links it on a serial image: a serial image stages only when asked
(`mk/firmware.mk:237`), and the control plane links where `BOOT_AB` and
`OTA_STAGE` are both 1 (`mk/firmware.mk:327`). No probe of the ATECC608C was
sent by hand, and no suite was run this pass.

```
make firmware BOARD=atmega2560 CRYPTO=auto ETH=0 OTA_STAGE=1
```

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560 CRYPTO=auto ETH=0 OTA_STAGE=1` on main a3d525d (`build/atmega2560/auto-stage`) | 23278 / 1073 bytes, RAM against the 2048-byte gate | `e46935553bf682a9d11639cca3e4a6852ae7159eb30cb2e1f7eacd74671a86a5` |

It links `ota_ctl.o`, `ota_activate.o`, `ota_stage.o`, `ota_trial.o`,
`ota_prev_none.o`, `spi_nor.o`, and all three floors: `ota_floor_auto.o`,
`ota_floor_atecc.o` and `ota_floor_eeprom.o`.

CAPS on this image: root of trust **software**; features `gpio sched analog pwm
tone pulse eeprom i2c spi onewire attest attest-cert auth attest-csr`, the same
as the default auto image.

| # | What ran | Result |
|---|---|---|
| — | `tfwctl caps` and `0x000`–`0x0FF` read on 99fee93's soft image | Read in full, no error. Kept outside the repository |
| 1 | The image above flashed with `make flash ... OTA_STAGE=1` | Pass. avrdude wrote and verified 23278 bytes |
| 2 | `tfwctl caps` | Root of trust `software`, features as above |
| 3 | `OTA_INFO` slot 0 and slot 1, through `sdk/go` | `TFW_ERR_CRYPTO` (14) for both. That is the auto floor's rule: with no handoff there is no tier to take the floor from (`src/core/ota_floor_auto.c:12`, `:34`). [docs/ota/api.md](../ota/api.md#activate-confirm-and-info) lists no 14 for `OTA_INFO` (its table, `api.md:84`, has only `TFW_ERR_RANGE`) |
| 4 | `OTA_INFO` slot 2 | `TFW_ERR_RANGE` (11), as `api.md:84` says |
| 5 | `OTA_STATUS` | Status 0, idle, 0 bytes received (`api.md:25`) |
| 6 | `OTA_ACTIVATE`, mode 0 | `TFW_ERR_BAD_STATE` (19): no image is complete, as `api.md:87` says. Not `TFW_ERR_AUTH_REQUIRED` (17): the image runs the soft tier (`api.md:86`) |
| 7 | `OTA_CONFIRM` | `TFW_ERR_BAD_STATE` (19): no trial is running and no handoff, as `api.md:88` says |
| 8 | `0x000`–`0x0FF` read after the commands | Equal to the first read, byte for byte |
| 9 | 99fee93's soft `.hex` (`bb0da896…b451a11`) written back through the stock bootloader with `-D` | Pass. avrdude wrote and verified 13672 bytes. `caps` answers exactly as before the pass |
| 10 | `0x000`–`0x0FF` read after the restore | Equal to the first read, byte for byte. No byte was changed, so none was restored |

The one answer the API page does not document is `OTA_INFO`'s
`TFW_ERR_CRYPTO` on an auto image with no handoff. The code says it on purpose;
the page's status table is short of that row.

## The bench Mega over Ethernet on main a3d525d, 2026-10-17

The same board, rules and toolchain as [the auto passes](#the-bench-megas-default-cryptoauto-image-on-main-a3d525d-2026-10-17).
Since #135 the 2560's default `CRYPTO` is `auto`, and `ETH=1` is its default
since item 48, so the line [the 36f6aab pass](#the-bench-mega-on-main-36f6aab-2026-10-17)
gave for the W5100 image, minus its `CRYPTO=`, is:

```
make firmware BOARD=atmega2560
```

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| `BOARD=atmega2560` (`CRYPTO=auto ETH=1`, W5100) on main a3d525d (`build/atmega2560/auto-eth-w5100`) | 25224 / 1093 bytes, RAM against the 2048-byte gate | `865d56c267f566baafdd9ed70cc43ef4cc267c25058ce7c31a91dd41d293365d` |

It links `w5100.o`, `w5x00.o`, `netcfg.o`, and, since an Ethernet image stages
by default, the stager and `ota_ctl.o`.

**This board has no Ethernet shield.** [The bench page](../development/bench/mega2560.md)
names none, and [the matrix](#matrix) gives the bench Mega the serial-only
row. The 36f6aab pass built the W5100 image and did not flash it; it is not an
Ethernet run.
The lane that ran this pass also has no route to the bench network. Items 3 to
7 need the link and did not run. This pass flashed the Ethernet image on the
shieldless board and ran items 1 and 2 only.

| Item | What ran | Result |
|---|---|---|
| — | `tfwctl caps` and `0x000`–`0x0FF` read on 99fee93's soft image | Read in full, no error. Kept outside the repository |
| 1 | The image above built, size-checked and flashed with `make flash BOARD=atmega2560 PORT=/dev/ttyACM0` | Pass. avrdude wrote and verified 25224 bytes |
| 2 | `tfwctl caps`, then `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0`, with `GOFLAGS=-json` | CAPS: root of trust `software`; features `gpio sched analog pwm tone pulse eeprom i2c spi onewire attest attest-cert auth attest-csr`. **Suite: 27 top-level tests passed, 1 failed, none skipped.** `TestAgainstFirmwareAuthCommandsAreAbsentOnTheSoftTier`: `tfwsim_test.go:241: AuthChallenge err = crypto provider failed, want unknown command`, the same failure as on the serial auto image |
| 3 | The Ethernet link with a static address | Not run: no shield, no route |
| 4 | TCP to `198.51.100.1` | Not run: no shield, no route |
| 5 | Attestation commands over TCP | Not run over TCP. Item 5 expects `ATTEST_CERT` and `ATTEST_CSR` to answer "unknown command" on the software tier. On this auto image CAPS lists `attest-cert`, `auth` and `attest-csr` on the software tier, and over serial `AUTH_CHALLENGE` answers `TFW_ERR_CRYPTO`, so item 5's text does not hold for an auto image as it stands. The expected text is left unchanged |
| 6 | The off-allow-list refusal over TCP | Not run: no shield, no route |
| 7 | The same identity after a reset, over TCP | Not run: no shield, no route |
| — | `0x000`–`0x0FF` read after the flash and after the suite | Equal to the first read, byte for byte |
| — | 99fee93's soft `.hex` (`bb0da896…b451a11`) written back through the stock bootloader with `-D` | Pass. avrdude wrote and verified 13672 bytes. `caps` answers exactly as before the pass |
| — | `0x000`–`0x0FF` read after the restore | Equal to the first read, byte for byte. No byte was changed, so none was restored |

## The bench Mega refuses an unauthorized OTA_ACTIVATE (#137), 2026-10-17

The same board and rules as [the stager pass](#the-bench-megas-cryptoauto-image-with-the-ota-stager-on-main-a3d525d-2026-10-17),
on the #137 train (`fix/137-activate-auth`): `OTA_ACTIVATE` and `OTA_CONFIRM`
answer `TFW_ERR_AUTH_REQUIRED` (17) unless the command just before was
`OTA_AUTH` (`src/core/ota_auth.c`). No probe of the ATECC608C was sent, and no
suite was run.

```
make firmware BOARD=atmega2560 CRYPTO=auto ETH=0 OTA_STAGE=1
```

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| The line above on the #137 train (`build/atmega2560/auto-stage`) | 23464 / 1138 bytes, RAM against the 2048-byte gate | `b70f03993728360cca62a04027289b0206cc108dd153f664dfe76097fd079065` |
| The same line on main 533a86f | 23286 / 1073 bytes | `8e0ba26dc8cf25c7bf216529bf9f504bf85dc82b7b0eb1d8ec876fcfeeb2da00` |

#137 costs this row 178 bytes of flash and 65 bytes of RAM.

`tfwctl` cannot send these cases. It refuses `ota-activate` and `ota-confirm`
without `--sig` or `--sig-file` before anything goes on the wire
(`sdk/go/cmd/tfwctl/main.go:411`, `:438`), and the Mega resets when the port
opens, which drops a staged session before a separate `ota-activate` run
starts. So a short program, kept outside the repository with its own `go.mod`
and a `replace` to `sdk/go`, ran every step below on one connection through
`sdk/go`. It staged the image `stageImage(1024, 700)` builds in
`sdk/go/tfwsim/ota_test.go:19`, with the trailer's board id set to the
2560's (0x02). The board reported no boot event during either run.

| # | What ran | Result |
|---|---|---|
| — | `tfwctl caps`, `0x000`–`0x0FF` and the flash read on 99fee93's soft image | Read in full, no error. Kept outside the repository |
| 1 | The image above flashed with `make flash ... OTA_STAGE=1` (`-D`) | Pass. avrdude wrote and verified 23464 bytes. CAPS: root of trust `software`; features `gpio sched analog pwm tone pulse eeprom i2c spi onewire attest attest-cert auth attest-csr` |
| 2 | `OTA_INFO` slot 1, before staging and after every step below | `TFW_ERR_BAD_STATE` (19) every time. Slot 1 never read PENDING |
| 3 | `StageImage`, 1024 bytes | `TFW_ERR_UNSUPPORTED` (7) at `OTA_END`; `OTA_STATUS` then reads failed, 1024 bytes received. The first run, with the simulator's board id 0x01, answered the same. The cause is below: this board has no store fitted. **No image was staged** |
| 4 | `OTA_ACTIVATE` mode 0, no `OTA_AUTH` | `TFW_ERR_AUTH_REQUIRED` (17), as `sdk/go/tfwsim/ota_test.go:127` pins |
| 5 | `OTA_AUTH` with 64 bytes of 0xA5, then `OTA_ACTIVATE` mode 0 | `OTA_AUTH`: `TFW_OK` (0); the soft tier holds the signature and does not verify it (tfw-boot does, at reset; `ota_test.go:108`). `OTA_ACTIVATE`: `TFW_ERR_BAD_STATE` (19), past the gate, because step 3 left no complete image. The case the brief asked for, a held invalid signature against a staged image, did not run |
| 6 | `OTA_CONFIRM`, no `OTA_AUTH` | `TFW_ERR_AUTH_REQUIRED` (17), as `ota_test.go:130` pins |
| 7 | 99fee93's soft `.hex` (`bb0da896…b451a11`) written back through the stock bootloader with `-D` | Pass. avrdude wrote and verified 13672 bytes. `caps` answers exactly as before the pass |
| 8 | `0x000`–`0x0FF` read after the restore | Equal to the first read, byte for byte. No byte was changed, so none was restored |

On the board, the gate refuses both unauthorized commands with 17.

**Why the stager answers 7: this row stages into an SPI NOR the bench Mega does
not have.** The 2560's `OTA_STORE` is `spi_nor` (`boards/atmega2560/profile.mk:70`),
selected on D8 (`boards/atmega2560/board_profile.h:106`). No NOR is fitted on
the board, so nothing drives MISO on that select. `tfwctl spi 8` on the factory
image returned each request byte for byte: `03` with nine zero bytes, `05 00`,
and `9F` with three zero bytes. `src/store/spi_nor.c` reads by clocking out zeros
(`:153`), so a store read returns zeros. The status read in `wait_ready` (`:82`)
therefore sees WIP clear, and every erase, program and `OTA_DATA` "succeeds"
without storing anything, which is why 1024 bytes read as received. At
`OTA_END`, `verify` reads the trailer back as zeros. Its first check, the magic
(`src/core/ota_stage.c:79`), fails against `TFIM`, as do the version and board
id after it, and `:83` answers `TFW_ERR_UNSUPPORTED`. With no store, 7 is the
right answer for this row. No code change was made.

No row on this bench can stage. `spi_nor` is the 2560's only store
driver, and it needs a NOR on D8. The Nano's row links no stager (`OTA_STAGE=0`,
[the matrix](#matrix)). So the case #137 names, a held invalid signature against
a staged image, did not run on hardware. Showing it on the board needs an SPI NOR
wired to D8 with the SPI bus on D50–D53, which is a cabling question for the
owner. Until then the host suite (`test/unit/test_ota_stage.c`) and tfwsim
(`sdk/go/tfwsim/ota_test.go:110`) are where that case runs.

## The bench Mega with its Ethernet shield on main efa1bac, 2026-10-18

The bench Mega, now with its Ethernet shield, over serial on `/dev/ttyACM0`
only (#7, #75). The toolchain is [the probe's](../development/wiznet-probe.md#toolchain),
unpacked under the lane's home directory. Nothing went over ISP. No fuse, lock,
boot-section or EEPROM byte was written, and no chip erase was sent: both
flashes went through the stock bootloader with `-D`.

```
make firmware BOARD=atmega2560 CRYPTO=soft ETH=1
```

| Image | Flash / RAM | SHA-256 of the `.hex` |
|---|---|---|
| The line above on main efa1bac (`build/atmega2560/soft-eth-w5100`, W5100) | 19208 / 901 bytes, against the 65536 / 2048-byte gates | `c79215f8e3662b84a9fd21cdea1f6a26948dd1a599921ad566abfc7c8a61e10d` |

It links `w5100.o`, `w5x00.o`, `netcfg.o`, `link_eth.o`, the stager
(`ota_stage.o`, `spi_nor.o`) and `ota_ctl.o`.

| # | What ran | Result |
|---|---|---|
| 1 | The factory-state record, read only, through the stock bootloader: `avrdude -c wiring -p atmega2560 -P /dev/ttyACM0 -b 115200 -n`, signature, fuses, lock, flash and EEPROM | **Pass**, kept outside the repository; those raw files were lost with the lane's container, and [the bench record of 2026-10-18](../development/bench/mega2560.md#bench-record-2026-10-18) re-takes them, with the full hashes. Signature `0x1E9801`, low fuse `0xFF`, extended `0xFD`, lock `0xCF`, as in [the factory record](../development/bench/mega2560.md#factory-values). The high fuse reads **`0xD8`**: the factory value, as the ISP read of [the boot-flash guard](#the-boot-flash-eesave-guard-on-the-mega-2026-10-16) found, not the `0xD0` that [the first write](../development/bench/mega2560.md#first-write-2026-10-16-eesave-item-25) recorded, which is corrected there. `EESAVE` is unprogrammed. No ISP read was made on 2026-10-18 to confirm it. The boot section hashes to `fdef04c5…742db711`, the stock bootloader's, unchanged. The application region held 15262 bytes (`576b4591…4bbda15f`), not the image the record names, and it answered no command: `ping` and `caps` timed out and it sent nothing in 6 s. So `0x000`–`0x0FF` could not be read through the application before the flash. The bootloader's EEPROM read (`aee3b31a…c5734d83`) cannot be compared with the record's ISP read: it misreads, see item 3 |
| 2 | The image above built and size-checked | **Pass**. 19208 bytes of flash, 901 of RAM |
| 3 | `make flash BOARD=atmega2560 CRYPTO=soft ETH=1` | **Pass**. avrdude wrote and verified 19208 bytes. The bootloader's 4096-byte EEPROM read gave the same file before the flash, after it and at the end of the pass. The application's `0x000`–`0x0FF` read after the flash (`ada8b459…580897e4`) differs from the bootloader's in 129 of the 256 bytes: the bootloader's EEPROM read is at fault, as [the 10-15 pass](#the-mega-run-2026-10-15) found. The application's read was the same after the suite, after the probe below and after the second flash |
| — | The shield probed with `make wiznet-probe-flash BOARD=atmega2560`, then the image above flashed back the same way | **W5100**: every byte the probe clocked back equals [the 10-15 probe's](../development/wiznet-probe.md#arduino-mega-2560-with-an-arduino-ethernet-shield-devttyacm0-2026-10-15). After the second flash `ping` answers |
| 4 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0` | **Pass**. `ok` for `sdk/go`, the `TestAgainstFirmware` suite against the board |
| 5 | Serial: `ping`, `caps`, `attest`, `eeprom-read` and a GPIO round trip | `ping`: pass. `caps`: board `0x02`, protocol 2.0, root of trust `software`, features `gpio sched analog pwm tone pulse eeprom i2c spi onewire attest`. CAPS has no transport field, so the transport is not reported. `attest`: `TFW_ERR_NOT_PROVISIONED`. The soft tier's UDS comes only from tfw-boot's handoff (`src/crypto/soft/uds_flash.c:17` returns none), and no tfw-boot is installed. `eeprom-read`: pass, `0x000`–`0x0FF`. GPIO: pass on one connection through `sdk/go`, D13 an output, written 1, 0, 1, 0, each level read back. Across separate `tfwctl` runs it fails, because every port open resets the Mega and the pin's mode with it |
| 5 | TCP: the same verbs | **Not run.** The network block at `0x0C0` holds the documentation values of [the table above](#values-the-run-writes), not the bench network's that [the 10-16 write](#the-megas-network-block-from-the-operators-configuration-2026-10-16) put there. So the device dials the documentation endpoint, which no host holds. The lane's container sits on a container bridge with no route to the bench network. A TCP connect to the board's configured address on the endpoint port timed out, and the container cannot take the endpoint's address, which a `tfw.Listener` would need (`sdk/go/tcp.go`; the device is the TCP client, and `tfwctl` has no TCP mode). It has no tool to read ARP, and neither CAPS nor `sys` reports link state. No byte of the block was rewritten |
| 6 | The OTA receive to completion with a real image (#75), over serial, one connection | **Stops at `OTA_END`, code 7 (`TFW_ERR_UNSUPPORTED`)**; see [the OTA run](#the-ota-receive-over-serial-2026-10-18) below. Over TCP: not run (item 5). Item 79's "shed OTA" is about tfw-boot's soft store and OTA rows ([the boot rows](../secure-boot/cli.md)), not this application row, which links the stager |
| 7 | Enrolment (#7) | **Not run**: three of its four prerequisites are missing (below). Nothing that enrols was sent |

| Enrolment prerequisite | State | Evidence |
|---|---|---|
| The soft DICE root as the runtime default (#130) | Met in the tree | `CRYPTO ?= auto` (`boards/atmega2560/profile.mk:56`): the element when one answers, else the soft root. But the soft root has no identity without tfw-boot: `attest` answers `TFW_ERR_NOT_PROVISIONED` (item 5) |
| A production image signed with go-xkms's image key (go-xkms#1606) | Missing | This tree holds no image key, and none was made in its place |
| tfw-boot installed | Missing | The boot section holds the stock bootloader (item 1). Installing tfw-boot needs ISP, and [the USBasp reaches only the upper 128 KB](../development/bench/mega2560.md#the-usbasp-cannot-see-the-low-128-kb) |
| A reachable go-trusted-platform test control plane at the configured endpoint | Missing | Three TCP connects to the endpoint in the network block, 5 s each, all timed out. The endpoint is the documentation address (item 5) |

### The OTA receive over serial, 2026-10-18

Run after [the bench record](../development/bench/mega2560.md#bench-record-2026-10-18),
against the same `CRYPTO=soft ETH=1` image, on `/dev/ttyACM0`, on one connection.
`OTA_ACTIVATE` was not sent.

No make target or tool in the tree writes a trailer onto a built image, so the
payload was made by hand: the 19208-byte application binary of the image above
(the bytes the flash holds), padded with `0xFF` to 19456 bytes (76 pages of
`TFW_OTA_PAGE_MAX`, 256), and a trailer v1 in its last 128 bytes as
`include/tfw/boot.h:84`–`98` lays it out: magic `TFIM`, version 1, board id
`0x02`, image flags `0x01` (development), image version 1, image length 19208,
and the SHA-256 of the 19208 bytes (`2783b1c6…5a3ec94e`). The signature is zero:
the tree holds no image key (go-xkms#1606). The stager checks no signature;
tfw-boot would refuse the image at activation.

| | |
|---|---|
| Payload | 19456 bytes, SHA-256 `0f09b13ae63e03700ec91df3c8323a196416ca6524c7f853b5afb34ea06b4298` |
| Driver | a bench program outside the repository that calls `sdk/go`'s `OTAStatus`, `OTABegin`, `OTAData` and `OTAEnd` on one `serialport` connection; `tfwctl ota-stage` sends the same frames but reports no `OTA_STATUS` on its connection |

| Step | Status | `OTA_STATUS` after it |
|---|---|---|
| `OTA_STATUS` | `TFW_OK` | idle, 0 bytes |
| `OTA_BEGIN`, 19456 | `TFW_OK` (0) | — |
| `OTA_DATA`, 325 chunks of up to 60 bytes, offsets 0 to 19440 | `TFW_OK` (0), every one | receiving, 19456 bytes |
| `OTA_END` | **`TFW_ERR_UNSUPPORTED` (7)** | failed, 19456 bytes |

**Where it stops, and why.** `OTA_END` (`src/core/ota_stage.c:210`) runs
`verify()`, which reads the trailer back from the store and returns
`TFW_ERR_UNSUPPORTED` at `src/core/ota_stage.c:84` when the magic, version or
board id it reads (`:80`) is not the trailer's. The stager's `fail()` then
sets the failed state and keeps the count (`:212`). There is no SPI NOR on D8
(`TFW_OTA_STORE_CS_PIN`, `boards/atmega2560/board_profile.h:107`), so nothing
was programmed and the read gives back the bus, not the trailer.
`OTA_BEGIN` and every `OTA_DATA` answered `TFW_OK` only because the driver
cannot tell a missing part: its busy poll sees the `WIP` bit clear
(`src/store/spi_nor.c:83`) and it writes without reading back. So the receive
went to the end of the stream, but **nothing of it is stored**: the 19456
bytes reached the stager and were lost on the bus. This is the same code 7 at
`OTA_END` as [the #137 pass](#the-bench-mega-refuses-an-unauthorized-ota_activate-137-2026-10-17).

The application's `0x000`–`0x0FF` EEPROM read after the run is unchanged:
`ada8b459…580897e4`, the same as before it.

**#75 on this board.** The wire half is shown end to end over serial: the
stager takes a whole real image, chunk by chunk, with every offset accepted and
the count right. Storing it, and so `OTA_END` verifying it and `OTA_ACTIVATE`,
needs an SPI NOR part on D8. tfw-boot taking the image needs tfw-boot in the
boot section (ISP) and an image signed with go-xkms's key (go-xkms#1606). The
store driver accepts writes with no part present; a JEDEC id read at
`OTA_BEGIN` would make the refusal come first rather than last.

**The refusal now comes first (#167).** With that read in place, the same
board, still with no part on D8, answers `OTA_BEGIN` with `TFW_ERR_NO_STORE`
(20) in about 4 ms and erases nothing. The session stays idle, so `OTA_DATA` and
`OTA_END` answer `TFW_ERR_BUSY` (8). The run, its hashes and the restore are on
[the bench page](../development/bench/mega2560.md#the-no-store-refusal-2026-10-18-167).

## The Mega on main b0eab87, 2026-10-18

The bench Mega with its Ethernet shield (W5100), over serial on `/dev/ttyACM0`
only (#7, #75), after the error-path coverage train (#166), the OTA receive run
(#168) and the norteststd train (#169, with the NOR check of #167) merged. The
toolchain is [the probe's](../development/wiznet-probe.md#toolchain), unpacked
under `/tmp` this time. Nothing went over ISP. No fuse, lock or boot-section byte
was read or written, no chip erase was sent, and every flash went through the
stock bootloader with `-D`. Still no SPI NOR part on D8.

```
make firmware BOARD=atmega2560 CRYPTO=soft ETH=1
```

| Image | Flash / RAM | SHA-256 |
|---|---|---|
| The line above on main b0eab87 (`build/atmega2560/soft-eth-w5100`, W5100), the `.hex` | 19280 / 901 bytes, against the 65536 / 2048-byte gates | `5d0b27be491663a043eb9bd3cf890fb12afc04154e7a6a8d03ed3499a96ab7e7` |
| Its binary, 19280 bytes | | `ae59be49624e0ea571979de83e3a18265a53cbb89c8046f175831cb123c0e584` |

It is 72 bytes larger than [efa1bac's](#the-bench-mega-with-its-ethernet-shield-on-main-efa1bac-2026-10-18)
19208, with the same RAM.

| # | What ran | Result |
|---|---|---|
| 1 | The factory-state record: the flash read through the stock bootloader (`avrdude -c wiring -p atmega2560 -P /dev/ttyACM0 -b 115200 -D -n -U flash:r:flash.bin:r`), and `0x000`–`0x0FF` through the application, 32 `tfwctl eeprom-read <addr> 8` runs | **Pass**: every hash is [the bench record's](../development/bench/mega2560.md#bench-record-2026-10-18). Flash, all 262144 bytes, `de2df314b01137a1c8d973104374f42f8f198f26422e5326eaccfefff0f234ca`. Boot section `fdef04c5b772b0b4cc6e8919f90036ae88217cf3f0c17c399ef09fa8742db711`. Application region, all 253952 bytes, `8bf39f46b552b9cb212f53792e6497c63f0e407e5c5a30c94d58edc9f674f9d7`. Its used 19208 bytes `2783b1c6abbc26d1fe05635415df423a5c3278b00fcaf5932ae665065a3ec94e`. EEPROM `0x000`–`0x0FF` `ada8b459a9b2e815ee599450ae4e31c54d08c8c99133196c5bcc8a1d580897e4`. The application answered `ping` |
| 2 | `make flash BOARD=atmega2560 CRYPTO=soft ETH=1` | **Pass**. avrdude wrote and verified 19280 bytes. `caps`: board `0x02`, protocol 2.0, root of trust `software`, features `gpio sched analog pwm tone pulse eeprom i2c spi onewire attest`, algorithms `hmac-sha256`. `0x000`–`0x0FF` read back over serial hashes to `ada8b459…580897e4`, unchanged: the platform block and the network block at `0x0C0` are in place, and nothing was re-provisioned |
| 3 | `make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0` | **Pass**, 28 of 28, listed below |
| 4 | Host writes into the protected region: `tfwctl eeprom-write` of one byte at `0x0E0`, one at `0x0E9`, four at `0x0DE`–`0x0E1`, one at `0x0EA`, one at `0x0F4` and one at `0x0F5`. Each refused write carried the byte already there, so a wrong acceptance would have changed nothing | On main the protected span past the platform block is `0x0E0`–`0x0F4`, 21 bytes: the swap journal at `0x0E0`–`0x0E9`, then OTA_CONFIRM's request record from `0x0EA` (#77; `boards/atmega2560/board_profile.h:130`–`132`, checked by `boot_owned`, `src/caps/storage.c:58`). `0x0F5` is the first writable byte. `0x0E0`, `0x0E9`, `0x0DE`–`0x0E1`: **`TFW_ERR_PROTECTED_REGION` (16)**, as expected. `0x0EA`: **`TFW_ERR_PROTECTED_REGION` (16)**, as the span says; the brief expected a write, from before #77. The byte reads `0xFF` before and after. `0x0F4`, the span's last byte: refused (16). `0x0F5`: **`TFW_OK`**, `0xFF` written as `0x00`, read back `0x00`, then written back and read back `0xFF`. `0x000`–`0x0FF` afterwards: `ada8b459…580897e4`, unchanged |
| 5 | The NOR check of #167: `tfwctl ota-stage <image>`, no `--activate`, three runs. `OTA_ACTIVATE` was not sent | **`TFW_ERR_NO_STORE` (20)** at `OTA_BEGIN`, every run, and `ota-status` stays `idle, 0 bytes received`. The image was the binary above, padded with `0xFF` to 19456 bytes (76 pages of 256). Unpadded, its 19280 bytes are not a whole number of pages, and `OTA_BEGIN` answers `TFW_ERR_RANGE` (11) first (`src/core/ota_stage.c:142`). The no-store refusal comes from the JEDEC ID read, whose second byte reads `0x00` or `0xFF` with no part on D8 (`tfw_store_probe`, `src/store/spi_nor.c:149`). `tfw_stage_cmd_begin` returns it at `src/core/ota_stage.c:156`, before the store is erased. `tfwctl` took about 2.05 s a run, which is mostly its port open and boot wait. It does not time the request and reply |
| 6 | Restore: the 19208 bytes the record read, written back the same way (`avrdude -v -pm2560 -cwiring -P/dev/ttyACM0 -b115200 -D -Uflash:w:app-used.bin:r`), then the flash and `0x000`–`0x0FF` read again | **Mismatch in the flash; the board was left as it is until ordered.** avrdude wrote and verified 19208 bytes, and the first 19208 bytes of the read are the record's. But the full read hashes to `8ed9ad5b497a8a3572c1f9eb291f611bf6d306184a1d4dda17dfac91394b33b9`, not `de2df314…f0f234ca`: 71 bytes differ, all in `0x4B08`–`0x4B4F`. That range holds the tail of item 2's image (19280 bytes), where the record has `0xFF`. With `-D` there is no chip erase, and avrdude filled the rest of the last 256-byte page from the device ("76 pages and 248 pad bytes"), so it kept those bytes. The application region now hashes to `3817b7cb9821051707d19471e52af6fd84beab3138252c572a9f4b9e002fd2fd`. The boot section is unchanged (`fdef04c5…742db711`). The factory image's code is in place and answers `ping`. EEPROM `0x000`–`0x0FF`: `ada8b459…580897e4`, unchanged, so nothing was written back. The fix is to write the record's bytes padded with `0xFF` to the page boundary (`0x00000`–`0x04BFF`), which 6b did |
| 6b | Restore, on order: the record's 19208 bytes padded with `0xFF` to `0x4C00`, 19456 bytes, 76 whole pages (SHA-256 `05106be13c268d0a4700be341784c3d5656c6d0c5cc84d339592614c618f33ed`, byte for byte the record's `0x00000`–`0x04BFF`), written the same way with `-D`. Then the flash and `0x000`–`0x0FF` read again, and `ping` | **Pass.** avrdude wrote and verified 19456 bytes with "0 pad bytes". The write needed the padding because without a chip erase avrdude fills a partial last page from what the flash already holds, so 6's write kept the old bytes in that page. Whole pages of `0xFF` replace them. The full read hashes to `de2df314b01137a1c8d973104374f42f8f198f26422e5326eaccfefff0f234ca`, the record's, byte for byte. Application region `8bf39f46b552b9cb212f53792e6497c63f0e407e5c5a30c94d58edc9f674f9d7`, its used 19208 bytes `2783b1c6abbc26d1fe05635415df423a5c3278b00fcaf5932ae665065a3ec94e`, boot section `fdef04c5b772b0b4cc6e8919f90036ae88217cf3f0c17c399ef09fa8742db711`. EEPROM `0x000`–`0x0FF` `ada8b459a9b2e815ee599450ae4e31c54d08c8c99133196c5bcc8a1d580897e4`, the record's, so nothing was written back. `ping` answers |

| Test, `TestAgainstFirmware…` | Result |
|---|---|
| `Ping`, `PingWithMaximumPayload`, `Caps`, `SysInfo`, `AppInfoIsAbsentWithoutAnApplication`, `UnknownCommandIsAnswered`, `AuthCommandsAreAbsentOnTheSoftTier` | Pass |
| `WrongArgumentCountIsRefused`, `SurvivesAMalformedBurstBeforeAValidRequest`, `BootEventIsDelivered`, `DefaultBootWaitConsumesTheStartupEvent`, `SustainedExchange` | Pass |
| `TimerRevertsWithoutTheHost`, `TimerActuallyReverts`, `ReservedPinsAreRefused`, `AdcOnlyPinCannotBeDriven` | Pass |
| `ReservedEEPROMIsRefused`, `ReservedEEPROMReadIsUngatedOnTheSoftTier`, `UnprovisionedDeviceRefusesToAttest` | Pass |
| `AnalogReadReturnsRawCounts`, `PWM`, `SecureElementAddressIsRefused`, `OneWireAndPulseAreReachable`, `CapsReportsTheAlgorithmsItCanPerform` | Pass |
| `PinInfo`, `PinInfoRefusesAPinTheBoardLacks`, `I2CConfigAppliesAClockWithinRange`, `SPIConfigAppliesModeClkdivAndBitOrder` | Pass |
