# Device capabilities: user guide

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
>
> Conflicts with this document are defects. Fix them here first.

Examples use `agctl`, the reference client (`../host-sdk/architecture.md`,
`cli.md`). Where a Go SDK call is shown instead, it is
given instead — see `cli.md#gap`.

## Find out what a device can do

```sh
agctl /dev/ttyACM0 caps       # board id, pin count, features, root of trust
agctl /dev/ttyACM0 pin 9      # one pin: port/bit, ADC channel, PWM state
```

Do this once per connection and act on the answer. Two boards can carry the
same MCU and different compiled feature sets — `caps` reports what this
*build* has, not what the silicon could in principle do. Four boards exist
today (328P, 2560, 32U4, ATtiny85); `../boards/adding-a-board.md` and
`../boards/attiny85.md` cover their differences, including the ATtiny85's
missing PWM, I2C, SPI and 1-Wire.

## Drive a pin

```sh
agctl /dev/ttyACM0 mode 7 out
agctl /dev/ttyACM0 write 7 1
agctl /dev/ttyACM0 read 2
```

A pin the firmware is using for itself — the UART, or a claimed
soft-peripheral — refuses `mode` and `write` but still allows `read`, so you
can observe a line without risking cutting the one the command arrived on.

## Hold a pin for a duration without babysitting it

```sh
agctl /dev/ttyACM0 timer 7 1 10s     # drive pin 7 high, device reverts it after 10s
agctl /dev/ttyACM0 timer-query 7
agctl /dev/ttyACM0 timer-cancel 7 apply
```

The deadline is owned by the **device**. If the host crashes or the link
drops while a relay is timed on, the device still reverts it —
that is the entire reason this primitive exists rather than a host-side
sleep. Re-issuing `timer` on a pin that already has one extends it instead
of consuming another slot, so a controller that polls and re-arms
continuously does not exhaust the scheduler table.

## Read a sensor

```go
raw, err := dev.AnalogRead(ctx, pin)   // 0..1023 on a 10-bit ADC
```

`raw` is the converter's own count — not a voltage, not a percentage.
Turning it into a percentage or a temperature is host work, because
the calibration depends on the sensor and its wiring, neither of which the
device knows.

## Dim an LED or drive a servo

```go
err := dev.PWMWrite(ctx, pin, 128)   // ~50% duty
err  = dev.PWMWrite(ctx, pin, 0)     // disconnects the pin, does not hold it low
```

Ask `caps`/`pin` first: a pin reporting PWM state 2 has an output-compare
unit that this build already spent on the millisecond tick — no amount of
retrying makes it drivable, a different build is the fix.

## Make a sound

```go
err := dev.Tone(ctx, pin, 440)   // A4
err  = dev.NoTone(ctx, pin)
```

Only one pin per board can do this (`caps` reports which); the ATmega32U4
has no timer for it at all and the feature is absent there.

## Time a sensor pulse or capture a bit stream

```go
width, err := dev.PulseIn(ctx, pin, true, 30*time.Millisecond)
edges, err := dev.PulseCapture(ctx, pin, true, 40, 5*time.Millisecond)
```

`PulseIn` measures one pulse — an HC-SR04's echo. `PulseCapture` records a
run of edge-to-edge intervals in microseconds and hands them back raw; a
DHT22 distinguishes a 0-bit from a 1-bit by whether a pulse ran 26 or
70 microseconds, and decoding that belongs to the host, not the firmware.

## Store small amounts of data on the device

```go
err := dev.EEPROMWrite(ctx, 0x100, []byte{1, 2, 3, 4})
data, err := dev.EEPROMRead(ctx, 0x100, 4)
```

Writes and reads are capped at 8 bytes per call — chunk larger payloads
yourself. Addresses below `0x080` belong to the platform (bootloader hash,
serial, boot counter, OTA state); a write there is refused, a read is not,
because those bytes are public by nature.

## Talk to an I2C, SPI or 1-Wire device

```go
reply, err := dev.I2CTransfer(ctx, 0x40, []byte{0x01}, 2)
reply, err  = dev.SPITransfer(ctx, csPin, []byte{0xAA, 0x00})
present, err := dev.OneWireReset(ctx, pin)
```

Both buses work out of the box: the device applies a sane default at boot
(I2C 100 kHz; SPI mode 0, slowest divider, MSB-first) without being asked.
Reconfigure either one if a device's datasheet wants something else:

```go
err = dev.SetI2CClock(ctx, 400_000)                              // Fast-mode
err = dev.ConfigureSPI(ctx, ag.SPIMode3, ag.SPIClockDiv16, false) // an SD card
```

Address `0x60` on I2C and a chip-select the board has claimed for its own
secure element or TPM are refused — that hardware is not reachable through
these generic commands, by design, so a compromised host cannot go around the
device's own root of trust. See `../dice-attestation/architecture.md` for why.

## When something is refused

The device answers with a status byte, not silence. The common ones you will
see from this feature set:

- **"that pin doesn't exist / can't do that"** — `AG_ERR_BAD_PIN` or
  `AG_ERR_UNSUPPORTED`; check `pin <n>` first.
- **"that pin is spoken for"** — `AG_ERR_RESERVED_PIN`; it is the UART or
  another peripheral this build claimed.
- **"that's off-limits"** — `AG_ERR_PROTECTED_REGION`; the low EEPROM region,
  the element's I2C address, or a reserved chip-select.
- **"try a smaller/larger value"** — `AG_ERR_RANGE`; durations, lengths and
  timeouts all have a stated ceiling in `api.md`.

Full status list and what each command can return: `engineering.md`.
