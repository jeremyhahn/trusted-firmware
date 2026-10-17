# Wire protocol — CLI

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

No CLI surface of its own: the protocol is exercised through `tfwctl` (and,
for provisioning, `tfwprov`), documented in `../host-sdk/architecture.md`. This file
maps the wire-protocol commands this folder documents to the `tfwctl` verbs
that exercise them, so a reader does not have to cross-reference by hand.

`tfwctl [flags] <port> <command> [args]`, from `sdk/go/cmd/tfwctl/main.go`.

| `tfwctl` verb | wire command(s) |
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
| `i2c-config <hz>` | `I2C_CONFIG` (0x55) |
| `spi-config <mode> <clkdiv> <msb\|lsb>` | `SPI_CONFIG` (0x56) |
| `ota-stage [--activate[=now]] <image>` | `OTA_BEGIN` (0x68), `OTA_DATA` (0x69), `OTA_END` (0x6A), then `OTA_STATUS` (0x6B); with `--activate`, `OTA_ACTIVATE` (0x6D) mode 0 on the same connection before `OTA_STATUS`; with `--activate=now`, mode 1 and no frame after its reply |
| `ota-status` | `OTA_STATUS` (0x6B) |
| `ota-activate [now]` | `OTA_ACTIVATE` (0x6D): mode 0, taken at the next reset; `now`, mode 1, the device resets after its reply |
| `ports` | none — lists serial ports the host can see; no device round trip |

Flags: `--baud` (default `serialport.DefaultBaud`, 115200, matching
`TFW_UART_BAUD`), `--json` (structured output instead of text), `--timeout`
(default 10s, matching `Device`'s default in `device.go`).

Analog, PWM, tone, pulse, EEPROM, bus and attestation commands (`api.md`
lists all of them) are reachable from the Go SDK
(`sdk/go/commands.go`, `sdk/go/attest.go`) but have no `tfwctl` verb yet — see
`../host-sdk/architecture.md`: "if a command exists on the wire and is not
reachable from `tfwctl`, that is a gap in `tfwctl`," not a gap in the
protocol.

## Examples

```sh
tfwctl /dev/ttyUSB0 caps
tfwctl /dev/ttyUSB0 pin 13
tfwctl --json /dev/ttyUSB0 sys
tfwctl /dev/ttyUSB0 write 13 1
tfwctl /dev/ttyUSB0 timer 13 0 5s
tfwctl --baud 57600 --timeout 30s /dev/ttyUSB0 ping hello
```
