# Wire protocol — CLI

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-11-21
> Conflicts with this document are defects. Fix them here first.

No CLI surface of its own: the protocol is exercised through `tfwctl` (and,
for provisioning, `tfwprov`), documented in `../host-sdk/architecture.md`. This file
maps the wire-protocol commands this folder documents to the `tfwctl` verbs
that exercise them, so a reader does not have to cross-reference by hand.

`tfwctl [flags] <port> <command> [args]`, from `sdk/go/cmd/tfwctl/main.go`.
`<port>` is a serial port; `tcp://host:port` for an Ethernet device, where
tfwctl listens and runs the command on the first device to connect within
`--timeout` (over Ethernet the device answers only `CAPS`, `APP_INFO`, the
attestation commands and OTA staging); or `unix:<path>`, an emulator's UART on
a Unix socket, which tfwctl dials and talks to as to a serial port.

| `tfwctl` verb | wire command(s) |
|---|---|
| `ping [text]` | `PING` (0x01) |
| `caps` | `CAPS` (0x00), no pin argument |
| `pin <pin>` | `CAPS` (0x00), one-byte pin index |
| `sys` | `SYS_INFO` (0x03) |
| `app` | `APP_INFO` (0x06) |
| `reset [soft\|boot]` | `RESET` (0x02) |
| `mode <pin> <in\|out\|pullup>` | `PIN_MODE` (0x10) |
| `read <pin>` | `DIGITAL_READ` (0x11) |
| `write <pin> <0\|1>` | `DIGITAL_WRITE` (0x12) |
| `timer <pin> <0\|1> <duration>` | `TIMER_SET` (0x14) |
| `timer-cancel <pin> [apply]` | `TIMER_CANCEL` (0x15) |
| `timer-query <pin>` | `TIMER_QUERY` (0x16) |
| `i2c-config <hz>` | `I2C_CONFIG` (0x55) |
| `spi-config <mode> <clkdiv> <msb\|lsb>` | `SPI_CONFIG` (0x56) |
| `ota-stage [--activate[=now] --sig=…\|--sig-file=…] <image>` | `OTA_BEGIN` (0x68), `OTA_DATA` (0x69), `OTA_END` (0x6A), then `OTA_STATUS` (0x6B); with `--activate`, `OTA_ACTIVATE` (0x6D) mode 0 on the same connection before `OTA_STATUS`, after `OTA_AUTH` (0x6F) with the signature; with `--activate=now`, mode 1 and no frame after its reply |
| `ota-status` | `OTA_STATUS` (0x6B) |
| `ota-info <slot>` | `OTA_INFO` (0x6C), one-byte slot: 0 running, 1 staged |
| `ota-activate --sig=…\|--sig-file=… [now]` | `OTA_AUTH` (0x6F) with the row key's 64-byte signature, then `OTA_ACTIVATE` (0x6D): mode 0, taken at the next reset; `now`, mode 1, the device resets after its reply |
| `ota-confirm --sig=…\|--sig-file=…` | `OTA_AUTH` (0x6F), then `OTA_CONFIRM` (0x6E): the running trial image is confirmed, settled by tfw-boot at the next reset |
| `netcfg set --mac … --ip … --mask … --gateway … --endpoint …`, or `netcfg set --mac … --dhcp --endpoint …` | `EEPROM_WRITE` (0x41) ×4, 8 bytes each (`EEPROMChunk`), at `0x140`–`0x15F` (`NetConfigAddr`, `NetConfigLen` 32): the network block |
| `netcfg get` | `EEPROM_READ` (0x40) ×4 over the same range, decoded by `tfw.NetConfig` |
| `eeprom-read <addr> <count>` | `EEPROM_READ` (0x40) |
| `eeprom-write <addr> <hex>` | `EEPROM_WRITE` (0x41) |
| `hsm-heartbeat` | `HSM_HEARTBEAT` (0x76), no payload |
| `hsm-report <ima\|hotplug\|n>` | `HSM_REPORT` (0x77), one-byte signal: S4 (`ima`) or S5 (`hotplug`) |
| `hsm-verdict <clear\|compromised\|n>` | `HSM_VERDICT` (0x78), one-byte verdict |
| `hsm-select <index>` | `HSM_SELECT` (0x79), one-byte policy index |
| `hsm-load <envelope file>` | `HSM_LOAD` (0x7A) per chunk of up to 60 bytes (`HSMLoadChunk`), each `[total:2][offset:2][bytes]`; the device's verdict comes with the last chunk |
| `hsm-record <index>` | `HSM_RECORD_READ` (0x7C), one-byte entry index |
| `hsm-records` | `HSM_RECORD_READ` (0x7C) from index 0 up, until the device answers `StatusErrRange` |
| `auth <serial> <key.pem> [-- <verb> <args...>]` | `AUTH_CHALLENGE` (0x61), then `AUTH_RESPONSE` (0x62); a verb after `--` runs on the same connection |
| `ports` | none — lists serial ports the host can see; no device round trip |

Flags: `--baud` (default `serialport.DefaultBaud`, 115200, matching
`TFW_UART_BAUD`), `--json` (structured output instead of text), `--timeout`
(default 10s, matching `Device`'s default in `device.go`), `--boot-wait`
(default `tfw.DefaultBootWait`, 2s: how long to wait for the firmware's BOOT
event after opening the port; `0` disables the wait).

The analog, PWM, tone, pulse and bus commands have `tfwctl` verbs too, mapped
in [`../device-capabilities/cli.md`](../device-capabilities/cli.md); the
attestation verbs (`attest`, `attest-cert`, `attest-csr`) are in
[`../dice-attestation/cli.md`](../dice-attestation/cli.md) and `tfwctl -h`.
The rule is `../host-sdk/architecture.md`'s: "if a command exists on the wire
and is not reachable from `tfwctl`, that is a gap in `tfwctl`," not a gap in
the protocol.

## Examples

```sh
tfwctl /dev/ttyUSB0 caps
tfwctl /dev/ttyUSB0 pin 13
tfwctl --json /dev/ttyUSB0 sys
tfwctl /dev/ttyUSB0 write 13 1
tfwctl /dev/ttyUSB0 timer 13 0 5s
tfwctl --baud 57600 --timeout 30s /dev/ttyUSB0 ping hello
```
