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
