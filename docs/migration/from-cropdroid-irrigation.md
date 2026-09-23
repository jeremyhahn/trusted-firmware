# Migrating from the CropDroid irrigation firmware

> **Status: INFORMATIVE** · Scope: `legacy/main.cpp`, `tools/irrigation`
> · Last verified: 2026-09-22
>
> The old firmware is kept at `legacy/main.cpp` as the reference for what the
> deployed hardware expects, not as something to build.

The firmware this project replaces was 820 lines of Arduino C++ doing one job:
a DHT22 on pin 9, six analog moisture sensors, a flow sensor, seven relay
channels, and a hand-rolled HTTP/JSON server over W5100 Ethernet.

Migrating it is mostly **deleting device code**. The device stops knowing what a
moisture sensor is; the host supplies the meaning.

## What moves where

| Old, on the device | New |
|---|---|
| `dht.readHumidity()` and the DHT library | `PULSE_CAPTURE` on the device; the host decodes 26 µs and 70 µs pulses into bits |
| six `analogRead()` calls with named sensors | `ANALOG_READ` on a pin number; the host owns the names and the calibration |
| flow-sensor interrupt counting | `PULSE_CAPTURE`, or a scheduled read; the host integrates |
| seven named relay channels | `PIN_MODE` + `DIGITAL_WRITE` on pin numbers |
| the auto-off timer | **stays on the device** — `TIMER_SET`. See below. |
| VPD and heat-index arithmetic | the host, in Go — which removes floating point and the soft-float library from the MCU entirely |
| `char json[375]` and `strcat` | the framed binary protocol |
| W5100 Ethernet + HTTP | USB serial to a gateway |
| `EEPROM.read(0)` for the MAC at file scope | gone; the gateway owns addressing |

## The one thing that stays in firmware

**Auto-off stays on the device**, and it is the reason `TIMER_SET` exists.

A valve told to run for ten minutes must close after ten minutes *whether or not
the host is still there*. If the host owned the timer, a crashed host, a pulled
USB cable or a rebooted gateway would leave the valve open. The scheduler runs
from the millisecond tick with no host involvement, and the plan's own bring-up
check is exactly this:

```sh
agctl timer-set /dev/ttyUSB0 13 1 5000
# unplug the host -- the pin must still go low at 5 s
```

Everything that is *convenience* moves to the host. Everything that is *safety*
stays where it cannot be disconnected from.

## Defects in the old firmware, and what replaced them

Worth reading if you maintain anything else from the same lineage.

| Defect | Consequence | What replaced it |
|---|---|---|
| `const char string_json_reboot_true PROGMEM = "..."` — missing `[]` | a single char, not a string | no PROGMEM string table; the protocol is binary |
| `param1 == ""` | compares pointers, never contents | no string parsing on the device at all |
| `strcat` into `char json[375]` with no bound | silent overrun under load | bounded writer with a **sticky** overflow flag: an overflowing write stores nothing and the error survives to the caller |
| `byte mac[] = {EEPROM.read(0), …}` at file scope | runs before `init()` | no file-scope initialisation that touches hardware |
| `itoa(millis(), …)` | truncates `unsigned long`; uptime wraps every 65 s | `SYS_INFO` returns a 32-bit uptime, little-endian |
| `loop()` blocks 2000 ms; every request pays `delay(100)` | one client starves the rest | no `delay()` anywhere; the loop is four non-blocking polls |
| 38 PROGMEM indices numbered by hand | one insertion renumbers everything | `src/core/commands.def`, one table, generated dispatch |

The bounded-writer point is the one that generalises: the replacement is not
"a bigger buffer", it is that **an overflow is a state the caller can observe**
rather than a thing that already happened.

## `tools/irrigation`

A host program that reproduces the old behaviour against the new firmware:
reads the sensors, computes VPD and heat index in Go, drives the relays, and
arms the auto-off timers on the device.

It exists to make the migration checkable rather than asserted — the old rig's
behaviour is a thing you can diff against, and the arithmetic moving to Go is a
thing you can test with ordinary Go tests instead of on a bench.

## What you will notice immediately

- **The device has no opinion about your plants.** `CAPS` reports pins, not
  channels. Naming pin 14 "moisture-3" is the host's job and belongs in the
  host's configuration, which is where you can change it without a re-flash.
- **One firmware, many rigs.** The reason to move is that the next rig with a
  different sensor layout is a configuration change rather than a fork.
- **The failure modes are different.** The old firmware failed by producing
  wrong JSON. This one fails by refusing: a bad pin is `AG_ERR_BAD_PIN`, a
  reserved pin is `AG_ERR_RESERVED_PIN`, a frame that does not parse is dropped
  and resynchronised. Refusals are visible in a way that wrong numbers are not.
