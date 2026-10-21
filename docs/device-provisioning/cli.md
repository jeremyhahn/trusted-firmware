# Device provisioning — CLI

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

`tfwprov` (`sdk/go/cmd/tfwprov`) is the only tool in this repo that drives the
ATECC608A's provisioning commands, plus one unrelated mode that bakes the
platform-reserved EEPROM block into a file. See
[`architecture.md`](architecture.md) for why the wire modes are three rather
than one, and [`engineering.md`](engineering.md) for the exact `--commit`
sequence.

## Synopsis

```
tfwprov [flags] [--record <file>] <port>                # --dry-run / --verify / --commit
tfwprov --bake-eeprom --serial <hex> --out <file> [--record <file>] [flags]  # no port, no element
tfwprov --bake-netblock --mac <mac> --ip <a.b.c.d/n> --gateway <a.b.c.d> \
        --endpoint <a.b.c.d:port> --out <file>             # no port, no element
tfwprov --bake-netblock --mac <mac> --dhcp --endpoint <a.b.c.d:port> --out <file>
```

Exactly one of `--dry-run`, `--verify`, `--commit`, or the bake modes
(`--bake-eeprom`, `--bake-netblock`, or both) is required. The first three take
exactly one positional argument, the serial port; the bake modes take none.

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
| `--force` | `false` | **refused**, not an alias. `tfwprov` exits with an error naming `--yes-i-mean-it` instead of accepting it |
| `--serial` | `""` | `--bake-eeprom`: the 16-byte device serial, hex |
| `--pubkey-hash` | `""` | `--bake-eeprom`: SHA-256 of the bootloader's Ed25519 pubkey, hex. Ignored on the element tier; load-bearing on `BOOT_SIG=ed25519-donna`, which item 79 (owner ruling 2026-10-17) takes off every row: the verifier is P-256 on every row — omitting it there bakes an all-zero hash and prints a warning, and `tfw-boot` will refuse every image until the real one is set. An explicit all-zero value is refused |
| `--operator-pubkey` | `""` | `--bake-eeprom`: 64-byte P-256 operator public key (X‖Y), hex — the `AUTH_CHALLENGE`/`AUTH_RESPONSE` session's trust anchor (issue #4), element tier only. Omitting it bakes an all-zero key and prints a warning: `AUTH_RESPONSE` will refuse every attempt until the real key is set |
| `--out` | `""` | `--bake-eeprom`/`--bake-netblock`: the `.eep` file to write |
| `--record` | `""` | the [provisioning record](#the-provisioning-record), a JSON file. `--bake-eeprom` starts it with the serial; `--commit` and `--verify` join the DeviceID point to it. Refused with `--dry-run`, and with `--bake-netblock` alone (the network block is not part of the record) |
| `--bake-netblock` | `false` | write the Ethernet network block (`sdk/go/netcfg.go`, layout in [../ethernet-transport/api.md](../ethernet-transport/api.md)) as an `.eep` image at `[0x0C0, 0x0E0)`, encoded by `NetConfig.MarshalBinary`. Opens no port. With `--bake-eeprom` too, one file of both blocks: the contiguous 224 bytes `[0x000, 0x0E0)` |
| `--mac` | `""` | `--bake-netblock`: the device's MAC, `aa:bb:cc:dd:ee:ff`; unicast |
| `--ip` | `""` | `--bake-netblock`: the device's IPv4 address, `a.b.c.d/prefix`, or `a.b.c.d` with `--mask` |
| `--mask` | `""` | `--bake-netblock`: the netmask, `a.b.c.d`, contiguous. Refused together with a `/prefix` on `--ip` |
| `--gateway` | `""` | `--bake-netblock`: the gateway, required; `0.0.0.0` for none |
| `--endpoint` | `""` | `--bake-netblock`: the host the device connects to, `a.b.c.d:port`; not `0.0.0.0`, not port 0 |
| `--dhcp` | `false` | `--bake-netblock`: set flags bit 0, so a `DHCP=1` build leases the address, mask and gateway; bytes 12–23 are written zero. Refused beside `--ip`, `--mask` or `--gateway`; `--mac` and `--endpoint` are still required |

A flag of one bake mode given without that mode is refused, never ignored:
`--serial` without `--bake-eeprom`, or `--mac` or `--dhcp` without `--bake-netblock`.

Every invocation of `--dry-run`/`--verify`/`--commit` first calls `ProvInfo`
and prints the element's revision, serial, and lock state. This is the same
call `--verify` uses and is safe on any part — it writes nothing.
`--bake-eeprom` opens no port and makes this call not at all.

## The irreversibility guard

`--commit` is the only mode that writes, and it is deliberately hard to invoke
by accident:

- it requires `--yes-i-mean-it` in the same invocation, or `tfwprov` refuses
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
tfwprov --dry-run --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
```

Prints every config block the template would write, the two lock steps marked
`IRREVERSIBLE`, and the key-generation and UDS-seeding steps — plus a note if
the part's config or data zone is already locked, since that changes what
`--commit` will do.

### 2. `--commit` — write, irreversible

```sh
tfwprov --commit --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0 --yes-i-mean-it
```

Performs the sequence in [`engineering.md`](engineering.md): write config
blocks, lock config zone, generate the DeviceID key, seed the UDS, lock data
zone. Prints the DeviceID public key at the end. With `--record`, it also joins
that key to the record; without it, record the printed key yourself. The
provisioning record is the authority on this device's identity, not the
device itself.

### 3. `--verify` — read back and compare

```sh
tfwprov --verify --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
```

Confirms both zones are locked, confirms the DeviceID slot holds a non-zero
key, and reads back the public key. It cannot check the config blocks
themselves once the config zone is locked — the element does not return a
locked config zone's contents — and says so explicitly in its output rather
than reporting a weaker pass as a clean one.

## The provisioning record

`--record <file>` writes the pair the device registry anchors enrolment in
([onboarding step 4](../device-onboarding/architecture.md)): the serial
`--bake-eeprom` wrote, joined to the DeviceID point `--commit` generated. The
two halves come from two runs, so the same file is passed to both. No member is
ours; the record is a JSON claims set of two registered claims:

| Member | Standard | Value |
|---|---|---|
| `ueid` | RFC 9711 §4.2.1 (EAT UEID), base64url as a JSON bstr per §7.2.2 | type byte `0x01` (RAND), then the 16-byte serial: the DeviceID field the device's own evidence carries |
| `cnf.jwk` | RFC 7800 §3.2 (confirmation claim), RFC 7517 (JWK) | the DeviceID public key: `kty` `EC`, `crv` `P-256`, `x`, `y`, per RFC 7518 §6.2.1 |

No RFC 7638 thumbprint is carried; the SDK computes none. The registry's hex
UEID and DER SPKI are re-encodings of these two members, and that is
go-trusted-platform's. An example, from test values only (the serial
`000102…0f` and the P-256 base point):

```json
{
  "ueid": "AQABAgMEBQYHCAkKCwwNDg8",
  "cnf": {
    "jwk": {
      "kty": "EC",
      "crv": "P-256",
      "x": "axfR8uEsQkf4vOblY6RA8ncDfYEt6zOg9KE5RdiYwpY",
      "y": "T-NC4v4af5uO5-tKfA-eFivOM1drMV7Oy7ZAaDe_UfU"
    }
  }
}
```

One record is one device, and `tfwprov` never overwrites one device's record
with another's:

- `--bake-eeprom` writes `{"ueid": …}` if the file is absent. It leaves a
  record of the same serial as it is, and refuses one of another serial before
  it writes the image.
- `--commit` refuses, before it sends the element anything, when the record is
  missing or already holds a point: a new key would not be that device's. A
  failed commit leaves the record unchanged.
- `--commit` and `--verify` add `cnf` and leave a record that already holds the
  same point as it is. They refuse a different point: that element is another
  device.
- A file that is not a record is refused, never rewritten: an unknown member, a
  `ueid` of another shape, or a `cnf.jwk` that is not a P-256 point.
- The file is replaced whole, through a rename in its own directory.

`tfwprov` cannot check on the device that the element sits on the board whose
serial the record holds. During `--commit` the platform block cannot be read
without an authenticated session, so the join rests on the operator passing
the same file to both runs.

## Exit behavior

Any failure — including a `--commit` attempted without `--yes-i-mean-it`, a
missing `--config`, or more than one mode flag — prints `tfwprov: <error>` to
stderr and exits non-zero. A `ProvInfo` failure at startup is reported with a
hint: `(is this device running a PROVISION=1 image?)`, since an unknown-command
error against a production image is the most common cause by far.

## Full sequence, including the parts outside `tfwprov`

The bake modes touch a file, never a device; every other mode here only
ever touches the secure element. Fuses and lock bits are set with `avrdude`,
outside this tool, and always in this order relative to it:

```sh
# 1. Bake the platform-reserved EEPROM block and flash it, before anything else.
#    An Ethernet (ETH=1) board takes its network block in the same image:
#    add --bake-netblock and its flags for one 224-byte file, [0x000, 0x0E0).
tfwprov --bake-eeprom --serial <16-byte hex> \
       --operator-pubkey <64-byte hex, element tier> \
       [--bake-netblock --mac <mac> --ip <a.b.c.d/n> --gateway <a.b.c.d> \
        --endpoint <a.b.c.d:port>] \
       --out platform.eep --record device.json
avrdude -c <prog> -p <part> -D -U eeprom:w:platform.eep:i
#    On a BENCH part with no ISP programmer, still running its stock serial
#    bootloader, the bench bake image writes the same tfwprov block instead
#    (never a production part; fuses and lock bits stay on ISP):
#      make platform-bake       BOARD=<board> SERIAL=<16-byte hex>
#      make platform-bake-flash BOARD=<board> SERIAL=<16-byte hex> AVRDUDE_PORT=<dev>

# 2. Fuses.
avrdude -c <prog> -p <part> -U lfuse:w:...:m -U hfuse:w:...:m -U efuse:w:...:m

# 3. Flash a PROVISIONING image (TFW_FEATURE_PROVISION=1).
make BOARD=atmega328p CRYPTO=atecc608a PROVISION=1 firmware
make BOARD=atmega328p flash

# 4-6. tfwprov, as above: --dry-run, then --commit, then --verify; pass
#      --record device.json to --commit to join the DeviceID point.

# 7. Lock bits last, only after --verify passes.
avrdude -c <prog> -p <part> -U lock:w:0x0C:m

# 8. Reflash the production image before the part leaves the bench.
make BOARD=atmega328p CRYPTO=atecc608a firmware
make BOARD=atmega328p flash
```

See [`user.md`](user.md) for the task-oriented walkthrough of this sequence.
