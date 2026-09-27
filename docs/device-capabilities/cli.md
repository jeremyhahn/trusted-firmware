# Device capabilities: CLI

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
>
> Conflicts with this document are defects. Fix them here first.

`agctl` (`sdk/go/cmd/agctl/main.go`) is the reference client:
`agctl [flags] <port> <command> [args]`. It talks to one board over serial;
`ports` lists what the host can see and takes no port argument.

```sh
agctl --baud 115200 --timeout 10s --json <port> <command> [args]
```

| flag | default | does |
|---|---|---|
| `--baud` | 115200 | serial baud rate |
| `--json` | off | emit JSON instead of text |
| `--timeout` | 10s | how long to wait for a reply |

## Verbs this feature exposes today

Every command in this capability set has a verb. The GPIO and scheduler verbs
are in `sdk/go/cmd/agctl/main.go`; the rest are in
`sdk/go/cmd/agctl/peripherals.go`.

| verb | args | does |
|---|---|---|
| `mode` | `<pin> <in\|out\|pullup>` | set a pin's direction |
| `read` | `<pin>` | read the level on a pin |
| `write` | `<pin> <0\|1>` | drive a pin |
| `timer` | `<pin> <0\|1> <duration>` | drive a pin, the device reverts it after `duration` |
| `timer-cancel` | `<pin> [apply]` | cancel a pin's timer; `apply` sets the final level first |
| `timer-query` | `<pin>` | report a pin's timer state |
| `caps` | — | what the device is and what it can do |
| `pin` | `<pin>` | one pin's descriptor: port, ADC channel, PWM state |
| `sys` | — | uptime, free RAM, last reset cause, framer error counters |
| `ping` | `[text]` | echo `text` through the device |
| `reset` | `[soft\|boot]` | restart; `boot` enters the bootloader |
| `ports` | — | list serial ports visible to this host |

## Examples

```sh
agctl /dev/ttyACM0 caps
agctl /dev/ttyACM0 pin 13
agctl /dev/ttyACM0 mode 7 out
agctl /dev/ttyACM0 write 7 1
agctl /dev/ttyACM0 read 2
agctl /dev/ttyACM0 timer 7 0 5s          # drive pin 7 low for 5s, device reverts it high
agctl /dev/ttyACM0 timer-query 7
agctl /dev/ttyACM0 timer-cancel 7 apply
agctl --json /dev/ttyACM0 sys
agctl /dev/ttyACM0 ping hello
agctl /dev/ttyACM0 reset soft
agctl ports
```

Duration parsing for `timer` accepts Go's `time.ParseDuration` syntax
(`5s`, `500ms`, `1m30s`). Pin arguments are the number silkscreened on the
board, decimal only. `--json` applies to every verb; text output is the
default.

## The rule this CLI is measured against

`../host-sdk/architecture.md`: "if a command exists on the wire and is not
reachable from `agctl`, that is a gap in `agctl`."

| verb | args | does |
|---|---|---|
| `analog` | `<pin>` | one conversion, raw counts |
| `adc-ref` | `<vcc\|external\|internal>` | select the converter's reference |
| `pwm` | `<pin> <duty 0-255>` | set a duty cycle |
| `tone` | `<pin> <hz>` | square wave on the board's one tone pin |
| `notone` | `<pin>` | stop it |
| `pulse-in` | `<pin> <0\|1> <timeout>` | measure one pulse |
| `pulse-capture` | `<pin> <0\|1> <max-edges> <timeout>` | intervals the host decodes into bits |
| `eeprom-read` | `<addr> <count>` | hex out |
| `eeprom-write` | `<addr> <hex>` | refused below the reserved boundary |
| `i2c` | `<addr7> <hex\|-> <read-count>` | `-` writes nothing |
| `spi` | `<cs pin> <hex>` | full duplex; the reply is the same length |
| `1wire-reset` | `<pin>` | presence pulse |
| `1wire-write` | `<pin> <hex>` | |
| `1wire-read` | `<pin> <count>` | |

Addresses and counts accept `0x` prefixes. Payloads are hex without
separators. `--json` applies to every verb.

`sdk/go/cmd/agctl/peripherals_test.go` holds the regression: it drives every
verb against `agsim` and fails on "unknown command", which is what a verb
silently losing its dispatch would otherwise look like — the SDK method keeps
working and only the CLI loses it.
