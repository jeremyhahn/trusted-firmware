# Device provisioning — CLI

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

`agprov` (`sdk/go/cmd/agprov`) is the only tool in this repo that drives the
ATECC608A's provisioning commands, plus one unrelated mode that bakes the
platform-reserved EEPROM block into a file. See
[`architecture.md`](architecture.md) for why the wire modes are three rather
than one, and [`engineering.md`](engineering.md) for the exact `--commit`
sequence.

## Synopsis

```
agprov [flags] <port>                                  # --dry-run / --verify / --commit
agprov --bake-eeprom --serial <hex> --out <file> [flags]  # no port, no element
```

Exactly one of `--dry-run`, `--verify`, `--commit`, `--bake-eeprom` is
required. The first three take exactly one positional argument, the serial
port; `--bake-eeprom` takes none.

## Flags

| Flag | Default | Meaning |
|---|---|---|
| `--dry-run` | `false` | print the plan; write nothing |
| `--verify` | `false` | read the part back and compare against the template; writes nothing |
| `--commit` | `false` | write the template. **Irreversible.** Requires `--yes-i-mean-it` |
| `--bake-eeprom` | `false` | write a platform-block `.eep` image. Opens no port, touches no element; its own mode, not combinable with the three above |
| `--yes-i-mean-it` | `false` | required by `--commit`; the flag that cannot be typed by accident |
| `--config` | `""` | path to the provisioning template (required for `--dry-run`/`--verify`/`--commit`) |
| `--baud` | `115200` | serial baud rate (`serialport.DefaultBaud`) |
| `--timeout` | `30s` | how long to wait for a reply |
| `--force` | `false` | **refused**, not an alias. `agprov` exits with an error naming `--yes-i-mean-it` instead of accepting it |
| `--serial` | `""` | `--bake-eeprom`: the 16-byte device serial, hex |
| `--pubkey-hash` | `""` | `--bake-eeprom`: SHA-256 of the bootloader's Ed25519 pubkey, hex. Ignored on the element tier; load-bearing on `BOOT_SIG=ed25519-donna` — omitting it there bakes an all-zero hash and prints a warning, and `ag-boot` will refuse every image until the real one is set. An explicit all-zero value is refused |
| `--out` | `""` | `--bake-eeprom`: the `.eep` file to write |

Every invocation of `--dry-run`/`--verify`/`--commit` first calls `ProvInfo`
and prints the element's revision, serial, and lock state. This is the same
call `--verify` uses and is safe on any part — it writes nothing.
`--bake-eeprom` opens no port and makes this call not at all.

## The irreversibility guard

`--commit` is the only mode that writes, and it is deliberately hard to invoke
by accident:

- it requires `--yes-i-mean-it` in the same invocation, or `agprov` refuses
  before opening the port
- `--force`, the flag typed from muscle memory on other tools, is explicitly
  rejected rather than accepted as a synonym
- the underlying `ProvLock` call refuses a zero summary CRC, so even a
  successful `--commit` cannot lock a zone without the element itself checking
  the zone's contents against the template first

Run `--commit` on a spare part before a real one. That is the documented
procedure, not a precaution.

## Verbs, in the progression they are meant to be used

### 1. `--dry-run` — plan, write nothing

```sh
agprov --dry-run --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
```

Prints every config block the template would write, the two lock steps marked
`IRREVERSIBLE`, and the key-generation and UDS-seeding steps — plus a note if
the part's config or data zone is already locked, since that changes what
`--commit` will do.

### 2. `--commit` — write, irreversible

```sh
agprov --commit --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0 --yes-i-mean-it
```

Performs the sequence in [`engineering.md`](engineering.md): write config
blocks, lock config zone, generate the DeviceID key, seed the UDS, lock data
zone. Prints the DeviceID public key at the end — record it; the provisioning
record is the authority on this device's identity, not the device itself.

### 3. `--verify` — read back and compare

```sh
agprov --verify --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
```

Confirms both zones are locked, confirms the DeviceID slot holds a non-zero
key, and reads back the public key. It cannot check the config blocks
themselves once the config zone is locked — the element does not return a
locked config zone's contents — and says so explicitly in its output rather
than reporting a weaker pass as a clean one.

## Exit behavior

Any failure — including a `--commit` attempted without `--yes-i-mean-it`, a
missing `--config`, or more than one mode flag — prints `agprov: <error>` to
stderr and exits non-zero. A `ProvInfo` failure at startup is reported with a
hint: `(is this device running a PROVISION=1 image?)`, since an unknown-command
error against a production image is the most common cause by far.

## Full sequence, including the parts outside `agprov`

`--bake-eeprom` touches a file, never a device; every other mode here only
ever touches the secure element. Fuses and lock bits are set with `avrdude`,
outside this tool, and always in this order relative to it:

```sh
# 1. Bake the platform-reserved EEPROM block and flash it, before anything else.
agprov --bake-eeprom --serial <16-byte hex> --out platform.eep
avrdude -c <prog> -p <part> -U eeprom:w:platform.eep:i

# 2. Fuses.
avrdude -c <prog> -p <part> -U lfuse:w:...:m -U hfuse:w:...:m -U efuse:w:...:m

# 3. Flash a PROVISIONING image (AG_FEATURE_PROVISION=1).
make BOARD=atmega328p CRYPTO=atecc608a PROVISION=1 firmware
make BOARD=atmega328p flash

# 4-6. agprov, as above: --dry-run, then --commit, then --verify.

# 7. Lock bits last, only after --verify passes.
avrdude -c <prog> -p <part> -U lock:w:0x0C:m

# 8. Reflash the production image before the part leaves the bench.
make BOARD=atmega328p CRYPTO=atecc608a firmware
make BOARD=atmega328p flash
```

See [`user.md`](user.md) for the task-oriented walkthrough of this sequence.
