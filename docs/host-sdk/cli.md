# Host SDK — command-line reference

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-11-12
> Conflicts with this document are defects. Fix them here first.

Every verb and flag below is grepped from the corresponding `main.go`/`main.c`,
not inferred. Build with `make sdk-build` (see `engineering.md`) or
`go run ./cmd/<name>` from `sdk/go`.

## `tfwctl` — the reference client

```
tfwctl [flags] <port> <command> [args]
tfwctl ports
```

### Ports

| `<port>` | Transport |
|---|---|
| a serial port path (`/dev/ttyACM0`, `COM3`) | opened at `-baud`; tfwctl waits `-boot-wait` for the BOOT event, then sends the command |
| `tcp://<host>:<port>` | an Ethernet device: tfwctl **listens** on that address, the device's network-block endpoint (`netcfg set --endpoint`), and runs the command on the first device to connect within `-timeout`. Over Ethernet the device answers only `caps`, `app`, the attestation verbs and OTA staging. An address that is not `host:port` exits 1 |
| `unix:<path>` | an emulator's UART on a Unix socket (QEMU `-serial unix:<path>,server=on`): tfwctl dials it within `-timeout` and talks to it as to a serial port, with `-boot-wait`. An empty path exits 1 |

### Flags

| Flag | Default | Meaning |
|---|---|---|
| `-baud` | `115200` (`serialport.DefaultBaud`) | serial baud rate |
| `-json` | `false` | emit JSON instead of text |
| `-timeout` | `10s` | how long to wait for a reply |
| `-boot-wait` | `2s` (`tfw.DefaultBootWait`) | how long to wait for the firmware's BOOT event after opening the port before sending the first command; `0` disables the wait. Raise this for an `tfw-boot` `BOOT_MEASURE=atecc608a` image |

### Commands

| Command | Args | Does |
|---|---|---|
| `ping` | `[text]` | echoes `text` through the device |
| `caps` | — | protocol/firmware version, board id, pins, sched slots, max payload, root of trust, features, algorithms |
| `pin` | `<pin>` | one pin's descriptor: flags, port.bit, ADC channel, PWM support |
| `sys` | — | uptime, free RAM, last reset cause, framer error counters |
| `app` | — | the application: name, profile, state, version, ABI, the CAPS bits it needs; an image with none answers unknown command |
| `reset` | `[soft\|boot]` | restart; `boot` enters the bootloader (default `soft`) |
| `mode` | `<pin> <in\|out\|pullup>` | configures a pin's direction |
| `read` | `<pin>` | digital read; prints `0` or `1` |
| `write` | `<pin> <0\|1>` | digital write |
| `timer` | `<pin> <0\|1> <duration>` | drives a pin to the given level; the **device** reverts it after `duration` (Go duration syntax, e.g. `5s`) |
| `timer-cancel` | `<pin> [apply]` | cancels a pin's timer; `apply` drives the pin to the level it would have reverted to |
| `timer-query` | `<pin>` | whether a timer is active, held level, remaining duration |
| `ota-stage` | `<image>`, or `--activate[=now] --sig=<128 hex digits>\|--sig-file=<path> <image>` | stages the image file through `OTA_BEGIN`, `OTA_DATA` in `OTAChunk` pieces and `OTA_END`, then prints `OTA_STATUS`'s state and received count; tfwctl does not parse the image, the device gates it, and a refusal prints its status and exits non-zero. `--activate` then sends `OTA_ACTIVATE` mode 0 on the same connection before `OTA_STATUS`; `--activate=now` sends mode 1, prints `activated; the device resets now` and sends no frame after its reply: the one way on a board that resets on open (the Mega 2560); a progress line `ota-stage: <sent>/<total> bytes` goes to stderr. `--activate` needs the row key's authorization (as `ota-activate`), sent as `OTA_AUTH` before `OTA_ACTIVATE`; without one, or a signature without `--activate`, tfwctl sends nothing and exits 1 |
| `ota-status` | | `OTA_STATUS`: the staging session's state and received count, printed as `ota-stage` prints them. A board built without a stager answers device status 4, `TFW_ERR_UNKNOWN_CMD`: tfwctl prints `tfwctl: unknown command` on stderr and exits 1 |
| `ota-activate` | `--sig=<128 hex digits>\|--sig-file=<path> [now]` | sends the row key's ES256 authorization (64 bytes r\|\|s; a file holds them raw or as hex; tfwctl signs nothing) as `OTA_AUTH`, then `OTA_ACTIVATE`: the staged, verified image is taken at the next reset (`activated; taken at the next reset`), or with `now` the device resets after its reply (`activated; the device resets now`); `--json` prints `activated` and `reset_now`. Without a signature tfwctl sends nothing and exits 1. A refusal prints `tfwctl: <error>` and exits 1; on a board that resets on open the session is gone and the device answers `TFW_ERR_BAD_STATE` |
| `ota-confirm` | `--sig=<128 hex digits>\|--sig-file=<path>` | `OTA_AUTH` with the row key's authorization (as `ota-activate`), then `OTA_CONFIRM`: confirms the running trial image, which tfw-boot settles at the next reset; prints `confirmed; settled at the next reset` (`--json`: `confirmed`). Without a signature, or with any other argument, tfwctl sends nothing and exits 1; a device refusal prints `tfwctl: <error>` and exits 1 |
| `ota-info` | `<slot>` | `OTA_INFO` for the slot (decimal or `0x` hex, 0-255): slot count, active slot, flags, floor, state, image version and the measurement in hex (`--json`: `slots`, `active`, `flags`, `floor`, `state`, `image_version`, `measurement`). A bad slot or a device refusal prints `tfwctl: <error>` and exits 1 |
| `netcfg set` | `--mac <mac> --ip <addr> --mask <addr> --gateway <addr> --endpoint <addr:port>`, or `--mac <mac> --dhcp --endpoint <addr:port>`; either with `[--serial <device serial, hex> --operator-key <PEM file>]` | on the element tier the block is written only inside an operator auth session: `--serial` and `--operator-key` (given together, or the verb exits 1) open it first in the same connection, the exchange `auth` makes; without them an element-tier refusal (`StatusErrAuthRequired`) says to give both. Writes the 32-byte network block an Ethernet build dials with, through `Device.WriteNetConfig`, then prints it (`--json`: `dhcp` (boolean), `mac`, `ip`, `mask`, `gateway`, `endpoint`). `--dhcp` sets `NetConfig.DHCP` and writes the address, mask and gateway as `0.0.0.0` for a `DHCP=1` build to lease; given with `--ip`, `--mask` or `--gateway` it is refused, exit 1, with nothing written. tfwctl only parses each flag into its `tfw.NetConfig` field; a flag left out stays zero, and the SDK's `*tfw.NetConfigError` (MAC not 6 bytes or multicast, an address not IPv4, endpoint `0.0.0.0` or port 0) is printed as `tfwctl: <error>`, exit 1, with nothing written. The device dials with the new block from its next boot or link retry |
| `netcfg get` | — | reads the block back through `Device.ReadNetConfig` and prints it as `netcfg set` does; a block the device would treat as absent (no `TFNC` magic, another version, a CRC mismatch, a zero endpoint) is the SDK's `*tfw.NetConfigError`, exit 1 |
| `analog` | `<pin>` | one ADC conversion; prints the raw count (`--json`: `pin`, `raw`) |
| `adc-ref` | `<vcc\|external\|internal>` | selects the ADC reference; prints `adc reference -> <ref>` (`--json`: `reference`) |
| `pwm` | `<pin> <duty 0-255>` | PWM output at that duty; prints `pin <n> <- duty <d>` (`--json`: `pin`, `duty`) |
| `tone` | `<pin> <hz>` | a square wave at `hz` (0-65535); prints `pin <n> <- <hz> Hz` (`--json`: `pin`, `hz`) |
| `notone` | `<pin>` | stops the pin's tone; prints `pin <n> tone off` (`--json`: `pin`) |
| `pulse-in` | `<pin> <0\|1> <timeout>` | the width of one pulse at that level, waiting up to `timeout` (Go duration, e.g. `50ms`); prints `<width> us` (`--json`: `pin`, `us`) |
| `pulse-capture` | `<pin> <0\|1> <max-edges> <timeout>` | up to `max-edges` (0-255) edge intervals from the first at that level; prints them in microseconds on one line, space separated (`--json`: `pin`, `edges_us`) |
| `eeprom-read` | `<addr> <count>` | `count` (0-255) bytes from `addr`; prints them as hex (`--json`: `addr`, `hex`). On the Mega 2560 and 32U4 element tier a read below the reserved boundary, `0x140`, needs an `auth` session |
| `eeprom-write` | `<addr> <hex bytes>` | writes the bytes at `addr`; prints `wrote <n> bytes at 0x<addr>` (`--json`: `addr`, `bytes`) |
| `i2c` | `<addr7> <hex\|-> <read-count>` | one transfer on the generic bus: writes the hex (`-` for none), then reads `read-count` bytes; prints them as hex (`--json`: `addr`, `hex`) |
| `i2c-config` | `<clock hz>` | the generic bus's clock (it boots at 100 kHz); prints `i2c clock -> <hz> Hz` (`--json`: `hz`) |
| `spi` | `<cs pin> <hex>` | one full-duplex transfer with that chip select; prints the bytes read as hex (`--json`: `cs`, `hex`) |
| `spi-config` | `<mode 0-3> <clkdiv 0-3> <msb\|lsb>` | the SPI mode, clock divider and bit order (it boots at mode 0, slowest, msb); prints them (`--json`: `mode`, `clkdiv`, `lsb_first`) |
| `1wire-reset` | `<pin>` | a 1-Wire reset; prints `presence` or `no device` (`--json`: `pin`, `present`) |
| `1wire-write` | `<pin> <hex>` | writes the bytes on the pin's 1-Wire bus; prints `wrote <n> bytes` (`--json`: `pin`, `bytes`) |
| `1wire-read` | `<pin> <count>` | reads `count` (0-255) bytes; prints them as hex (`--json`: `pin`, `hex`) |
| `attest` | `<16 hex bytes>` | runtime evidence for that nonce, which is the caller's (tfwctl never makes one); prints it as hex (`--json`: `evidence`, `len`) |
| `attest-cert` | `<32 hex bytes>` | the alias certificate for that nonce; prints the DER as hex (`--json`: `certificate`, `len`, and `app_evidence`, the 32-byte measurements claim: a running application's evidence, or the firmware measurement where none supplied any) |
| `attest-csr` | `--nonce <32 hex bytes>` | the possession signature for that challenge; prints r\|\|s as hex (`--json`: `signature`, `len`). Without `--nonce`, or with any other argument, exits 1 |
| `auth` | `<device serial, hex> <key.pem> [-- <verb> <args...>]` | element tier only: authenticates with the operator key (a PEM EC private key file, read by `externalECKeySigner`) against the device serial, opening `eeprom-read` below `0x140` and `reset boot` until the device resets; prints `authenticated` (`--json`: `authenticated`). A verb after `--` runs in the same connection, the one way on a board that resets on reopen (the Mega 2560) |
| `hsm-heartbeat` | — | `HSM_HEARTBEAT`: the A55 is alive; prints the engine's state (below) |
| `hsm-report` | `<ima\|hotplug\|n>` | `HSM_REPORT` of signal S4 (`ima`, an IMA violation), S5 (`hotplug`) or the signal byte `n`; prints the state |
| `hsm-verdict` | `<clear\|compromised\|n>` | `HSM_VERDICT`, the verifier's verdict (or the byte `n`); prints the state |
| `hsm-select` | `<index>` | `HSM_SELECT`: applies that policy (0-255) of the held set; prints the state |
| `hsm-load` | `<envelope file>` | `HSM_LOAD` of a signed policy set or script envelope, chunked; tfwctl does not parse it, the device verifies it and its verdict comes back with the last chunk; prints the state |
| `hsm-record` | `<index>` | one entry (0-255) of the A6 record (below) |
| `hsm-records` | — | the A6 record, first entry to last (below) |
| `ports` | — | lists serial ports this host can see; no device connection made |

A number argument (pin, address, count, index, signal, verdict) is decimal or
`0x` hex. A bad argument, a missing one or a device refusal prints
`tfwctl: <error>` on stderr and exits 1.

The `hsm-*` verbs that change the engine print its state as
`flags 0x<ff> (<set held, locked, wiped or none>)`, `selected <n>`,
`refused <n>` (`--json`: `flags`, `set_held`, `locked`, `wiped`, `selected`,
`refused`). `hsm-record` and `hsm-records` print `count <n>, dropped <n>` and
one line per entry: its index, `at <t>`, its kind (`signal`, `decision` or
`action`) with its byte, and `result <n>` (`--json`: `count`, `dropped`,
`entries` of `index`, `at`, `kind`, `what`, `result`).

An unsolicited boot event seen during any exchange prints a note to stderr:
the device reset underneath the command and any assumed state is stale. The
BOOT event connecting itself waits for (`-boot-wait`) is not this — it is
expected, consumed before the first command is sent, and never printed.

### Examples

```sh
tfwctl /dev/ttyUSB0 ping hello
tfwctl -json /dev/ttyUSB0 caps
tfwctl /dev/ttyUSB0 pin 13
tfwctl /dev/ttyUSB0 timer 13 1 5s
tfwctl -timeout 30s /dev/ttyUSB0 reset boot
tfwctl ports
```

## `tfwprov` — provisioning, with a guard

```
tfwprov [flags] <port>
```

Exactly one of `-dry-run`, `-verify`, `-commit` is required, and `-config` is
always required.

### Flags

| Flag | Default | Meaning |
|---|---|---|
| `-dry-run` | `false` | print the plan; write nothing |
| `-verify` | `false` | read the part back and compare against the template |
| `-commit` | `false` | write. **Irreversible.** Requires `-yes-i-mean-it` |
| `-yes-i-mean-it` | `false` | required by `-commit` |
| `-force` | `false` | **refused, not accepted** — deprecated alias for `-yes-i-mean-it`; passing it exits with an error |
| `-config` | `""` | provisioning template path (see `engineering.md` for the format — it is not YAML despite the flag's own `-h` text) |
| `-baud` | `115200` | serial baud rate |
| `-timeout` | `30s` | how long to wait for a reply |
| `-record` | `""` | the provisioning record, JSON: `-bake-eeprom` starts it with the serial as an RFC 9711 §4.2.1 `ueid`; `-commit`/`-verify` join the DeviceID point as an RFC 7800 §3.2 `cnf` JWK (RFC 7517, RFC 7518 §6.2.1). One device per file; another's is refused, never overwritten ([the provisioning record](../device-provisioning/cli.md#the-provisioning-record)) |

### Examples

```sh
tfwprov -dry-run  -config templates/atecc608a-dice.conf /dev/ttyUSB0
tfwprov -commit   -config templates/atecc608a-dice.conf /dev/ttyUSB0 -yes-i-mean-it
tfwprov -commit   -config templates/atecc608a-dice.conf -record device.json /dev/ttyUSB0 -yes-i-mean-it
tfwprov -verify   -config templates/atecc608a-dice.conf /dev/ttyUSB0
```

Full procedure, ordering, and what each zone lock means: `docs/device-provisioning/user.md`.

## `tfwsim` — the firmware, built for the host

Not a Go command; a C binary built by `make tfwsim` into `build/host/tfwsim`.
Argument parsing is literal comparisons, not a flag library.

| Invocation | Does |
|---|---|
| `tfwsim` | speaks the protocol on stdin/stdout |
| `tfwsim --pty` | the same, on a pty whose path is printed to stderr — lets a serial client open it like a real device |
| `tfwsim --netcfg-addr` | prints the EEPROM address this build reads the network block at (`TFW_NETCFG_ADDR`, e.g. `0x0140`) and exits 0; `tfwsim.StartElementEthernet` asks it before writing the block |

```sh
make tfwsim
build/host/tfwsim --pty
# in another shell, against the printed pty path:
tfwctl /dev/pts/N ping hi
```

### `tfwsim-atecc608a` — the element tier

`make tfwsim-atecc608a` (or `make tfwsim CRYPTO=atecc608a`) builds
`build/host/tfwsim-atecc608a`: the same firmware with the real ATECC608A driver
and provider, and `test/model/atecc608a.c` as the I2C bus. It takes `--pty` and
the identity options below. A start with no options is a blank, unprovisioned
part. An unknown option, a malformed value or an unreadable file exits with
status 2 and a message; the software `tfwsim` accepts none of these options.

| Option | Provides |
|---|---|
| `--eeprom FILE` | the platform EEPROM block, as Intel HEX (what `tfwprov --bake-eeprom` writes); unaddressed bytes stay erased (`0xFF`) |
| `--eeprom-bin FILE` | the same, as a raw image from address 0 (at most the mock's 1024 bytes) |
| `--provision-element` | the TEST DeviceID key in slot 0 (real P-256; private key = SHA-256 of `tfwsim-atecc608a TEST DeviceID key v1`) and a TEST UDS, seeded directly into the model |
| `--image-hash HEX64` | an tfw-boot handoff block (FWHash and a non-zero CDI) for that SHA-256, built by `tfw_boot_handoff_build` |

**Test identity only, never to be trusted**: the model does real P-256, so the
evidence verifies, but its private key is derived from a published string and
anyone can sign as this device. The binary also has the operator-signature auth
session compiled in. See
`docs/dice-attestation/user.md#produce-real-dice-evidence-with-no-hardware`.
Go tests should use `sdk/go/tfwsim` rather than launching it by hand.

### `tfwsim-prov` — the element tier as a provisioning jig image

`make tfwsim-prov` builds `build/host/tfwsim-prov`: `tfwsim-atecc608a`'s object
list plus `src/caps/provision.c` with `TFW_FEATURE_PROVISION=1`, as
`PROVISION=1` selects them for a board, and `tools/tfwsim/reboot.c`. It takes
the same options. Started without `--provision-element` its element is blank,
so `tfwprov --commit` runs the real `PROV_*` sequence against the model. A
`RESET` restarts the firmware in place with the EEPROM and the element kept,
and from that boot on `ATTEST` signs with the key `PROV_GENKEY` made. Go tests
start it with `tfwsim.StartProvisioningWith`.
