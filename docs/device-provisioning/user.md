# Device provisioning — user guide

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

This is a bench procedure: physical access to the part, on a jig, never over
the network. Read this whole page before running anything on hardware you
care about — several steps below cannot be undone.

## Before you start

- **Practice on a spare part first.** This is the documented procedure, not a
  precaution: `tfwprov --commit` locks the secure element, and a locked element
  has no erase, no reset, and no factory unlock.
- Have a reviewed provisioning template ready (`sdk/go/templates/*.conf` — see
  [`api.md`](api.md) for the format). The template is what the part is checked
  against; a wrong template on `--commit` bricks the part.
- Know your serial port (e.g. `/dev/ttyUSB0`) and have an ISP programmer for
  the fuse and lock-bit steps.

## Task: provision a new part

1. **Bake the platform-reserved EEPROM block, before anything is flashed.**
   The device serial has no wire command that writes it — `src/caps/
   storage.c`'s refusal of the reserved region is absolute — so it goes into
   a `.eep` image here and is flashed alongside the fuses below.

   **Use an ISP programmer.** `tfw-boot` has no EEPROM-write command, and the
   application refuses this region over the wire, so on this production path
   ISP is the only supported route. A bench board with no ISP to hand has the
   bake image, below: it goes through the bootloader's flash write and checks
   the block on the part.

   Do not trust avrdude's verification of an EEPROM write through a stock
   serial bootloader. On a real Mega 2560 with the factory STK500v2
   bootloader, `avrdude -c wiring ... -U eeprom:w:platform.eep:i` writes the
   block correctly, then reports a verification mismatch. The bootloader reads
   EEPROM back from twice the address it was asked for, so avrdude compares
   the wrong bytes. A failed verify there is not proof of a bad write, and a
   readback through the same bootloader is not proof of a good one. Check the
   block on the part. The [engineering notes](engineering.md) give the
   evidence.

   ```sh
   tfwprov --bake-eeprom --serial <16-byte hex> --out platform.eep
   avrdude -c <isp-prog> -p <part> -U eeprom:w:platform.eep:i
   ```

   `--pubkey-hash` is also this step's job on `BOOT_SIG=ed25519-donna`
   (no row under item 79, owner ruling 2026-10-17: the verifier is P-256 on
   every row)
   (software-verify) builds; it is ignored on the element tier and may be
   omitted there. `--operator-pubkey` is this step's job for the
   `AUTH_CHALLENGE`/`AUTH_RESPONSE` session (issue #4, element tier only) —
   omit it on a soft-tier or `CRYPTO=none` build, where nothing ever reads
   the field. See [`cli.md`](cli.md).

   An Ethernet board (`ETH=1`) also needs its network block at `0x0C0`, right
   after the platform block. `--bake-netblock` encodes it with the SDK's
   `NetConfig` encoder, and given with `--bake-eeprom` it writes both blocks as
   one image, the 224 bytes `[0x000, 0x0E0)`:

   ```sh
   tfwprov --bake-eeprom --serial <16-byte hex> \
           --bake-netblock --mac 5a:ed:09:10:bf:59 --ip 198.51.100.22/24 \
           --gateway 0.0.0.0 --endpoint 198.51.100.1:7020 --out platform.eep
   avrdude -c <isp-prog> -p <part> -D -U eeprom:w:platform.eep:i
   ```

   `--bake-netblock` alone writes only `[0x0C0, 0x0E0)`, for a part whose
   platform block is already right. A running device can also take the block
   over serial (`tfwctl netcfg set`, or `WriteNetConfig` from Go); see
   [../ethernet-transport/user.md](../ethernet-transport/user.md).

2. **Set fuses.** `EESAVE` programmed: `make boot-flash` chip-erases the
   part, and refuses to while the erase would take this EEPROM with it
   ([../secure-boot/cli.md](../secure-boot/cli.md)). `BOOTRST` and `BOOTSZ` programmed, `BODLEVEL` enabled,
   `DWEN` unprogrammed. Do this before flashing anything else — see "Why
   these fuses" below for `BODLEVEL` in particular.

   ```sh
   avrdude -c <prog> -p <part> -U lfuse:w:...:m -U hfuse:w:...:m -U efuse:w:...:m
   ```

3. **Flash a provisioning image.** `PROVISION=1` compiles in the six
   provisioning command handlers that a production image never carries.

   ```sh
   make BOARD=atmega328p CRYPTO=atecc608a PROVISION=1 firmware
   make BOARD=atmega328p flash
   ```

4. **Plan, and read nothing wrong yet.**

   ```sh
   tfwprov --dry-run --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
   ```

   Read the printed plan. If the element reports a zone already locked, stop —
   see "If the part is already provisioned" below.

5. **Commit, on the spare part first.**

   ```sh
   tfwprov --commit --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0 --yes-i-mean-it
   ```

   This writes the config blocks, locks the config zone, generates the
   DeviceID key, seeds the UDS from the element's own randomness, and locks
   the data zone. **Write down the DeviceID public key it prints** — this
   part cannot report it again through any command that also generates one,
   and the printed record is what makes the device's identity provable later.

6. **Verify.** Not a formality — `--commit` reports what the element
   acknowledged; `--verify` reads back what the element actually holds, and on
   a part that cannot be unlocked those can disagree.

   ```sh
   tfwprov --verify --config sdk/go/templates/atecc608a-dice.conf /dev/ttyUSB0
   ```

7. **Lock bits, only now.** Locking before step 6 passes means a bad
   provisioning attempt is now also undiagnosable — lock bits block the ISP
   reads `--verify`'s equivalent debugging would need.

   ```sh
   avrdude -c <prog> -p <part> -U lock:w:0x0C:m
   ```

8. **Reflash the production image** before the part leaves the bench. The
   provisioning image must never ship — it is what makes re-provisioning
   possible, and a fielded device must not have that capability.

   ```sh
   make BOARD=atmega328p CRYPTO=atecc608a firmware
   make BOARD=atmega328p flash
   ```

Full flag reference: [`cli.md`](cli.md). Wire-level detail: [`api.md`](api.md).

## Task: bake the block on a bench board through its stock bootloader

For a development board that still runs its stock serial bootloader — an Uno,
a Nano, a Mega 2560 — and no ISP programmer on the desk. **A bench path only.**
It works because a stock bootloader flashes arbitrary code, and whoever can do
that already owns the part. A production part runs `tfw-boot`, which takes
signed images only; the bake image is never signed with a vendor key, so it
cannot reach one. Fuses and lock bits still need ISP.

1. **Build the bake image for the board and serial.** It carries exactly the
   block `tfwprov --bake-eeprom` writes for the same flags.

   ```sh
   make platform-bake BOARD=atmega2560 SERIAL=<16-byte hex> \
        [PUBKEY_HASH=<32-byte hex>] [OPERATOR_PUBKEY=<64-byte hex>]
   ```

2. **Flash it through the bootloader, and watch the port.**

   ```sh
   make platform-bake-flash BOARD=atmega2560 SERIAL=<16-byte hex> AVRDUDE_PORT=<dev>
   ```

   On boot it prints one line at the board's baud (115200 on the Uno, Nano
   and Mega) and halts:

   ```
   platform-bake board=atmega2560 serial=<32 hex digits> result=baked
   ```

   | `result=` | What it means | What to do |
   |---|---|---|
   | `baked` | the region was blank, torn or failed its CRC; the block was written, read back and parsed | flash the application |
   | `already` | the region already holds this exact block; nothing was written | flash the application |
   | `refused` | the region holds a **different** valid block; nothing was written | stop — this part already has an identity, and replacing it is not a bench decision (`include/tfw/boot.h`) |
   | `readback-failed` | the write did not read back as the block | do not use the part's identity; retry once, then suspect the supply or the EEPROM |

   Opening the port resets most of these boards through DTR, which runs the
   image again. A second line reading `already` after `baked` is that rerun,
   not a fault.

3. **Flash the application over it.** `make flash` passes `-D`, so avrdude does
   not chip-erase and the EEPROM keeps the block.

   ```sh
   make BOARD=atmega2560 firmware
   make BOARD=atmega2560 flash AVRDUDE_PORT=<dev>
   ```

The image writes only `[0x000, 0x0C0)`; the rest of the EEPROM is never
touched. The application's own `EEPROM_WRITE` refusal of the region is the
same as ever — the bake image is a separate image, and no part of it links into
the application.

## Why these fuses matter

| Setting | Why it is load-bearing |
|---|---|
| `BODLEVEL` (brown-out detection) | an EEPROM write on a sagging supply corrupts rather than fails, and this EEPROM holds key material and the platform's trust anchor. With BOD enabled the part halts below the voltage threshold instead of writing garbage. This is a security setting here, not a reliability one. |
| `DWEN` (debugWIRE) | debugWIRE reads SRAM directly and defeats every lock bit below it. It must stay unprogrammed, and — the trap — it can only be cleared over debugWIRE or high-voltage programming once set, never over ISP. A part shipped with `DWEN` on and lock bits set has an open debug channel nobody can close over ISP. |
| Lock bits (`BLB0`/`BLB1`/`LB`) | they stop **external** access only — ISP, JTAG, debugWIRE, high-voltage programming. They do **not** constrain application code at all. A firmware bug that exposes "read any flash address" gives away everything the lock bits were protecting, and no fuse setting prevents that. |

Full detail and the complete fuse table: `docs/security/fuses-and-lockbits.md`.

## If the part is already provisioned

`tfwprov --dry-run` and `tfwprov --commit` both check the element's lock state
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
