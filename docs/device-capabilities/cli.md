# Device capabilities: CLI

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
>
> Conflicts with this document are defects. Fix them here first.

`tfwctl` (`sdk/go/cmd/tfwctl/main.go`) is the reference client:
`tfwctl [flags] <port> <command> [args]`. It talks to one board over serial;
`ports` lists what the host can see and takes no port argument.

```sh
tfwctl --baud 115200 --timeout 10s --json <port> <command> [args]
```

| flag | default | does |
|---|---|---|
| `--baud` | 115200 | serial baud rate |
| `--json` | off | emit JSON instead of text |
| `--timeout` | 10s | how long to wait for a reply |

## Verbs this feature exposes today

Every command in this capability set has a verb. The GPIO and scheduler verbs
are in `sdk/go/cmd/tfwctl/main.go`; the rest are in
`sdk/go/cmd/tfwctl/peripherals.go`.

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
| `app` | — | the application: name, profile, state, version, ABI, the CAPS bits it needs |
| `ping` | `[text]` | echo `text` through the device |
| `reset` | `[soft\|boot]` | restart; `boot` enters the bootloader |
| `ports` | — | list serial ports visible to this host |

## Examples

```sh
tfwctl /dev/ttyACM0 caps
tfwctl /dev/ttyACM0 pin 13
tfwctl /dev/ttyACM0 mode 7 out
tfwctl /dev/ttyACM0 write 7 1
tfwctl /dev/ttyACM0 read 2
tfwctl /dev/ttyACM0 timer 7 0 5s          # drive pin 7 low for 5s, device reverts it high
tfwctl /dev/ttyACM0 timer-query 7
tfwctl /dev/ttyACM0 timer-cancel 7 apply
tfwctl --json /dev/ttyACM0 sys
tfwctl /dev/ttyACM0 ping hello
tfwctl /dev/ttyACM0 reset soft
tfwctl ports
```

Duration parsing for `timer` accepts Go's `time.ParseDuration` syntax
(`5s`, `500ms`, `1m30s`). Pin arguments are the number silkscreened on the
board, decimal only. `--json` applies to every verb; text output is the
default.

## The rule this CLI is measured against

`../host-sdk/architecture.md`: "if a command exists on the wire and is not
reachable from `tfwctl`, that is a gap in `tfwctl`."

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
| `netcfg set` | `--mac <mac> --ip <addr> --mask <addr> --gateway <addr> --endpoint <addr:port>` | the network block at `0x0C0`, encoded and checked by the SDK (`tfw.NetConfig`) |
| `netcfg get` | | the network block read back and decoded |
| `i2c` | `<addr7> <hex\|-> <read-count>` | `-` writes nothing |
| `i2c-config` | `<clock hz>` | reconfigure the bus clock; boots at 100 kHz |
| `spi` | `<cs pin> <hex>` | full duplex; the reply is the same length |
| `spi-config` | `<mode 0-3> <clkdiv 0-3> <msb\|lsb>` | boots at mode 0, slowest divider, MSB-first |
| `1wire-reset` | `<pin>` | presence pulse |
| `1wire-write` | `<pin> <hex>` | |
| `1wire-read` | `<pin> <count>` | |

Addresses and counts accept `0x` prefixes. Payloads are hex without
separators. `--json` applies to every verb.

`sdk/go/cmd/tfwctl/peripherals_test.go` holds the regression: it drives every
verb against `tfwsim` and fails on "unknown command", which is what a verb
silently losing its dispatch would otherwise look like — the SDK method keeps
working and only the CLI loses it.
