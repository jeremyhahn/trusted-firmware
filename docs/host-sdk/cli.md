# Host SDK — command-line reference

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Every verb and flag below is grepped from the corresponding `main.go`/`main.c`,
not inferred. Build with `make sdk-build` (see `engineering.md`) or
`go run ./cmd/<name>` from `sdk/go`.

## `agctl` — the reference client

```
agctl [flags] <port> <command> [args]
agctl ports
```

### Flags

| Flag | Default | Meaning |
|---|---|---|
| `-baud` | `115200` (`serialport.DefaultBaud`) | serial baud rate |
| `-json` | `false` | emit JSON instead of text |
| `-timeout` | `10s` | how long to wait for a reply |
| `-boot-wait` | `2s` (`ag.DefaultBootWait`) | how long to wait for the firmware's BOOT event after opening the port before sending the first command; `0` disables the wait. Raise this for an `ag-boot` `BOOT_MEASURE=atecc608a` image |

### Commands

| Command | Args | Does |
|---|---|---|
| `ping` | `[text]` | echoes `text` through the device |
| `caps` | — | protocol/firmware version, board id, pins, sched slots, max payload, root of trust, features, algorithms |
| `pin` | `<pin>` | one pin's descriptor: flags, port.bit, ADC channel, PWM support |
| `sys` | — | uptime, free RAM, last reset cause, framer error counters |
| `reset` | `[soft\|boot]` | restart; `boot` enters the bootloader (default `soft`) |
| `mode` | `<pin> <in\|out\|pullup>` | configures a pin's direction |
| `read` | `<pin>` | digital read; prints `0` or `1` |
| `write` | `<pin> <0\|1>` | digital write |
| `timer` | `<pin> <0\|1> <duration>` | drives a pin to the given level; the **device** reverts it after `duration` (Go duration syntax, e.g. `5s`) |
| `timer-cancel` | `<pin> [apply]` | cancels a pin's timer; `apply` drives the pin to the level it would have reverted to |
| `timer-query` | `<pin>` | whether a timer is active, held level, remaining duration |
| `ports` | — | lists serial ports this host can see; no device connection made |

An unsolicited boot event seen during any exchange prints a note to stderr:
the device reset underneath the command and any assumed state is stale. The
BOOT event connecting itself waits for (`-boot-wait`) is not this — it is
expected, consumed before the first command is sent, and never printed.

### Examples

```sh
agctl /dev/ttyUSB0 ping hello
agctl -json /dev/ttyUSB0 caps
agctl /dev/ttyUSB0 pin 13
agctl /dev/ttyUSB0 timer 13 1 5s
agctl -timeout 30s /dev/ttyUSB0 reset boot
agctl ports
```

## `agprov` — provisioning, with a guard

```
agprov [flags] <port>
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

### Examples

```sh
agprov -dry-run  -config templates/atecc608a-dice.conf /dev/ttyUSB0
agprov -commit   -config templates/atecc608a-dice.conf /dev/ttyUSB0 -yes-i-mean-it
agprov -verify   -config templates/atecc608a-dice.conf /dev/ttyUSB0
```

Full procedure, ordering, and what each zone lock means: `docs/device-provisioning/user.md`.

## `agsim` — the firmware, built for the host

Not a Go command; a C binary built by `make agsim` into `build/host/agsim`.
Argument parsing is literal comparisons, not a flag library.

| Invocation | Does |
|---|---|
| `agsim` | speaks the protocol on stdin/stdout |
| `agsim --pty` | the same, on a pty whose path is printed to stderr — lets a serial client open it like a real device |

```sh
make agsim
build/host/agsim --pty
# in another shell, against the printed pty path:
agctl /dev/pts/N ping hi
```

### `agsim-atecc608a` — the element tier

`make agsim-atecc608a` (or `make agsim CRYPTO=atecc608a`) builds
`build/host/agsim-atecc608a`: the same firmware with the real ATECC608A driver
and provider, and `test/model/atecc608a.c` as the I2C bus. It takes `--pty` and
the identity options below. A start with no options is a blank, unprovisioned
part. An unknown option, a malformed value or an unreadable file exits with
status 2 and a message; the software `agsim` accepts none of these options.

| Option | Provides |
|---|---|
| `--eeprom FILE` | the platform EEPROM block, as Intel HEX (what `agprov --bake-eeprom` writes); unaddressed bytes stay erased (`0xFF`) |
| `--eeprom-bin FILE` | the same, as a raw image from address 0 (at most the mock's 1024 bytes) |
| `--provision-element` | a DeviceID key in slot 0 and a UDS, seeded directly into the model |
| `--image-hash HEX64` | an ag-boot handoff block (FWHash and a non-zero CDI) for that SHA-256, built by `ag_boot_handoff_build` |

**Test identity only, never to be trusted**: the model's public key and
signatures are deterministic filler, not P-256. See
`docs/dice-attestation/user.md#produce-real-dice-evidence-with-no-hardware`.
Go tests should use `sdk/go/agsim` rather than launching it by hand.
