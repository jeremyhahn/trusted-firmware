# tfw-boot — architecture

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-11-21
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
| Verification | `boot/boot_verify.h`, `boot/verify/{verify_p256_soft,verify_atecc608a,verify_ed25519_donna}.c`, `boot/verify/p256_key_slot.c`, `boot/verify/donna_shim.c`, `boot/boot_cose.c` | signature check, one backend linked (`BOOT_SIG=p256-soft`, `atecc608a` or `ed25519-donna`); `p256_key_slot.c` is the p256-soft key slot the provisioning jig patches, `donna_shim.c` the one symbol ed25519-donna needs defined, and `boot_cose.c` the `OTA_ACTIVATE`/`OTA_CONFIRM` Sig_structures the AVR store row and the ESP32 layer 0 share |
| DICE layer 0 | `boot/boot_cdi.h`, `boot/tier/{cdi_soft,cdi_atecc}.c` | CDI derivation and the rollback floor's authority |
| Reserved EEPROM | `boot/boot_platform.{c,h}`, `boot/boot_store.{c,h}` | parse/serialise and the floor's software-tier fallback |
| Handoff | `boot/boot_handoff.c` | building/parsing the CDI block the application reads |
| Recovery | `boot/boot_recovery.h`, `boot/recovery/recovery_ota.c`, `boot/hal/avr/recovery_halt.c`, `boot/hal/cortex-m/recovery_halt.c` | what runs when nothing may boot, one backend linked |
| OTA session | `boot/boot_ota.{c,h}`, `boot/boot_session.{c,h}` | the four-command wire protocol, linked only when `BOOT_OTA=1` |
| The one trial | `boot/boot_trial.{c,h}` | the OTA state machine's transitions and refusals, pure over the platform block ([../ota/architecture.md](../ota/architecture.md#what-runs-today-77)) |
| A/B swap | `boot/boot_swap.{c,h}`, `boot/boot_swap_none.c` | swap-move of the primary and secondary slots (the ATmega2560's, and a two-slot MPS2 board's under `BOOT_AB=1`) and its phase-and-step record, one byte rewritten per advance so a torn EEPROM write leaves the old position or the new; `boot_swap_none.c` on every other row except a `boot_prev.c` row, where the three calls `main()` makes change nothing |
| Fall-back | `boot/boot_prev.c` | the PREV restore of a single-slot store row, linked in place of `boot_swap_none.c` only when `BOOT_STORE=1`, `BOOT_AB=0` and `BOOT_PREV=1` (`mk/boot.mk`); in `BOOT_SUPPORTED` today those are the MPS2 AN505 and AN500 store rows (`mps2-an505:soft:soft:p256-soft:0:1`, `mps2-an500:soft:soft:p256-soft:0:1`). On such a row it copies the store's PREV image back over an ended trial or a refused pending image and ends the round GOOD with FAILED (`TFW_BOOT_EE_FLAG_REFUSED` set for a refusal). Neither AVR single-slot part has it: the ATmega328P's `profile.mk` sets `BOOT_PREV ?= 0` (owner queue 176, option (a)), so a failed activation there has no boot-time rollback and the part is recovered by reflash, and the ATmega32U4 has no store row in `BOOT_SUPPORTED`, since with the restore it does not fit the boot section ([../ota/architecture.md](../ota/architecture.md#the-328p-and-32u4-fallback-from-prev-52)) |
| Stage | `boot/boot_stage.{c,h}`, `boot/boot_stage_none.c` | tfw-boot's side of the external OTA store: read the stager's MARK, check the staged image again, copy it into the application region; `boot_stage_none.c` on a board with no store (`BOOT_STORE=0`) |
| Update request | `boot/boot_request.{c,h}`, `boot/boot_entry.c` | the update-session request: the encoding both images share (`boot_request.c`, pure, linked by the application too) and tfw-boot's half that reads and clears it through the boot HAL (`boot_entry.c`) |
| HAL | `boot/boot_hal.h`; one of `boot/hal/avr/`, `boot/hal/cortex-m/`, `boot/hal/esp32/`, `boot/hal/host/` linked per image | flash, EEPROM, UART, handoff placement, jump/reset, and the OTA store's bus where the board names one. AVR: `boot_hal_avr.c`, `boot_start_avr.S`, `boot_ota_store_avr.c`. Cortex-M: the core's half in `boot_hal_cortex-m.c` and `boot_jump.c`, `boot_ota_store_cortex-m.c`, `boot_factory.c`/`boot_factory_none.c` (a two-slot MPS2 board's factory copies), and the SoC's half (flash and UART) in `mcxn94x/`, `mps2/`, `nrf54l/`, `rp2350/` and `stm32h5/`. ESP32: `boot_hal_esp32.c` on ESP-IDF's second-stage bootloader, beside layer 0 (`boot_l0.c`, `l0_gate.c`, `l0_crypto.c`, `flash_span.c`, `measure_esp32.c`, `verify_p256_rom.c`, `verify_digest_soft.c`; [../esp32/](../esp32/)). Host: `boot_hal_host.c`, tfw-boot inside tfwsim |
| Shared with the application | `src/crypto/sha256.c`, `src/proto/frame.c`, `src/proto/crc16.c`, `src/proto/crc16_update.c`, `src/hal/avr/hal_early.c` | one hash, one frame codec, one watchdog-disarm sequence. The application's *firmware* image links `src/hal/avr/hal_crc16.c` instead of `crc16_update.c` for the per-byte fold (#12); the bootloader does not carry that substitution, so it keeps the portable form. |
| Wire layouts | `include/tfw/boot.h` | the trailer, the reserved-EEPROM region, the handoff block — each has a second party that must agree byte for byte |

`boot/boot_main.c` composes these into `main()` and owns nothing else: every
refusal reason lives in `tfw_boot_evaluate` so "why did this device not boot"
has one place to read, not a scatter of checks between peripheral setup.

## Data flow

The diagram is `boot/boot_main.c`'s, which `mk/boot.mk` links on the AVR,
Cortex-M and host builds only. Its first line is the AVR entry (`BOOTRST`,
`.init3`); the Cortex-M and host HALs enter `main()` from their own reset path
— see [../boards/](../boards/) for each board and [../cortex-m/](../cortex-m/).
From `main()` on, the order is the same on those three.

The ESP32 links no `boot_main.c`. Its layer 0 (`boot/hal/esp32/boot_l0.c`)
replaces ESP-IDF's second-stage entry, `call_start_cpu0`, and runs
`bootloader_init`, ESP-IDF's choice of the boot partition, layer 0's verify,
gate and CDI derivation, then ESP-IDF's loader
(`bootloader_utility_load_boot_image`). It has no request/session, EEPROM
trial or swap step; see [../esp32/engineering.md](../esp32/engineering.md).

```
reset (BOOTRST) -> .init3 (hal_early.c: clear MCUSR, disarm watchdog)
  -> main(): request_take (read + clear the app's update request), uart_init, cdi_init
  -> platform_load (reserved EEPROM)                -> fail: recovery(reason)
  -> trial_at_reset -> swap_at_reset -> stage_after_reset
                      (swap and stage are no-ops on a board without the slots or a store)
                      TRIAL                          -> FAILED, recovery(TFW_ERR_BAD_STATE)
  -> floor_load                                      -> fail: recovery(reason)
  -> CONFIRMED?                                      -> evaluate, floor := version, GOOD
  -> stage_take (copy a staged image from the external store, if any)
  -> request honoured?                               -> yes: session_open (bounded; returns on
                                                        idle timeout, arms the trial and hands
                                                        over on a verified OTA_END)
  -> boot_state == STAGED?                           -> yes: recovery(TFW_ERR_CRYPTO)
  -> swap_take (A/B: swap a pending secondary in)
  -> tfw_boot_evaluate(floor):
       trailer_parse -> binding_permit -> rollback_permit -> measure -> compare -> verify
                                                                -> fail: swap_fall_back (A/B or PREV)
                                                                   -> evaluate again
                                                                -> fail: PENDING -> TRIAL+FAILED, recovery(reason)
  -> PENDING? arm: TRIAL
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
  tricked into replacing.** (Item 79, owner ruling 2026-10-17: the verifier
  is P-256 on every row, so no row ships `ed25519-donna`.) On
  `BOOT_SIG=ed25519-donna` it is patched into
  boot-section flash at provisioning and pinned by a SHA-256 in reserved
  EEPROM (`tfw_boot_check_pubkey_pin`); on `BOOT_SIG=atecc608a` it never leaves
  a locked element slot at all, named by number, and `tfw_boot_pubkey` answers
  `TFW_ERR_UNSUPPORTED` because there is nothing to hash and nothing to pin.
  On `BOOT_SIG=p256-soft` the key is the boot-section slot
  `boot/verify/p256_key_slot.c` the provisioning jig patches, write-protected
  by the lock bits, and `tfw_boot_pubkey` likewise answers `TFW_ERR_UNSUPPORTED`.
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
- **Reserved EEPROM `0x000`–`0x13F` is a trust boundary the application-facing
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
