# Bench board: Arduino Nano (ATmega328P, CH340)

The factory record for the bench Nano under the owner's ruling of 2026-10-16
(CA m346). It holds what the board gives without an ISP programmer, which is
not enough. The record is finished, and the first write made, only once the
USBasp is on the Nano's ICSP header ([the Nano, next](../isp-route.md#the-nano-next)).
Nothing has been written to the Nano.

| | |
|---|---|
| Board | Arduino Nano clone, USB through a CH340 (`1a86:7523`), `/dev/ttyUSB0` on the bench host |
| MCU | ATmega328P, expected from the board ([the probe](../wiznet-probe.md#arduino-nano-atmega328p-ch340-devttyusb0)). Not confirmed: the signature has not been read |

## Factory values

| | Value | Where it came from |
|---|---|---|
| Signature | not read | needs ISP or a bootloader that answers |
| Low, high, extended fuse | **not readable without ISP** | the ICSP header is not wired; the USBasp is on the Mega |
| Lock | **not readable without ISP** | the same |
| Bootloader | **none answers** | below |

## What `/dev/ttyUSB0` answers, 2026-10-16

```sh
avrdude -c arduino  -P /dev/ttyUSB0 -b 115200 -p m328p
avrdude -c arduino  -P /dev/ttyUSB0 -b 57600  -p m328p
avrdude -c stk500v2 -P /dev/ttyUSB0 -b 115200 -p m328p
```

| Programmer, baud | Answer |
|---|---|
| `arduino` (Optiboot), 115200 | `not in sync: resp=0x4d`, ten times |
| `arduino` (ATmegaBOOT), 57600 | `not in sync: resp=0x80`, ten times |
| `stk500v2`, 115200 | timeouts; stopped after 60 s |

These are the answers [the probe](../wiznet-probe.md#arduino-nano-atmega328p-ch340-devttyusb0)
recorded on 2026-10-15. `0x4d` is the `M` of the resident sketch's `macByte1`
line, and `0x80` is that 115200 output read at 57600. The sketch starts 71 ms
after a reset, too early for any Arduino bootloader to have waited for a sync.
So either no bootloader is in the boot section or `BOOTRST` is unprogrammed.
Which one it is, and the fuses, wait for the ISP read.

## The Nano on its Ethernet base, 2026-10-18

The probe ran again with the Nano seated on its Ethernet base, over
`/dev/ttyUSB0` only, with avrdude 7.1 unpacked under `$HOME/avr` as in
[the probe's toolchain](../wiznet-probe.md#toolchain). Every run passed `-n`
and no `-U`, so nothing was written. avrdude pulses DTR before each attempt.

```sh
avrdude -n -c arduino  -P /dev/ttyUSB0 -b 115200 -p m328p
avrdude -n -c arduino  -P /dev/ttyUSB0 -b 57600  -p m328p
avrdude -n -c stk500v1 -P /dev/ttyUSB0 -b 19200  -p m328p
stty -F /dev/ttyUSB0 hupcl    # then the 115200 run again
```

| Programmer, baud | Answer |
|---|---|
| `arduino`, 115200 | `resp=0x00` once, then `resp=0x4d` nine times |
| `arduino`, 57600 | `resp=0x00` once, then `resp=0x80` nine times |
| `stk500v1`, 19200 | `resp=0x00`, ten times |
| `arduino`, 115200, after `stty hupcl` | `resp=0x00` once, then `resp=0x4d` nine times |

No bootloader answered, so no signature, fuse, flash or EEPROM record could be
taken over serial. After a DTR reset the port was then held open for 5 s at
each baud. At 115200 the sketch sent 97 bytes, the first 73 ms after the reset,
beginning `Initializing dosing controller...\r\n`, then its `macByte1:` and
`MAC:` lines (the MAC is not recorded here). At 9600 the same output arrived as
6 bytes of noise (`dcf258146b92`), first at 78 ms. A sketch talking 73 ms after
reset confirms the 2026-10-16 reading: nothing in the boot section waits for a
sync. Base or no base, the serial route gives nothing more; the record needs
the USBasp on the Nano's ICSP header.

## Before the first write

The Nano slice reads these over ISP and adds them to this page before anything
is written:

```sh
avrdude -c usbasp -P usb -p m328p -U signature:r:-:h \
        -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h -U lock:r:-:h
avrdude -c usbasp -P usb -p m328p -U flash:r:nano-flash-factory.bin:r \
        -U eeprom:r:nano-eeprom-factory.bin:r
```

The 328P's 32 KB is within the USBasp's reach, unlike the Mega's upper half
([the Mega's page](mega2560.md#the-usbasp-cannot-see-the-low-128-kb)). The
flash read is the resident image, the owner's dosing-controller sketch, which
the owner has freed. It is recorded only so a restore can tell what was there.

## The bench change, 2026-10-18

The owner changed the bench at 16:42Z. A USBasp is wired to a standard Arduino
with no Ethernet shield, and a second Arduino, with the shield, is on USB alone.
This section records what the bench host saw between 16:46Z and 16:51Z. Every
probe was read only. Nothing was written to any board: no chip erase, no fuse,
no lock bit, no flash or EEPROM byte. avrdude is the Debian 7.1 in [the ISP
route](../isp-route.md#toolchain-and-sources), fetched and unpacked again for
this run with the same package hashes.

| USB | Port | What answered | Board |
|---|---|---|---|
| `2341:0042`, Arduino | `/dev/ttyACM0` | The stk500v2 bootloader (`avrdude -c wiring -b 115200 -p m2560 -n`): signature `0x1E9801`. The application (`tfwctl ping`, `caps`): board `0x02` (atmega2560), protocol 2.0, firmware 0.1.0, root of trust `software`. `tfwctl spi 10 …` is refused with `target is platform-reserved`, because D10 is the W5100's chip select in the `ETH=1` build | [The Mega](mega2560.md#the-bench-change-2026-10-18), the shield board |
| `1a86:7523`, CH340 | `/dev/ttyUSB0` | No bootloader. After a DTR reset the dosing-controller sketch sends its first byte 72 ms later at 115200, the same 97 bytes as on 2026-10-15, beginning `Initializing dosing controller...` | This Nano |
| `16c0:05dc`, USBasp, `bcdDevice` `0x0102` | `usb` | `-p m328p -n` and `-p m2560 -n`: `cannot set sck period`, then `program enable: target does not answer (0x01)`. With `-F` the signature reads `0x000000` | The ISP target: not identified, below |
| `1a86:55d3`, CH343 | `/dev/ttyACM1` | Not opened, and not passed into this run | The ESP32-P4, [its page](esp32-p4.md) |

### Whose RESET the USBasp holds

An avrdude session on the USBasp drives the target's RESET low from its
connect request until it quits. `-F` keeps the session open past the failed
program enable, and terminal mode holds it open until `quit`:

```sh
(sleep 12; echo quit) | avrdude -c usbasp -P usb -p m328p -n -F -t
(sleep 10; echo quit) | avrdude -c usbasp -P usb -p m2560 -n -F -t
```

| During the session | Answer |
|---|---|
| `/dev/ttyUSB0`, a DTR reset and 1.5 s of listening at 115200, three times, 2, 6 and 9 s in | The sketch, 72 ms after each reset, all 97 bytes each time |
| `/dev/ttyACM0`, `avrdude -c wiring -p m2560 -n`, 2.5 s in | The bootloader answered, signature `0x1E9801` |

A part held in reset runs nothing. So the USBasp's RESET line is on neither
board that is on USB. That leaves two cases, and software cannot tell them
apart:

1. **The cable is on the Nano, or the Mega, and does not reach its RESET.** A
   6-pin plug turned round fits the readings: the USBasp's MISO lands on the
   target's GND, so every bit reads 0 (`0x000000`), and its RESET lands on the
   target's VCC. A plug that is not fully seated fits them as well.
2. **The target is a third board on the USBasp alone.** It is then powered only
   if the USBasp's target-power jumper is on. An unpowered target also reads
   `0x000000`.

The owner described two Arduinos, and two are on USB. By elimination the shield
board is the Mega and the ISP board is this Nano, which puts the bench in case 1.
That is inferred, not measured.

The slow-clock jumper (JP3) is not the cause if the target is this Nano. Its
sketch runs at 115200 off a 16 MHz clock, and the USBasp's default clock,
which this firmware cannot change (`cannot set sck period`), needs a target
clock above about 1.5 MHz. JP3 matters only for a part on its 1 MHz internal
clock, such as a blank chip.

**No factory record.** Without an ISP signature the fuses, the lock byte and
the flash cannot be read, so the factory values above stay as they were. The
questions for the owner are in [the ISP route](../isp-route.md#the-bench-change-2026-10-18-the-cabling-question).

**tfw-boot on this board, once the ISP read works.** `BOOT_SUPPORTED` in
`mk/boot.mk` has one 328P row, `atmega328p:atecc608a:atecc608a:atecc608a:0:0`,
which needs the ATECC608C. The soft row is 4030 bytes over the 4 KB section and
is not a row. Under CA m435 (the element is never required) and the owner's
ruling of 2026-10-17 (the 328P is element-only), no tfw-boot build for this board
can be flashed here.
