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
| `make boot-flash BOARD=atmega2560 ISP_PROG=usbasp ISP_PORT=usb` | **Did not build.** The default row (soft, ed25519-donna) does not fit the 8 KB boot section, and avrdude never ran. The supported element row builds, but needs the ATECC608C and a provisioned EEPROM |
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

The bench Mega 2560 without the W5100 shield, with EESAVE programmed, on
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
the Mega 2560 without the shield, with EESAVE programmed, on `/dev/ttyACM0`,
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
