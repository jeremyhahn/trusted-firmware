# Flashing

> **Status: INFORMATIVE** · Scope: `mk/arch/avr.mk`, `boards/*/profile.mk`
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
build/<board>/soft/trusted-firmware.hex        no element; software tier
build/<board>/atecc608a/trusted-firmware.hex   DICE-HW
```

The element image drives the ATECC608C (MIKROE-6774 Click board). It talks to
the TFLXTLS part at the address and on the TWI pins in the board profile:
`0x36`, Nano A4/A5 and Mega D20/D21, at 100 kHz. Set the Click's JP1 to 5V
before powering it. The wiring and sources are in
[device provisioning](../device-provisioning/engineering.md#the-atecc608c-mikroe-6774-click-board).

Flashing the element image onto a board with no element is not an error: the
provider probes for the part, does not find it, reports `TFW_CRYPTO_NONE` in
`CAPS` and refuses every crypto operation. That is the correct behaviour — but
it is not the same as the software tier, which has a UDS in flash and will sign
once provisioned.

## Bring-up

```sh
tfwctl /dev/ttyUSB0 ping
tfwctl /dev/ttyUSB0 caps      # board id, pin count, crypto tier, algorithms
tfwctl /dev/ttyUSB0 pin 13    # one pin's descriptor
```

Flashing itself, and the `avrdude` invocation right before it, both open the
port and both reset the board via DTR the same way `tfwctl` opening it does.
That reset costs about a second before the just-flashed application starts
answering: the SDK's connect step waits for the firmware's own BOOT event
(bounded, ~2 s) before sending `PING`, so the first `tfwctl` invocation right
after a flash succeeds without a manual pause. This is a property of the SDK's
`Device`, not of `avrdude` or the bootloader — a raw terminal program (e.g.
`screen`, `minicom`) opened on the port still needs to wait out the reset
itself.

The ~2 s default assumes a bootloader that runs briefly and jumps, which the
stock Arduino bootloader does. **An `tfw-boot` image built with
`BOOT_MEASURE=atecc608a` does not**: it measures the application through the
element at up to 36 ms per 64-byte block, so bringing up anything but a small
image on that configuration needs `tfwctl -boot-wait <longer>`, sized to the
image just flashed, or the first `PING` will still be sent before the
application has started.

Then the one check that proves the scheduler is real:

```sh
tfwctl timer-set /dev/ttyUSB0 13 1 5000
# now unplug the host. The LED must still go out at 5 s.
```

If it does not, the timer is being driven from the host somewhere and the safety
property this firmware exists to provide is not there.

## Conformance on the board

Once the image is flashed, run the SDK's cross-implementation suite against it:

```sh
make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0
```

This is the suite `make sdk-test` runs against `tfwsim`. Each test resets the
board by pulsing DTR and waits for its BOOT event, so the run needs no manual
reset and works even on a port left with `-hupcl`. `BOARD` must be the board
on the port, because `CAPS` is checked against it. The target flashes nothing
and fails when the port is missing or the board is silent. See
[testing](testing.md#the-suite-on-a-real-board--make-hw-test-not-a-gate).

## Baking the platform block on a bench board

A board that still runs its stock serial bootloader and has no ISP programmer
on it gets its platform identity block from the bench bake image: a separate
image that carries the block `tfwprov --bake-eeprom` produces, writes it into
`[0x000, 0x0C0)`, prints one line and halts. On the Mega:

```sh
make platform-bake       BOARD=atmega2560 SERIAL=<16-byte hex>
make platform-bake-flash BOARD=atmega2560 SERIAL=<16-byte hex> AVRDUDE_PORT=<dev>
# then read the one line at 115200:
#   platform-bake board=atmega2560 serial=<hex> result=baked
make BOARD=atmega2560 firmware
make BOARD=atmega2560 flash AVRDUDE_PORT=<dev>
```

`result=` is `baked`, `already` (the block was there — what a second, DTR-reset
run prints), `refused` (a different valid block was there and is left alone) or
`readback-failed`. `make flash` passes `-D`, so flashing the application
afterwards keeps the block. This is a bench path only: it needs the ability to
flash arbitrary code, which already owns the part; a production part runs
`tfw-boot`, which takes signed images only, and the bake image is never
vendor-signed. See [device provisioning](../device-provisioning/user.md#task-bake-the-block-on-a-bench-board-through-its-stock-bootloader).

## Fuses and lock bits

Not here. See `docs/security/fuses-and-lockbits.md` — several of those settings
are irreversible and one of them (`DWEN`) cannot be cleared over ISP once set.

## If it does not respond

| symptom | usual cause |
|---|---|
| `avrdude` cannot sync | wrong `AVRDUDE_BAUD` for that bootloader, or the port is held open by a serial monitor |
| flashes, then nothing on the wire | a watchdog reset loop — `MCUSR` must be cleared and the WDT disarmed in the first instructions, before the C runtime; `src/hal/avr/hal_sys.c` does this from `.init3` |
| garbage on the wire | the host's baud does not match `TFW_UART_BAUD`, or the part is running at a different `F_CPU` than the profile says |
| `PING` works, nothing else does | you are talking to a bootloader, not the application |

## What this deliberately does not do

- **No flashing from CI, ever.** No USB, no serial port, no device. A gate that
  needs hardware plugged in fails for reasons unrelated to the change.
- **No custom flashing tool.** `avrdude` is mature, it is what everyone already
  has, and a wrapper around it would be one more thing to be wrong.
