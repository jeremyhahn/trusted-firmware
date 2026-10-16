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
