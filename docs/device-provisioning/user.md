# Device provisioning — user guide

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

This is a bench procedure: physical access to the part, on a jig, never over
the network. Read this whole page before running anything on hardware you
care about — several steps below cannot be undone.

## Before you start

- **Practice on a spare part first.** This is the documented procedure, not a
  precaution: `agprov --commit` locks the secure element, and a locked element
  has no erase, no reset, and no factory unlock.
- Have a reviewed provisioning template ready (`sdk/go/templates/*.conf` — see
  [`api.md`](api.md) for the format). The template is what the part is checked
  against; a wrong template on `--commit` bricks the part.
- Know your serial port (e.g. `/dev/ttyUSB0`) and have an ISP programmer for
  the fuse and lock-bit steps.

## Task: provision a new part

1. **Set fuses.** `BOOTRST` and `BOOTSZ` programmed, `BODLEVEL` enabled,
   `DWEN` unprogrammed. Do this before flashing anything — see "Why these
   fuses" below for `BODLEVEL` in particular.

   ```sh
   avrdude -c <prog> -p <part> -U lfuse:w:...:m -U hfuse:w:...:m -U efuse:w:...:m
   ```

2. **Flash a provisioning image.** `PROVISION=1` compiles in the six
   provisioning command handlers that a production image never carries.

   ```sh
   make BOARD=atmega328p CRYPTO=atecc608a PROVISION=1 firmware
   make BOARD=atmega328p flash
   ```

3. **Plan, and read nothing wrong yet.**

   ```sh
   agprov --dry-run --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
   ```

   Read the printed plan. If the element reports a zone already locked, stop —
   see "If the part is already provisioned" below.

4. **Commit, on the spare part first.**

   ```sh
   agprov --commit --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0 --yes-i-mean-it
   ```

   This writes the config blocks, locks the config zone, generates the
   DeviceID key, seeds the UDS from the element's own randomness, and locks
   the data zone. **Write down the DeviceID public key it prints** — this
   part cannot report it again through any command that also generates one,
   and the printed record is what makes the device's identity provable later.

5. **Verify.** Not a formality — `--commit` reports what the element
   acknowledged; `--verify` reads back what the element actually holds, and on
   a part that cannot be unlocked those can disagree.

   ```sh
   agprov --verify --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
   ```

6. **Lock bits, only now.** Locking before step 5 passes means a bad
   provisioning attempt is now also undiagnosable — lock bits block the ISP
   reads `--verify`'s equivalent debugging would need.

   ```sh
   avrdude -c <prog> -p <part> -U lock:w:0x0C:m
   ```

7. **Reflash the production image** before the part leaves the bench. The
   provisioning image must never ship — it is what makes re-provisioning
   possible, and a fielded device must not have that capability.

   ```sh
   make BOARD=atmega328p CRYPTO=atecc608a firmware
   make BOARD=atmega328p flash
   ```

Full flag reference: [`cli.md`](cli.md). Wire-level detail: [`api.md`](api.md).

## Why these fuses matter

| Setting | Why it is load-bearing |
|---|---|
| `BODLEVEL` (brown-out detection) | an EEPROM write on a sagging supply corrupts rather than fails, and this EEPROM holds key material and the platform's trust anchor. With BOD enabled the part halts below the voltage threshold instead of writing garbage. This is a security setting here, not a reliability one. |
| `DWEN` (debugWIRE) | debugWIRE reads SRAM directly and defeats every lock bit below it. It must stay unprogrammed, and — the trap — it can only be cleared over debugWIRE or high-voltage programming once set, never over ISP. A part shipped with `DWEN` on and lock bits set has an open debug channel nobody can close over ISP. |
| Lock bits (`BLB0`/`BLB1`/`LB`) | they stop **external** access only — ISP, JTAG, debugWIRE, high-voltage programming. They do **not** constrain application code at all. A firmware bug that exposes "read any flash address" gives away everything the lock bits were protecting, and no fuse setting prevents that. |

Full detail and the complete fuse table: `docs/security/fuses-and-lockbits.md`.

## If the part is already provisioned

`agprov --dry-run` and `agprov --commit` both check the element's lock state
before doing anything. If the config or data zone is already locked, the part
cannot be reconfigured — there is no erase, no reset. Treat it as either
already correctly provisioned (run `--verify` to confirm) or as scrap.

## Decommissioning a device

A locked secure element **cannot be unlocked**. Decommissioning does not
reclaim the part — the element retires with the board it is soldered to.
Decommissioning means: revoke the DeviceID certificate and delete the registry
record (both in go-trusted-platform — see below), and chip-erase the part
where the MCU allows it. Nobody should plan around getting the element back.

## What happens after provisioning

Provisioning gives the device a key, nothing more. Turning that key into a
certificate — enrollment, over ACME `device-attest-01` — and everything about
the device registry or fleet policy is go-trusted-platform's job, not this
repo's. See that project's documentation; a gap there is not something to work
around here.
