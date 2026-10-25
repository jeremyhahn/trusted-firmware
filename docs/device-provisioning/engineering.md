# Device provisioning — engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-10-21
> Conflicts with this document are defects. Fix them here first.

See [`architecture.md`](architecture.md) for component boundaries and trust
model, [`cli.md`](cli.md) for `tfwprov` usage, [`api.md`](api.md) for the wire
commands.

## The row's requirements on `main`

Row `device-provisioning` in [`../SPEC.md`](../SPEC.md), one requirement a
line, as `main` stands. A requirement on an open PR is unmet here until it
merges.

| # | Requirement | On `main` | Met by, or what is missing |
|---|---|---|---|
| R1 | A part is provisioned once: no tool redoes a step it completed | met | `sdk/go/cmd/tfwprov/steps.go` (`runCommit` refuses a locked data zone and skips a locked config zone); `tools/platform-bake/bake.c` (`already`, `refused`); the element's locks are irreversible |
| R2 | On a bench, never over the network | met | `src/proto/link_eth.c` (`tfw_link_admits` admits none of `0x70`–`0x75` over Ethernet), pinned id by id in `test/unit/test_link_eth.c`; a jig image takes the commands over serial only |
| R3 | Provisioning commands are absent from every production image | met | file selection: `mk/firmware.mk` (`PROV_SRCS` only for `PROVISION=1`) and `src/core/commands.def` (the entries only under `TFW_FEATURE_PROVISION`), held by `scripts/prov-absent-check.py` over every image's object list, run by `make scripts-test` ([below](#the-production-image-check-scriptsprov-absent-checkpy)) |
| R4 | Fuses and lock bits, the secure element and the serial are set by independent tools | met | the ISP programmer (`avrdude`, the operator's; `boot-flash` writes no fuse but the `EESAVE` bit, and that only under `BOOT_FIRST_FLASH=1` (R7), `mk/arch/avr.mk`), `tfwprov --commit` (the element), `tfwprov --bake-eeprom` and `make platform-bake` (the serial); [`architecture.md`](architecture.md), component boundaries |
| R5 | None of those tools can undo another's step | met | each refuses to redo its own (R1). `tfwprov` sends no EEPROM write in any mode: `sdk/go/cmd/tfwprov/noeeprom_test.go` (`TestTfwprovNeverSendsAnEEPROMWrite`, ids from `src/core/commands.def`), and the application's `EEPROM_WRITE` refuses the platform block anyway (`src/caps/storage.c`, `sdk/go/commands.go`). The bake image links no element, crypto-provider, bus or provisioning unit: `scripts/prov-absent-check.py` over every board's `platform-bake-srcs`, under `make scripts-test`. No code on the part sets a fuse or lock bit, and `boot-flash` refuses a chip erase without EESAVE (R7) |
| R6 | The soft root's UDS sits in EEPROM or flash under lock bits | partly | the lock bits are the operator's last `avrdude` step ([`architecture.md`](architecture.md), data flow), and no tool sets them: the write cannot be undone without a chip erase. `make lock-check` (`mk/boot.mk`, `ARCH_LOCK_CHECK` in `mk/arch/avr.mk`) reads the lock byte back and refuses to call the part provisioned unless every bit of the profile's `BOOT_LOCK_PROGRAMMED` reads programmed, naming each that does not; a board whose profile names none is refused. `scripts/test_lock_check.py` (`make scripts-test`), literal lock bytes on the ATmega2560: `0x0C` and `0x00` pass, `0xFF` and the factory `0xCF` refused with the bits named, `0x1C` refused naming `BLB11 (bit 4)` alone, the ATtiny85 (no boot section, no expected value) refused before `avrdude` runs; the ATmega328P and ATmega32U4 by literal too, `0x0C` passing, `0xFF` refused, one bit short refused naming it. Every ATmega profile names `LB1 LB2 BLB11 BLB12` (`0x33`) from its data sheet's Lock Bit Byte table and Boot Loader Lock Bits section ([`../reference/README.md`](../reference/README.md), "Board sources"; [`../security/fuses-and-lockbits.md`](../security/fuses-and-lockbits.md), "The lock byte, per board"); `BLB0` stays unprogrammed because it guards the application section, which `tfw-boot` must read to verify and write to install an update. Still partly: where the soft root's UDS is placed is with Software as an architecture question — `tfw_crypto_soft_uds` (`src/crypto/soft/uds_flash.c`) returns no UDS until `tfw-boot` writes one into the boot section, and on the ATmega328P and ATmega32U4 no soft-root `tfw-boot` fits the 4 KB boot section (`mk/boot.mk` `BOOT_SUPPORTED`), so `BLB1` guards no UDS there yet |
| R7 | EESAVE is programmed at the first boot flash | met | `make boot-flash BOOT_FIRST_FLASH=1` (`mk/arch/avr.mk`, `ARCH_BOOT_FIRST_FLASH`): it writes the profile's `BOOT_EESAVE_FUSE` with `BOOT_EESAVE_BIT` cleared and no other bit changed, worked out from the byte it read, reads it back, and refuses before the chip erase unless it holds. Without `BOOT_FIRST_FLASH=1` it refuses the erase while `EESAVE` is unprogrammed. `scripts/test_boot_flash_eesave.py` (`make scripts-test`), against a stand-in `avrdude` on every board whose profile names `EESAVE`: refused and nothing written; the one bit written, then the flash; a write that does not hold refused before the flash; an already programmed part flashed with no fuse write |
| R8 | The factory record is written together with it | met | the factory record is the board's state before its first write, the `FACTORY_*` lines `make board-restore` reads, not `tfwprov --record` (the provisioning record: the UEID and the DeviceID key, written at bake and commit). The same `BOOT_FIRST_FLASH=1` step reads every fuse byte, the lock byte, the EEPROM and the flash, and writes `build/factory-<board>-<UTC>.mk` with the two raw reads, before its one fuse write. `scripts/test_boot_flash_eesave.py` (`test_first_flash_writes_the_record_then_eesave_then_flashes`: every read before the fuse write, each byte in the record as read; `test_first_flash_refuses_when_eesave_does_not_hold`: the record stays) |

The "factory record" of owner ruling item 25 (2026-10-17) is the board's pre-write state, R8's record above: the bench rule, record before any write and restore after, is what item 25 names.

## Exported surface

Firmware (`src/caps/provision.c`, `src/caps/provision.h`, with the backend
`src/caps/provision_atecc.c` on an element part, compiled only when
`TFW_FEATURE_PROVISION=1`):

```
tfw_status_t tfw_prov_cmd_info(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_write_config(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_lock(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_genkey(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_pubkey(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_prov_cmd_seed_uds(tfw_reader_t *r, tfw_writer_t *w);

/* The backend the handlers call, one file per part. */
tfw_status_t tfw_prov_info(uint8_t rev[4], uint8_t cfg[32], uint8_t lock[2]);
tfw_status_t tfw_prov_write_config(uint8_t block, const uint8_t data[32]);
tfw_status_t tfw_prov_lock(uint8_t zone, uint16_t crc);
tfw_status_t tfw_prov_genkey(uint8_t slot, uint8_t pub[64]);
tfw_status_t tfw_prov_pubkey(uint8_t slot, uint8_t pub[64]);
tfw_status_t tfw_prov_seed_uds(uint8_t slot);
```

Go SDK (`sdk/go/provision.go`, package `tfw`):

```go
func (d *Device) ProvInfo(ctx context.Context) (*ElementInfo, error)
func (d *Device) ProvWriteConfig(ctx context.Context, block uint8, data []byte) error
func (d *Device) ProvLock(ctx context.Context, zone uint8, summaryCRC uint16) error
func (d *Device) ProvGenKey(ctx context.Context, slot uint8) ([]byte, error)
func (d *Device) ProvPubKey(ctx context.Context, slot uint8) ([]byte, error)
func (d *Device) ProvSeedUDS(ctx context.Context, slot uint8) error

type ElementInfo struct {
	Revision     [4]byte
	Serial       [9]byte
	ConfigLocked bool
	DataLocked   bool
	ConfigBlock0 [32]byte
}

const (
	ZoneConfig uint8 = 0x00
	ZoneData   uint8 = 0x01
	SlotDeviceID uint8 = 0
	SlotUDS      uint8 = 1
)
```

A separate surface touches no element and opens no port — it bakes the
platform-reserved EEPROM block into a `.eep` image before the device is ever
flashed (`sdk/go/platform.go`, package `tfw`):

```go
func BuildPlatformBlock(serial [SerialLen]byte, pubkeyHash [PubkeyHashLen]byte,
	operatorPubkey [OperatorPubkeyLen]byte) [PlatformBlockLen]byte
```

`SerialLen` (16), `PubkeyHashLen` (32), `OperatorPubkeyLen` (64, issue #4) and
`PlatformBlockLen` (192, grew from 128 when `operatorPubkey` was added) mirror
`include/tfw/boot.h`'s `TFW_BOOT_SERIAL_LEN`/`TFW_BOOT_MEASURE_LEN`/
`TFW_BOOT_OPERATOR_PUBKEY_LEN`/`TFW_BOOT_EE_LEN`. `operatorPubkey` is the
`AUTH_CHALLENGE`/`AUTH_RESPONSE` session's trust anchor — a P-256 point, X‖Y
— and is meaningful only on the element tier; a soft-tier or `CRYPTO=none`
image never reads it, so the zero value is correct there. CLI:
`sdk/go/cmd/tfwprov --bake-eeprom` — writes a partial Intel HEX file
(`sdk/go/cmd/tfwprov/eeprom.go`) covering only the platform block's 192
addresses, which is what makes it safe: `avrdude -U eeprom:w:file:i` writes
only the addresses a hex file names and leaves the rest of the part's EEPROM
untouched.

**The programmer matters, not just the file.** The `.eep` file this tool
writes has been verified correct offline (byte-for-byte, against
`tfw_boot_platform_parse`'s own decode — see the test row below). ISP is the
supported route. `tfw-boot` exposes only PING, OTA and RESET
(`boot/boot_session.c`), and the application's `EEPROM_WRITE` refuses the
region unconditionally, so neither offers another one.

A stock serial bootloader is a bench route, and on the Mega 2560 its
verification is wrong, not its write. On 2026-10-15, on the bench Mega with
its factory STK500v2 bootloader, `avrdude -c wiring -p m2560 -D -U
eeprom:w:platform.eep:i` wrote a fresh 192-byte block and reported:

```
avrdude: 192 bytes of eeprom written
avrdude: verifying eeprom memory against platform.eep
avrdude warning: verification mismatch
        device 0x54 != input 0x00 at addr 0x001c (error)
avrdude error: verification mismatch
```

The application's own `EEPROM_READ`, eight bytes at a time over `[0x000,
0x0C0)`, then read back every byte of the file, the CRC at `0x0BE` included.
The write had landed. The bootloader writes EEPROM at the address it is
given and reads it from twice that address: avrdude's eight-byte read page
*j* returns the part's page *2j*. The word `de ad be ef` that the application
reads at `0x100`, outside the block, reads through avrdude at `0x080`. Every avrdude EEPROM
read through this bootloader shows that pattern: the block's even pages packed into the
first half, then `0xFF`, then whatever sits at twice the address.

That is what the earlier "a prefix landed, the rest was dropped" reading saw.
The block the part held then was not a torn v2 write either. It carried the
pre-#3 magic `'A','G','P','R'` and layout version 1, a bake from before the
rename, which a current build rejects by its magic alone. And the
software-tier `ATTEST` that answered "no provisioned identity" did so for a
third reason: behind a stock bootloader the software tier has no UDS
(`src/crypto/soft/uds_flash.c`), so it cannot sign whatever the block holds.

Do not trust avrdude's EEPROM verification through this bootloader, and do not
take a failed one as proof of a bad write. Check the block on the part: with
the application's `EEPROM_READ` (the software tier reads the reserved region;
the element tier gates the read behind the operator session), or with the
bench bake image below, which writes the block from code running on the part
and checks it with the firmware's own parse. The bake image is the documented
route for a board without an ISP programmer. It does not depend on the
bootloader's EEPROM commands at all, and some stock bootloaders have none.

### The bench bake image (`make platform-bake`, issue #7)

A separate AVR image that writes the `.eep` block from the inside, for a
development board that still runs its stock serial bootloader and has no ISP
programmer on it.

```sh
make platform-bake BOARD=<board> SERIAL=<hex> [PUBKEY_HASH=<hex>] [OPERATOR_PUBKEY=<hex>]
make platform-bake-flash BOARD=<board> SERIAL=<hex> [AVRDUDE_PORT=<dev>]
```

**Why it is not a hole.** It needs the ability to flash arbitrary code to the
part, and whoever has that already owns the part: they could flash any image
that writes any EEPROM. A production part runs `tfw-boot`, which accepts
signed images only, and the bake image is never signed with a vendor key — it
cannot run there. ISP remains the path for fuses and lock bits, which no code
on the part can set.

**One encoder.** `mk/platform-bake.mk` runs `tfwprov --bake-eeprom` with the
same flags, converts its Intel HEX to binary with the host's GNU `objcopy`
(Debian 13's `binutils-avr` 2.43.50 cannot read Intel HEX: `avr-objcopy -I
ihex` fails with "invalid operation"),
and turns the bytes into a C array (`build/platform-bake/<board>/gen/block.c`).
Nothing in the image or the build lays out the block a second time. A block of
any length but `TFW_BOOT_EE_LEN` fails to compile.

**What it does, once per boot** (`tools/platform-bake/bake.c`,
`tfw_bake_run`):

1. Reads `[0x000, 0x0C0)` through `tfw_hal_nvm_read` — the view the
   firmware's `tfw_platform_id_init` and `tfw-boot` take.
2. A valid block equal to its own: `already`, nothing written.
3. A valid block that differs: `refused`, nothing written. A device that
   already has an identity keeps it; see `include/tfw/boot.h` on re-baking.
4. Anything else — blank, torn, a bad CRC, a layout this build does not know:
   writes the 192 bytes through `tfw_hal_eeprom_write`, reads them back, and
   checks them with the same `tfw_boot_platform_parse` the firmware uses:
   `baked` or `readback-failed`.

Then it prints `platform-bake board=<board> serial=<hex> result=<result>` at
the board's `TFW_UART_BAUD` and halts. It never writes outside the region.

**What it links.** `tools/platform-bake/main.c` and `bake.c`, the generated
block, `boot/boot_platform.c`, the firmware's CRC pair, and the HAL's sys,
early, EEPROM and UART files — compiled with the firmware's `FW_CFLAGS` and
linked with its `FW_LDFLAGS`, by reference. `make platform-bake-no-link-check`,
part of `make ci`, asks `mk/firmware.mk` for the object list of every board and
tier, with and without `PROVISION=1` and `ETH=1`, and fails if any bake object
is in one, or if `src/`, `boot/` or `include/` name the bake image. The
application's `EEPROM_WRITE` refusal of the region does not change.

CLI: `sdk/go/cmd/tfwprov` (binary `tfwprov`) — see [`cli.md`](cli.md).

### The production-image check (`scripts/prov-absent-check.py`)

R3 is a file-selection rule, and this check holds the build to it. It
compiles nothing and needs no toolchain. It asks make for the object list
each image links (`firmware-srcs`), the feature defines it compiles with
(`feature-defs`) and the boot image's list (`BOOT_SRCS`, read through
`--eval`).

It asks for every board under `boards/` and every tier the board builds,
first as the board defaults it. A board that names an Ethernet controller is
also asked with `ETH=0` and with `ETH=1`. Then come every extra row of
`firmware-matrix-list` and the example application (`test/app/example`) on
each board's first tier. Each of these is asked with `PROVISION=0` and with
`PROVISION=1`.

- A production image (`PROVISION=0`) links no provisioning unit:
  `src/caps/provision.c`, any `src/caps/provision_*.c` and any
  `src/hal/*/prov_*.c`. The match is by pattern, so a new backend is caught
  the day it is added. Nor does it compile with any `TFW_FEATURE_PROVISION`
  define.
- A jig image (`PROVISION=1`) of the same row links `src/caps/provision.c`
  and defines `TFW_FEATURE_PROVISION=1`. Otherwise the patterns no longer name
  what a jig links, and a pass would have checked nothing.
- No boot image links a provisioning unit, jig or not. Its list must hold
  `boot/boot_main.c`, or the probe read the wrong variable.

`scripts/test_prov_absent_check.py` runs the sweep on the tree under `make
scripts-test`, and so in `make test` and `make ci`. It also tests each rule on
literal lists, and it forces a production image to link the wire on make's
command line, to show that the sweep reads make and not a copy of it. Run it
alone with `python3 scripts/prov-absent-check.py`. `tfwsim` is not a
production image, and the check does not ask for it.

The bench bake image is not a production image either, and the same script
holds it to R5. It asks make for every board's bake list
(`platform-bake-srcs`) and fails if one links a provisioning unit or any of
`src/crypto/*/*.c`, `src/crypto/deviceid_*.c`, `src/hal/*/crypto_*.c`,
`src/*/*atecc*.c` and `src/hal/*/hal_bus.c`: a crypto tier's provider or the
element's driver, an architecture's own provider, other element code, and the
bus the element sits on. Each list must hold `tools/platform-bake/bake.c`, or
the probe read nothing.

## Invariants

- **`ProvLock` refuses a zero summary CRC.** The element's own no-CRC lock form
  locks without checking the zone contents; a zero CRC is refused in both
  `tfw_prov_cmd_lock` (`TFW_ERR_BAD_ARGC`) and `Device.ProvLock`
  (`ArgRangeError`), so the check cannot be bypassed from either side of the
  wire.
- **`ProvWriteConfig` never touches blocks 0 or 1.** They hold the serial
  number, revision and OTP mode; `tfw_prov_cmd_write_config` returns
  `TFW_ERR_PROTECTED_REGION` for `block < 2`, so a broken template fails at the
  firmware boundary rather than as an opaque element execution error.
- **`ProvGenKey` and `ProvPubKey` are separate commands, not modes of one.**
  `ProvGenKey` replaces the DeviceID; `ProvPubKey` only recomputes the public
  point from the key already in the slot. This split is what makes
  `tfwprov --verify` safe to run against an already-provisioned part — no
  verify path can regenerate the identity it is checking.
- **`ProvSeedUDS` takes a slot and no key material.** The UDS goes
  Random → TempKey → slot entirely inside the element (`tfw_prov_cmd_seed_uds`);
  the host never sees it, and the SRAM copy is scrubbed with `memset` whether
  or not the write succeeded.
- **Both slot constants are fixed, not caller-supplied policy.** `ProvGenKey`
  and `ProvPubKey` reject any `slot != SlotDeviceID` (`TFW_ERR_RANGE`);
  `ProvSeedUDS` rejects any `slot != SlotUDS`. A template cannot redirect the
  DeviceID key or the UDS into a slot the rest of the firmware never reads
  from.
- **Locking is irreversible and the SDK does not pretend otherwise.** There is
  no `ProvUnlock`. Once `ConfigLocked` or `DataLocked` is true on a part, no
  call in this surface can change it.

### The `--commit` sequence

`runCommit` in `sdk/go/cmd/tfwprov/steps.go` performs, in this order — the only
order the element accepts, since slot writes require a locked config zone and
the data zone cannot lock before the config zone does:

1. write each template config block (`ProvWriteConfig`)
2. lock the config zone (`ProvLock`, `ZoneConfig`) — **irreversible**
3. generate the DeviceID key in the element (`ProvGenKey`, `SlotDeviceID`)
4. seed the UDS from the element's TRNG (`ProvSeedUDS`, `SlotUDS`)
5. lock the data zone (`ProvLock`, `ZoneData`) — **irreversible**

If the config zone is already locked, steps 1–2 are skipped. If the data zone
is already locked, `runCommit` refuses outright: the part cannot be
reconfigured.

**Resolved** (issue #6): the device serial is baked into a `.eep` image
BEFORE the device is ever flashed, by `tfwprov --bake-eeprom`
(`sdk/go/cmd/tfwprov/eeprom.go`, `sdk/go/platform.go`) — never over the wire.
`sdk/go/commands.go`'s `EEPROMWrite` still refuses every address below
`EEPROMReservedEnd` (`0xC0`) unconditionally, and that refusal was kept
absolute rather than given a provisioning exemption: a narrower hole is
still a hole. `--bake-eeprom` opens no serial port and needs no
`TFW_FEATURE_PROVISION` image; it is a pure file operation, run once, before
any of the fuse/flash/`tfwprov --commit` steps below. See
[`cli.md`](cli.md) and [`user.md`](user.md) for where it fits in the
sequence.

The same `--bake-eeprom` step now also bakes the `AUTH_CHALLENGE`/
`AUTH_RESPONSE` operator public key (issue #4, `--operator-pubkey`) into the
same block, for the same reason: the reserved-region write refusal is
absolute, so this is the only place either value can be set, and one bake
step doing both keeps `tfwprov`'s pre-flash sequence a single operation.

## Error semantics

| Status | Meaning here | Raised by |
|---|---|---|
| `TFW_ERR_NACK` (10) | the element did not answer the wake | every `tfw_prov_cmd_*` on `tfw_atecc_wake()` failure — not reported as "no element," since a miswired or overclocked bus is indistinguishable from an absent part |
| `TFW_ERR_RANGE` (11) | a block index, zone id, or slot id outside the permitted set | `tfw_prov_cmd_write_config` (block ≥ 4), `tfw_prov_cmd_lock` (unknown zone), `tfw_prov_cmd_genkey`/`_pubkey` (slot ≠ `SlotDeviceID`), `tfw_prov_cmd_seed_uds` (slot ≠ `SlotUDS`) |
| `TFW_ERR_BAD_ARGC` (5) | wrong payload length, or a zero summary CRC | `tfw_prov_cmd_write_config` (no data), `tfw_prov_cmd_lock` (CRC == 0) |
| `TFW_ERR_PROTECTED_REGION` (16) | write targets config block 0 or 1 | `tfw_prov_cmd_write_config` |
| `TFW_ERR_UNKNOWN_CMD` (4) | production image — no provisioning handler compiled in | any `0x70`–`0x75` command against a device built with `TFW_FEATURE_PROVISION=0` |
| `TFW_ERR_NOT_PROVISIONED` (15) | attestation attempted on an element with no UDS | not raised by this package; raised by the attestation path when steps 4–5 of `--commit` never completed — see `docs/security/threat-model.md` |

On the Go side, a short or malformed reply is `*ShortReplyError` (e.g.
`ProvInfo` needs 38 bytes: 4 revision + 32 config block 0 + 2 lock bytes), and
an out-of-range caller argument is `*ArgRangeError` — both raised before any
frame is sent where the check does not require the device.

## Configuration

- **Build-time gate:** `TFW_FEATURE_PROVISION` (`include/tfw/config.h`), default
  `0`. Set to `1` via `mk/firmware.mk`'s `PROV_DEFS` or `mk/host.mk`'s equivalent
  flag only for a provisioning-jig image; a fielded image must never carry it.
- **Template:** a `key = value` file (not YAML — see
  [`architecture.md`](architecture.md) for why), parsed by
  `sdk/go/cmd/tfwprov/template.go`. Required keys: `name`, `block<N>` (32 bytes
  hex, N is 2 or 3), `config_lock_crc`, `data_lock_crc` (both non-zero). See
  [`api.md`](api.md) for the format and `sdk/go/templates/atecc608a-dice.conf`
  for the shipped example.

## The ATECC608C (MIKROE-6774 Click board)

The element the boards take is the ATECC608C-TFLXTLS on MikroE's ECC608 Trust
Click (MIKROE-6774). The build identifier stays `atecc608a`: the 608C runs the
same command set, opcodes, packet format, counters and wake timing as the 608A
(AN5078, DS00005078; the 608A-to-608B guide, DS40002237A). The driver changes
for the 608C are the I2C address, which is now board-profile data, and the
execution times, now one table in `src/crypto/atecc608a/atecc.h`.

| Fact | Value | Source |
|---|---|---|
| Part at the element address | ATECC608C-TFLXTLS, revision `00 00 60 05` | schematic v100 (U3); AN5078 |
| I2C address as shipped | `0x36`, changeable once by `UpdateExtra` | DS40002515B §3.1 |
| Other parts on the Click | ATECC608C-TNGTLS at `0x35`, ECC608-TMNGTLS at `0x38`, same bus | schematic v100 (U2, U4) |
| State as shipped | pre-provisioned (TrustFLEX): config and data zones locked, slot policies set by Microchip | DS40002515B page 1 and §3 |
| Supply / I/O | 2.0–5.5 V supply, 1.8–5.5 V I/O | DS40002515B page 1 |
| VCC select | JP1 "VCC SEL", 3V3 or 5V; set to 5V for the AVRs. The schematic does not show the shipped position | schematic v100 |
| Pull-ups | R5, R6, 4.7 kΩ to the Click's VCC | schematic v100 |
| Pins | mikroBUS SDA and SCL; Nano A4/A5, Mega D20/D21 by flying lead | schematic v100; `boards/*/board_profile.h` |
| Bus speed | 100 kHz in the profiles; the part is rated to 1 MHz, but the wake pulse needs ≤ 100 kHz | DS40002515B table 10-3 |
| KDF details, clock divider | under NDA; not used and not guessed. Execution times assume the 608A's default divider | — |

Sources:
[DS40002515B](https://ww1.microchip.com/downloads/aemDocuments/documents/SCBU/ProductDocuments/DataSheets/ATECC608C-TFLXTLS-CryptoAuthentication-Data-Sheet-DS40002515.pdf),
[DS40002513](https://ww1.microchip.com/downloads/aemDocuments/documents/SCBU/ProductDocuments/DataSheets/ATECC608C-CryptoAuthentication-Summary-Data-Sheet-DS40002513.pdf),
[AN5078, DS00005078](https://ww1.microchip.com/downloads/aemDocuments/documents/SCBU/ApplicationNotes/ApplicationNotes/AN5078-ATECC608B-to-ATECC608C-Migration-Guide-DS00005078.pdf),
[DS40002237A](https://ww1.microchip.com/downloads/en/Appnotes/Migrating-from-the-ATECC608A-to-the-ATECC608B-DS40002237A.pdf),
[ECC608 Trust Click schematic v100](https://download.mikroe.com/documents/add-on-boards/click/ecc608-trust-click/ecc608-trust-click-schematic.pdf).

**The slot map is an open owner decision.** The `--commit` sequence above
assumes a blank part. The TFLXTLS is not blank: its zones are locked, so
`tfwprov --commit` refuses it. Its slot policies (DS40002515B §3.2.3) do not
match ours:

- Slot 0 holds a permanent primary key that Microchip generated. It signs
  external messages, so it can serve as the DeviceID, but `ProvGenKey` cannot
  replace it.
- Slot 1, where `SlotUDS` points, is an internal-sign private key, not a secret,
  so the CDI HMAC fails. The element tier therefore answers `NOT_PROVISIONED`.
- A 32-byte secret fits only slot 5. That slot takes an encrypted write under
  the slot-6 I/O protection key, which needs GenDig, and the driver does not
  implement it.
- The secure-boot public key belongs in slot 15, not slot 2.

`SlotUDS` is pinned in `sdk/go/provision.go`, so moving it is a wire-visible
choice for the owner, not a driver fix. The shipped layout, the options and a
proposed answer are in [the slot-map ADR](adr-trustflex-608c-slot-map.md).

## Test strategy

| Tier | Location | Covers |
|---|---|---|
| Firmware unit | `test/unit/test_provision.c`, linked against `test/model/atecc608a.c` | every handler against a software model of the element — block refusal, CRC-required lock, slot range checks, out-of-order lock, seeding never exposing key material |
| Go unit | `sdk/go/provision_test.go` | `ProvInfo` payload decoding (both lock-byte polarities, truncated/missing-lock-byte replies, a production image's `ErrUnknownCommand`), `ProvLock`'s zero-CRC refusal, `ProvWriteConfig`'s size check |
| Go unit | `sdk/go/platform_test.go` | `BuildPlatformBlock`'s layout, CRC coverage and tamper detection |
| Firmware unit (cross-language) | `test/unit/boot/test_boot_platform.c`'s `test_a_baked_eeprom_image_is_decoded_correctly` | a LITERAL byte-for-byte capture of what `tfwprov --bake-eeprom` actually wrote, decoded by `tfw_boot_platform_parse` — the two implementations of one layout checked against each other's output, not just against themselves |
| Firmware unit (bake image) | `test/unit/test_platform_bake.c`, linked against the mock HAL | the bench bake image's decision on a LITERAL `tfwprov --bake-eeprom` block — blank and torn regions baked with exactly 192 bytes and nothing past them, an equal block `already` with nothing written, a different valid block `refused` and left alone, a stuck cell `readback-failed`, an invalid carried block never written, the exact result line |
| Build (object lists) | `scripts/test_prov_absent_check.py`, under `make scripts-test` | no production image of any board, tier, Ethernet variant, matrix row or the example application links a provisioning unit or defines `TFW_FEATURE_PROVISION`; every jig image does; no boot image links one; each rule on literal lists, and a production image forced on make's command line to link the wire fails |
| Build (object lists) | the same script, `check_bake` | no board's bench bake image links a provisioning, element, crypto-provider or element-bus unit (R5); a bake list forced on make's command line to link the element's driver fails |
| Go unit (wire) | `sdk/go/cmd/tfwprov/noeeprom_test.go` | every `tfwprov` wire mode (`provision`: `--dry-run`, `--verify`, `--commit` on a blank, config-locked and locked part, `--commit` refused at each step) sends no frame whose id `src/core/commands.def` gates on `TFW_FEATURE_EEPROM` other than a read (R5); the same recorder, sent an `EEPROM_WRITE`, flags it |
| Build (ISP recipe) | `scripts/test_boot_flash_eesave.py`, under `make scripts-test` | `make boot-flash` run for real against a stand-in `avrdude` that holds a part's fuse bytes, with the EESAVE fuse, bit and part asked of each board's profile: the refusal, the first boot flash (factory record, then the one bit, then the flash, R7 and R8), a fuse write that does not hold, an already programmed part, and `BOOT_FIRST_FLASH=1` with `BOOT_EEPROM_RESTORE=1` refused |
| Build (lock read-back) | `scripts/test_lock_check.py`, under `make scripts-test` | `make lock-check` run for real against the same stand-in `avrdude`, with literal lock bytes against the ATmega2560's `BOOT_LOCK_PROGRAMMED`: the operator's `0x0C` and `0x00` pass; `0xFF`, the factory `0xCF` and one bit short (`0x1C`) refused with each unprogrammed bit named; an unreadable lock byte refused; a board whose profile names no bit (the ATtiny85) refused before `avrdude` runs; no call writes. The ATmega328P and ATmega32U4 by literal against their own profiles: `0x0C` passes, `0xFF` refused, one bit short (`0x2C`, `0x0D`) refused naming that bit (R6) |
| Simulator / tfwsim | `sdk/go/cmd/tfwctl/e2e_firmware_test.go` (`TestFirmwareEEPROM`) | wire-level EEPROM reserved-region enforcement (`tfwctl eeprom-write` refused at `0x000`, at `0x0C0`, the old end, and across `0x13F`, the reserved region's last byte since #125; written and read back at `0x180`) exercised against the real firmware build, not a model |
| Integration (simavr) | `test/integration/simavr/boot_harness.c`'s `test_signed_image_boots` | a serial baked into the emulated EEPROM reaches `ATTEST`'s `DeviceID` over the real wire, through `tfw_platform_id_init`, on the real compiled application — end to end, not mocked |

Run `test-provision`-equivalent unit targets per the top-level `Makefile`
conventions in this repo's `CLAUDE.md`; there is no dedicated
`integration-test-provision` target as of this writing — provisioning's
cross-implementation coverage runs through the simulator suite above.

## Observability

`tfwprov` is the only operator-facing signal: it prints the element's revision,
serial, and lock state (`report`) before any mode runs, and prints each
`--commit` step and the resulting DeviceID public key as it executes
(`runCommit`). There is no separate provisioning log; the operator is expected
to capture stdout as the provisioning record, per
[`architecture.md`](architecture.md)'s trust-boundary note that the record,
not the device, is the authority.
