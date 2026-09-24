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

Only the commands below are wired into `agctl`; everything else in this
capability set (analog, PWM, tone, pulse, EEPROM, the buses) is reachable
from the Go SDK (`api.md`, `sdk/go/commands.go`) but has no CLI verb yet —
see "Gap" below.

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
| `sys` | — | uptime, free RAM, last reset cause |
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

## Gap

`../host-sdk/architecture.md` states the rule this CLI is measured against: "if a
command exists on the wire and is not reachable from `agctl`, that is a gap
in `agctl`." As of this writing `agctl` has no verb for `ANALOG_READ`,
`ADC_REF`, `PWM_WRITE`, `TONE`/`NO_TONE`, `PULSE_IN`/`PULSE_CAPTURE`,
`EEPROM_READ`/`WRITE`, or `I2C_XFER`/`SPI_XFER`/`ONEWIRE_*`, even though the
Go SDK implements all of them (`sdk/go/commands.go`). Until those verbs land,
reach those commands from a short Go program against `sdk/go`, or from
`tools/agsim` for protocol-level testing without hardware.
