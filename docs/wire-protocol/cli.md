# Wire protocol — CLI

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

No CLI surface of its own: the protocol is exercised through `agctl` (and,
for provisioning, `agprov`), documented in `../host-sdk/architecture.md`. This file
maps the wire-protocol commands this folder documents to the `agctl` verbs
that exercise them, so a reader does not have to cross-reference by hand.

`agctl [flags] <port> <command> [args]`, from `sdk/go/cmd/agctl/main.go`.

| `agctl` verb | wire command(s) |
|---|---|
| `ping [text]` | `PING` (0x01) |
| `caps` | `CAPS` (0x00), no pin argument |
| `pin <pin>` | `CAPS` (0x00), one-byte pin index |
| `sys` | `SYS_INFO` (0x03) |
| `reset [soft\|boot]` | `RESET` (0x02) |
| `mode <pin> <in\|out\|pullup>` | `PIN_MODE` (0x10) |
| `read <pin>` | `DIGITAL_READ` (0x11) |
| `write <pin> <0\|1>` | `DIGITAL_WRITE` (0x12) |
| `timer <pin> <0\|1> <duration>` | `TIMER_SET` (0x14) |
| `timer-cancel <pin> [apply]` | `TIMER_CANCEL` (0x15) |
| `timer-query <pin>` | `TIMER_QUERY` (0x16) |
| `ports` | none — lists serial ports the host can see; no device round trip |

Flags: `--baud` (default `serialport.DefaultBaud`, 115200, matching
`AG_UART_BAUD`), `--json` (structured output instead of text), `--timeout`
(default 10s, matching `Device`'s default in `device.go`).

Analog, PWM, tone, pulse, EEPROM, bus and attestation commands (`api.md`
lists all of them) are reachable from the Go SDK
(`sdk/go/commands.go`, `sdk/go/attest.go`) but have no `agctl` verb yet — see
`../host-sdk/architecture.md`: "if a command exists on the wire and is not
reachable from `agctl`, that is a gap in `agctl`," not a gap in the
protocol.

## Examples

```sh
agctl /dev/ttyUSB0 caps
agctl /dev/ttyUSB0 pin 13
agctl --json /dev/ttyUSB0 sys
agctl /dev/ttyUSB0 write 13 1
agctl /dev/ttyUSB0 timer 13 0 5s
agctl --baud 57600 --timeout 30s /dev/ttyUSB0 ping hello
```
