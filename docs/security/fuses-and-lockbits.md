# Fuses and lock bits

> **Status: NORMATIVE** · Scope: provisioning, `docs/device-provisioning/user.md`
> · Last verified: 2026-09-22
>
> Several settings on this page are **irreversible**. Read the whole page before
> running anything on a part you care about.

Lock bits are what makes the software tier's UDS worth anything, and what makes
the "the DICE is immutable" claim in `docs/dice-attestation/architecture.md`
*partial* rather than absent. They are also the easiest thing in this project to
get permanently wrong.

## What lock bits do and do not do

They stop **external** access: ISP, high-voltage programming, JTAG, debugWIRE.
They do not constrain application code at all. A firmware that exposes a generic
"read any flash address" command has given away everything the lock bits were
protecting, and no fuse setting prevents that.

That is why the protected-resource set exists in the firmware itself, and why
**no boot-loader-section callable is exposed**: one trampoline into the BLS
voids the BLB read barrier from the inside.

## The settings

| Setting | Value | Why |
|---|---|---|
| `BOOTRST` | programmed | the part enters the bootloader on reset, which is what lets `tfw-boot` measure before the application runs |
| `BOOTSZ` | 2048 words (4 KB) | enough for Ed25519 verify plus the anti-rollback check |
| `BLB1` mode | **3** | the application cannot read or write the boot section; this is the barrier the software-tier UDS sits behind |
| `BLB0` mode | 3 | the boot section cannot be rewritten by the application |
| `LB` mode | 3 | no external read of flash or EEPROM |
| `BODLEVEL` | enabled, 2.7 V (or the part's nearest) | **not optional** — see below |
| `DWEN` | unprogrammed | debugWIRE reads SRAM and defeats everything above |
| `OCDEN` | unprogrammed | same, for parts with on-chip debug |
| `EESAVE` | programmed | a chip erase then preserves the EEPROM, including the device serial |

## Brown-out detection is a security setting here, not a reliability one

`BODLEVEL` must be enabled and verified at provisioning.

An EEPROM write on a sagging supply does not fail cleanly — it corrupts. This
EEPROM holds platform-reserved material in `0x000`–`0x0BF` (corrected
2026-10-16 from `0x07F`: the bound is `TFW_BOOT_EE_LEN`, 192 bytes), including the
bootloader's public key hash and the anti-rollback floor. A corrupted floor is a
downgrade attack that arrives by accident, and a corrupted key hash is a device
that will never accept another update.

With BOD enabled the part halts below the threshold instead of writing.

## Order matters, and one of these is a trap

1. Fuses first — `BOOTRST`, `BOOTSZ`, `BODLEVEL`, `DWEN` off.
2. Flash the bootloader, then the application.
3. Write the platform-reserved EEPROM region.
4. Provision the element (see `provisioning.md`) and **verify by reading back**.
5. Lock bits last.

**The trap:** lock bits can only be cleared by a chip erase, and a chip erase
also erases the flash. So setting them before the image is final costs a full
re-flash, and setting `LB` mode 3 before you have verified the EEPROM contents
costs you the ability to verify them at all.

**The second trap:** `DWEN` is a fuse, not a lock bit, and once programmed it
cannot be cleared over ISP — only over debugWIRE or with high-voltage
programming. A part shipped with `DWEN` programmed and lock bits set is a part
with an open debug channel you can no longer close over ISP.

## An ATECC608A cannot be unlocked

Locking the element's config zone and data zone is **permanent**. There is no
erase, no reset, no factory command. A wrong configuration template bricks the
part.

This is the entire reason `tfwprov` has three separate modes rather than one:

```sh
tfwprov --dry-run --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
tfwprov --verify  --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
tfwprov --commit  --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0 --yes-i-mean-it
```

`--dry-run` prints the plan and touches nothing. `--verify` reads the part back
and compares against the template. `--commit` is the only one that writes, and
it requires the flag that cannot be typed by accident.

**Do the first commit on a spare part.** Not as a precaution — as the
procedure.

## Decommissioning

A locked element is retired with the board. There is no path back.

The decommission procedure is therefore: revoke the DeviceID certificate, delete
the registry record, and — where the MCU allows it — chip-erase the part. The
element stays locked and stays with the board. This is written down so nobody
plans around an unlock that does not exist.

## Verification

`tfwprov --verify` reads back and compares:

- every fuse byte against the template
- the lock byte
- the element's config zone, its lock state, and its serial number
- the device serial in EEPROM
- that slot 0 holds a key and slot 1 is not readable

A device that fails any of these is not provisioned, whatever earlier steps
appeared to succeed.

## What this deliberately does not do

- **No fuse writing from the firmware.** Fuses are set by the programmer over
  ISP, at provisioning, by a person or a jig. A command that could change them
  would be a command that could unlock the part.
- **No lock-bit reporting as a security claim.** `CAPS` reports the crypto tier,
  not the fuse state. The firmware cannot read the lock bits in a way an
  attacker could not also forge, so the authority on whether a device was
  provisioned correctly is the provisioning record, not the device.
