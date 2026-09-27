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
the device reset underneath the command and any assumed state is stale.

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

## `irrigation` — the migration example

```
irrigation [flags] <port>
```

With no `-water`, reads the rig once and prints a sample.

### Flags

| Flag | Default | Meaning |
|---|---|---|
| `-baud` | `115200` | serial baud rate |
| `-water` | `0` | open valve `N` (1-based) and let the **device** close it; `0` means "don't water" |
| `-for` | `30s` | how long the valve stays open when `-water` is set |
| `-json` | `false` | emit JSON |
| `-timeout` | `10s` | how long to wait for a reply |

### Examples

```sh
irrigation /dev/ttyUSB0
irrigation -json /dev/ttyUSB0
irrigation -water 3 -for 2m /dev/ttyUSB0
```

Rig layout (`defaultRig`) and what moved from the old firmware:
`docs/migration/from-cropdroid-irrigation.md`.

## `agsim` — the firmware, built for the host

Not a Go command; a C binary built by `make agsim` into `build/host/agsim`.
Argument parsing is a single literal comparison, not a flag library.

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
