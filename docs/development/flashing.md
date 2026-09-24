# Flashing

> **Status: INFORMATIVE** · Scope: `mk/avr.mk`, `boards/*/profile.mk`
> · Last verified: 2026-09-22
>
> Nothing here runs in CI. CI touches no hardware and no USB device at all.

We write no flasher. `avrdude` is the tool; the Makefile calls it with the
board's own settings from `boards/<board>/profile.mk`.

```sh
make BOARD=atmega328p firmware
make BOARD=atmega328p flash AVRDUDE_PORT=/dev/ttyUSB0
```

`AVRDUDE_PORT` defaults to `/dev/ttyUSB0` and is the one setting you will
usually override. `AVRDUDE_PART`, `AVRDUDE_PROG` and `AVRDUDE_BAUD` come from
the profile — an Uno is `arduino` at 115200, a Nano with the old bootloader is
the same programmer at 57600, and getting that wrong looks like a dead board
rather than a wrong baud rate.

## Which image

The crypto tier is part of the build path, so the two tiers are separate
artifacts:

```
build/<board>/soft/arduino-generic.hex        no element; software tier
build/<board>/atecc608a/arduino-generic.hex   DICE-HW
```

Flashing the element image onto a board with no element is not an error: the
provider probes for the part, does not find it, reports `AG_CRYPTO_NONE` in
`CAPS` and refuses every crypto operation. That is the correct behaviour — but
it is not the same as the software tier, which has a UDS in flash and will sign
once provisioned.

## Bring-up

```sh
agctl ping /dev/ttyUSB0
agctl caps /dev/ttyUSB0      # board id, pin count, crypto tier, algorithms
agctl pin  /dev/ttyUSB0 13   # one pin's descriptor
```

Then the one check that proves the scheduler is real:

```sh
agctl timer-set /dev/ttyUSB0 13 1 5000
# now unplug the host. The LED must still go out at 5 s.
```

If it does not, the timer is being driven from the host somewhere and the safety
property this firmware exists to provide is not there.

## Fuses and lock bits

Not here. See `docs/security/fuses-and-lockbits.md` — several of those settings
are irreversible and one of them (`DWEN`) cannot be cleared over ISP once set.

## If it does not respond

| symptom | usual cause |
|---|---|
| `avrdude` cannot sync | wrong `AVRDUDE_BAUD` for that bootloader, or the port is held open by a serial monitor |
| flashes, then nothing on the wire | a watchdog reset loop — `MCUSR` must be cleared and the WDT disarmed in the first instructions, before the C runtime; `src/hal/avr/hal_sys.c` does this from `.init3` |
| garbage on the wire | the host's baud does not match `AG_UART_BAUD`, or the part is running at a different `F_CPU` than the profile says |
| `PING` works, nothing else does | you are talking to a bootloader, not the application |

## What this deliberately does not do

- **No flashing from CI, ever.** No USB, no serial port, no device. A gate that
  needs hardware plugged in fails for reasons unrelated to the change.
- **No custom flashing tool.** `avrdude` is mature, it is what everyone already
  has, and a wrapper around it would be one more thing to be wrong.
