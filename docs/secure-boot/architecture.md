# tfw-boot — architecture

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

`tfw-boot` is a second, separate firmware image, linked with its own `main()`,
its own HAL and its own section address, flashed into the boot section with
`BOOTRST` programmed. On every reset it measures the application, verifies its
signature, checks the anti-rollback floor, derives the DICE layer-0 CDI, and
jumps — or, if any of those refuses, stays in a recovery path. It never
returns to itself; it either hands control to the application or halts/serves
OTA forever.

Sibling docs: [engineering.md](engineering.md) for the exported surface and
invariants, [cli.md](cli.md) for the build/flash verbs, [api.md](api.md) for
the on-wire commands and record layouts, [user.md](user.md) for operator
tasks. Provisioning (keys, EEPROM, lock bits) is
[../device-provisioning/user.md](../device-provisioning/user.md); DICE evidence
consumption is [../dice-attestation/architecture.md](../dice-attestation/architecture.md).

## Component boundaries

| Component | Files | Owns |
|---|---|---|
| Boot decision | `boot/boot_policy.c`, `boot/boot_image.c` | `tfw_boot_evaluate`: trailer parse, rollback check, measurement compare, signature check, in that order |
| Measurement | `boot/boot_measure.h`, `boot/measure/{measure_soft,measure_atecc608a}.c` | SHA-256 over flash, one backend linked |
| Verification | `boot/boot_verify.h`, `boot/verify/{verify_ed25519_donna,verify_atecc608a}.c` | signature check, one backend linked |
| DICE layer 0 | `boot/boot_cdi.h`, `boot/tier/{cdi_soft,cdi_atecc}.c` | CDI derivation and the rollback floor's authority |
| Reserved EEPROM | `boot/boot_platform.{c,h}`, `boot/boot_store.{c,h}` | parse/serialise and the floor's software-tier fallback |
| Handoff | `boot/boot_handoff.c` | building/parsing the CDI block the application reads |
| Recovery | `boot/boot_recovery.h`, `boot/recovery/recovery_ota.c`, `boot/hal/avr/recovery_halt.c` | what runs when nothing may boot, one backend linked |
| OTA session | `boot/boot_ota.{c,h}`, `boot/boot_session.{c,h}` | the four-command wire protocol, linked only when `BOOT_OTA=1` |
| HAL | `boot/boot_hal.h`, `boot/hal/avr/boot_hal_avr.c` | flash, EEPROM, UART, handoff placement, jump/reset |
| Shared with the application | `src/crypto/sha256.c`, `src/proto/frame.c`, `src/proto/crc16.c`, `src/proto/crc16_update.c`, `src/hal/avr/hal_early.c` | one hash, one frame codec, one watchdog-disarm sequence. The application's *firmware* image links `src/hal/avr/hal_crc16.c` instead of `crc16_update.c` for the per-byte fold (#12); the bootloader does not carry that substitution, so it keeps the portable form. |
| Wire layouts | `include/tfw/boot.h` | the trailer, the reserved-EEPROM region, the handoff block — each has a second party that must agree byte for byte |

`boot/boot_main.c` composes these into `main()` and owns nothing else: every
refusal reason lives in `tfw_boot_evaluate` so "why did this device not boot"
has one place to read, not a scatter of checks between peripheral setup.

## Data flow

```
reset (BOOTRST) -> .init3 (hal_early.c: clear MCUSR, disarm watchdog)
  -> main(): request_take (read + clear the app's update request), uart_init, cdi_init
  -> platform_load (reserved EEPROM)                -> fail: recovery(reason)
  -> boot_state == STAGED?                           -> yes: recovery(TFW_ERR_CRYPTO)
  -> floor_load
  -> request honoured?                               -> yes: session_open (bounded; returns on
                                                        idle timeout, hands over on a verified OTA_END)
  -> tfw_boot_evaluate(floor):
       trailer_parse -> rollback_permit -> measure -> compare -> verify
                                                                -> fail: recovery(reason)
  -> floor_advance + floor_store (only on a verified boot)
  -> tfw_boot_hand_over: cdi_derive -> handoff_build -> handoff_write -> zeroize -> jump_application
```

Recovery is one of two link-time backends. `recovery_ota.c` serves the framed
OTA session (`TFW_CMD_OTA_*`) over the polled UART and re-enters
`tfw_boot_evaluate` on `OTA_END`; `recovery_halt.c` disables interrupts and
sleeps forever, because the framed path plus the hash plus the boot decision
do not fit an 8 KB-or-smaller image alongside signature verification (see
[cli.md](cli.md) and `mk/boot.mk`'s `BOOT_SUPPORTED` rows).

A healthy device is moved into that loop by the application: `RESET` mode 1
leaves a one-shot request in the handoff region and takes a watchdog reset.
The request is app-to-bootloader, the reverse of the CDI handoff, and shares its
SRAM span because both images already agree on its address. It is bounded by an
idle timeout, falls through to the ordinary verify-and-jump, and grants nothing
(the images are checked as always). See [api.md](api.md#update-session-request).
The `RESET` itself needs the operator auth session on the element tier.

## Trust boundaries

- **The signing key never enters the boot section as a value the code can be
  tricked into replacing.** On `BOOT_SIG=ed25519-donna` it is patched into
  boot-section flash at provisioning and pinned by a SHA-256 in reserved
  EEPROM (`tfw_boot_check_pubkey_pin`); on `BOOT_SIG=atecc608a` it never leaves
  a locked element slot at all, named by number, and `tfw_boot_pubkey` answers
  `TFW_ERR_UNSUPPORTED` because there is nothing to hash and nothing to pin.
- **The UDS never reaches the application.** It is consumed inside the
  bootloader (`tfw_boot_cdi_derive`) and only the derived CDI crosses into
  SRAM. Under BLB1 mode 3 the application cannot read boot-section flash at
  all, so even a UDS held in flash (`cdi_soft.c`) is physically unreachable
  from application code — the CDI, not the secret, is what the handoff block
  in `include/tfw/boot.h` carries.
- **No boot-section code is callable from the application.** The handoff is
  data in `.noinit` SRAM, written once and read once, precisely because a
  trampoline into the boot section would void the BLB mode 3 read barrier
  from the inside — see [engineering.md](engineering.md) for the invariant.
- **Reserved EEPROM `0x000`–`0x0BF` is a trust boundary the application-facing
  storage API refuses to cross.** `src/caps/storage.c` rejects writes to it;
  the pubkey hash, rollback floor and — element tier only, since issue #4 —
  the `AUTH_CHALLENGE`/`AUTH_RESPONSE` operator public key living there is
  what makes that refusal a security boundary rather than tidiness.
- **The anti-rollback floor is authoritative over the signature.** A signed
  image with a version below the floor is refused before the (expensive)
  signature check runs at all; see [engineering.md](engineering.md) for the
  ordering rationale.

## Alternatives rejected

| Alternative | Rejected because |
|---|---|
| A function-pointer table selecting measure/verify/recovery backends at runtime | A vtable is a RAM-resident table of live references, so `--gc-sections` can no longer drop the backends not selected — every backend's code stays in the image whether linked in or not. Link-time substitution (`BOOT_TIER`, `BOOT_MEASURE`, `BOOT_SIG`, `BOOT_OTA` in `mk/boot.mk`) costs nothing at runtime and lets the linker discard everything unreachable, which is the only way a signature backend fits a 4 KB section at all. |
| Verifying in the application rather than in the bootloader | An application that verifies its own successor still has to have booted to do it — so a tampered image would run, at least once, before anything checked it. Verification belongs at the one point that runs before any application byte executes. |
| Hashing on the MCU where the element can do it | `BOOT_MEASURE=atecc608a` exists for exactly the opposite reason: the software SHA-256 (`src/crypto/sha256.c`, 4 052 bytes) does not fit a 4 KB boot section alongside the rest of the core. Where the section is large enough (`BOOT_MEASURE=soft` on the 8 KB `atmega2560` build), software hashing is several hundred times faster and is what's used — the element measurement is a size accommodation, not a preference. |
| An A/B application slot for OTA | An ATmega328P has 32 KB of flash total; two 28 KB application slots need 56 KB, which does not exist without external storage the board contract does not provide. See [api.md](api.md) for how the bootloader itself substitutes for the missing second slot. |

## What this deliberately does not do

- **No signing.** `tfw_boot_verify` has no counterpart that produces a
  signature; the private key belongs to the build system, never to a device.
- **No arbitrary flash read/write command.** The four-command OTA surface
  (`TFW_CMD_OTA_*`) writes pages sequentially and never lets a caller name an
  address inside the boot section — `tfw_boot_flash_write_page` refuses any
  write that would reach `TFW_BOOT_APP_END`.
- **No bootloader self-update over the wire.** There is no command that
  rewrites the boot section; a bootloader that could replace itself over UART
  is one an attacker could replace over UART, and there is no key-rotation
  story that survives that.
- **No MPU-backed protection of the CDI in SRAM.** The handoff transits plain
  SRAM with no MPU on classic AVR to protect it; the mitigation is that both
  sides zeroize their copy immediately after use, not hardware isolation.
- **No A/B rollback of the application image.** An OTA writes in place; an
  interrupted write leaves no bootable application until a new image is sent
  or written over ISP. This is a stated cost, not an oversight — see
  [api.md](api.md).
