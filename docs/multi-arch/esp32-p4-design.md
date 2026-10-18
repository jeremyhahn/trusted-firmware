# ESP32-P4 port: the design from main to the HAL and boot-HAL port

> **Status: PROPOSED** · Issue: [#48](../../../../issues/48)
> · ADR: [`adr-esp-idf-integration.md`](adr-esp-idf-integration.md) ([#16](../../../../issues/16))
> · Scope: trusted-firmware (docs only) · Last verified: 2026-10-18
>
> The owner ruling of 2026-10-18 (CA m623) lifts the ESP32-P4 gate in full: a
> design slice first, then the HAL and boot-HAL port, identity tests on QEMU
> `esp32c3`, and hardware-in-the-loop on the bench host once its factory state
> is recorded and verified. This page is that design slice. It changes no code.

## Sources and how they are cited

Every claim names its source as `path` and section, or `path:line` on main.
The repository holds no Espressif document: `docs/reference/` carries the IETF,
NIST and RFC texts only (`docs/reference/README.md`). Where this page rests on
an Espressif fact it names the ESP-IDF v5.5.5 Programming Guide page and
section, marks it **(ESP-IDF, not in the repo)**, and states the fact only as
far as the port on main already relies on it. A fact the port has never
exercised is marked **to confirm** and is the named slice's first task, not a
premise of this design. TCG DICE and RFC 6979 are not in `docs/reference/`
either; both are marked where used.

## 1. Inventory: what is on main

### 1.1 Files of the port

| File | What it does |
|---|---|
| `mk/arch/esp32.mk` | Read only by an ESP32 board. Pins ESP-IDF `v5.5.5` and Espressif QEMU `esp_develop_9.2.2_20260417` (ADR, "Toolchain pins"). |
| `mk/arch/esp32-targets.mk` | `esp32-build`: evaluates `FW_SRCS` from `mk/firmware.mk`, generates the RSA-3072 secure boot v2 signing key into the build directory on first use, and runs `idf.py` with the source list, board directory and feature defines. Refuses `PROVISION=1` ("no provisioning image on ESP32 yet"). `esp32-qemu-test`: builds `esp32-qemu` for `esp32c3` (`hmac-ds` tier) and `esp32` (`none` tier), builds `wirecheck`, runs `test/integration/esp32/qemu_test.py` on each. |
| `mk/arch/esp32/CMakeLists.txt`, `main/CMakeLists.txt`, `main/Kconfig.projbuild`, `sdkconfig.defaults` | The ESP-IDF project (ADR decision 1, option 1c). One component registers exactly `TFW_SRCS`. `Kconfig.projbuild` carries `CONFIG_TFW_ETH_STATIC_IP` (ADR follow-up 9). |
| `boot/hal/esp32/bootloader_components/main/CMakeLists.txt` | Layer 0's bootloader component. Its sources today: `boot_l0.c`, `boot_hal_esp32.c`, `boot/boot_handoff.c`, `src/proto/crc16.c`, `src/proto/crc16_update.c`. No trailer, verifier or SUIT code. |
| `boot/hal/esp32/boot_l0.c` | Replaces `call_start_cpu0` (ADR decision 3, option 2c). `bootloader_init`, partition-table load, `bootloader_utility_get_selected_boot_partition`, then `measure_and_hand_off`, then `bootloader_utility_load_boot_image`. `measure_and_hand_off` runs `esp_image_verify(ESP_IMAGE_VERIFY, …)`; on success the image's appended SHA-256 is the measurement and `derive_cdi` computes `HMAC-SHA256(UDS, measurement)` through `ets_hmac_calculate_message` on the `HMAC_UP` key block. A slot that fails verification hands off tier `NONE` with a zero CDI. The handoff carries `data.secure_version` as the image version. |
| `boot/hal/esp32/boot_hal_esp32.c` | The boot HAL: flash read and page write on 4096-byte sectors, NVM on the `tfw_plat` partition, the handoff in RTC-retained memory, reset cause, jump and reset. |
| `src/hal/esp32/hal_sys.c` | `app_main` hosts the superloop in one FreeRTOS task; tick, reset, reset-cause decode (`TFW_RESET_SOFTWARE`, `TFW_RESET_PANIC`; ADR follow-up 6). |
| `src/hal/esp32/hal_eth.c`, `hal_uart.c` | The TCP byte link on port 21574 behind `tfw_hal_uart_*` (ADR decision 5); DHCP or the static address. |
| `src/hal/esp32/hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_pulse.c`, `hal_bus.c` | The peripheral HAL on ESP-IDF's drivers (ADR decision 2). |
| `src/hal/esp32/hal_eeprom.c` | The EEPROM capability on the `tfw_eeprom` partition, sector read-modify-write (ADR decision 6). |
| `src/hal/esp32/hal_handoff.c` | `tfw_hal_handoff_block`: the RTC-retained handoff to `src/core/handoff.c`. |
| `src/hal/esp32/hal_board.c`, `hal_esp32.h`, `hal_types.h` | Board glue; partition names; the C6 enable held low (ADR, Consequences). |
| `src/hal/esp32/crypto_hmac_ds.c`, `id_record.h` | The `hmac-ds` provider. `tfw_crypto_init` takes the CDI from the handoff (tier `SECURE_ELEMENT`) and runs `deviceid_check`: reads the RSA record from `tfw_id`, signs a fixed digest through `esp_ds_sign` and verifies it against the record's modulus. `tfw_crypto_algorithms` returns 0; `tfw_crypto_sign` and `tfw_crypto_verify_image` refuse; `tfw_crypto_random` is `esp_fill_random`. |
| `boards/esp32p4-poe/` | `board_profile.h`, `board.c`, `profile.mk` (`ESP_TARGET := esp32p4`, `BOARD_TIERS := hmac-ds`, the factory flash size and SHA-256), `partitions.csv` (`tfw_plat` `0x11000`, `tfw_id` `0x12000`, `tfw_eeprom` `0x13000`, `otadata` `0x14000`, `ota_0` `0x20000` and `ota_1` `0x620000`, 6 MB each), `sdkconfig.defaults` (EMAC and IP101, 16 MB flash, flash encryption in release mode). |
| `boards/esp32-qemu/` | The QEMU board, `0x81`, for both machines (ADR decision 8), with `sdkconfig.defaults.esp32` for the Xtensa run. |
| `test/integration/esp32/qemu_test.py`, `wirecheck/` | The QEMU harness: `make_identity` generates the UDS and the RSA DS key and record, `provision_efuses` burns them and the secure boot digest through QEMU's download mode, `boot` and `boot_facts` read layer 0's and the provider's log lines, `wirecheck` drives `sdk/go` over the emulated Ethernet. |
| `test/unit/test_board_esp32p4-poe.c`, `test_board_esp32-qemu.c` | Host unit tests of the two board profiles, in `make test`. |
| `docker/Dockerfile.esp32`, the `esp32` CI job | ESP-IDF v5.5.5 and Espressif QEMU; runs `make esp32-build BOARD=esp32p4-poe` and `make esp32-qemu-test` (ADR, Consequences, last bullet). |

### 1.2 What `make esp32-qemu-test` proves today

From the ADR's Consequences, first bullet, and the harness above, on each
machine with keys generated at test time:

- the ROM verifies the bootloader and the bootloader the app (secure boot v2,
  RSA-PSS); layer 0's measurement equals the image's own SHA-256;
- on `esp32c3`, the CDI fingerprint equals `HMAC-SHA256(UDS, measurement)`
  computed on the host, and the DS self-test verifies against the generated
  RSA key;
- all three survive a soft reset and a cold start, and an EEPROM mark written
  over the wire survives both;
- `sdk/go` answers PING, CAPS (board `0x81`, the expected tier) and SYS_INFO
  over the emulated Ethernet;
- an image with one byte flipped and its checksum and digest fixed up is
  refused: no measurement, no wire.

It does **not** prove, because none of it exists yet: a P-256 DeviceID, any
signature of ours (no trailer v1, no ES256, no SUIT manifest in layer 0), OTA,
anti-rollback, provisioning on the part, or anything on the P4 itself (the P4
image is built in CI but runs nowhere).

### 1.3 Gaps found on main (docs and data, for the slices)

- **The DeviceID is RSA in DS** (ADR decision 4; `crypto_hmac_ds.c`
  `deviceid_check`), against the owner ruling of 2026-10-17 (CA m499, m522):
  ECDSA P-256 on every board, ES256 only. Section 2.1.
- **The SPEC row `esp32-p4`** says "the DeviceID key is held by the DS
  peripheral" and, in the same sentence, quotes the ruling that there is no RSA
  DeviceID (`docs/SPEC.md` row `esp32-p4`). DS is an RSA engine, so the row
  contradicts itself. Section 5 proposes the text.
- **Flash size.** The bench record reads a 32 MB part
  (`docs/development/bench/esp32-p4.md`, first table); the profile builds for
  16 MB (`boards/esp32p4-poe/sdkconfig.defaults`, `partitions.csv` header). A
  16 MB image runs on a 32 MB part; the slots could be larger. Slice 7 settles it.
- **Stale comment.** `boards/esp32p4-poe/profile.mk` says "the board has not
  arrived"; it is on the bench (`docs/development/bench/esp32-p4.md`).
- **The soft P-256 DeviceID is ruled but not on main.** The AVR soft provider
  still signs with HMAC-SHA256 and has no public key
  (`src/crypto/soft/provider.c`, `tfw_crypto_sign`, `tfw_crypto_pubkey`). The
  P-256 code on main is the boot verifier (`boot/verify/verify_p256_soft.c`)
  over micro-ecc (`src/third_party/micro-ecc/`), which also has
  `uECC_compute_public_key` and `uECC_sign`.
- **The manifest gate has no caller.** `tfw_suit_gate` (`src/core/suit.c`) is
  called by no file in `boot/` or `src/` (`docs/SPEC.md` row `ota`, "The
  manifest gate is built but nothing calls it yet").

## 2. Decisions the port still needs

### 2.1 The P-256 DeviceID on the P4

The ruling fixes the algorithm: ECDSA P-256, ES256, on every board
(`docs/SPEC.md` row `dice-attestation`, owner ruling 2026-10-17, CA m499). The
ADR's decision 4 (DS-held RSA) is superseded by it. What remains is where the
P-256 private key lives.

| | (a) Derived in software from the HMAC-peripheral UDS | (b) The P4's ECDSA peripheral, eFuse key | (c) Random P-256 key generated on the part, stored in the encrypted `tfw_id` partition |
|---|---|---|---|
| How | The provider asks the HMAC peripheral for `HMAC-SHA256(UDS, "tfw-deviceid-v1")` on the `HMAC_UP` key layer 0 already uses, reduces it to a P-256 scalar as the element model does (`test/model/atecc608a.c`, retry until `uECC_compute_public_key` accepts), and signs with micro-ecc. The scalar is in SRAM only, zeroed after use. Independent of the measurement, so it survives every OTA; it changes only with the UDS or the label. | A P-256 key burned into an eFuse key block with the ECDSA key purpose, read-protected; signing through ESP-IDF's ECDSA driver **(ESP-IDF, not in the repo: API Reference › Peripherals › ECDSA, "ECDSA on ESP32-P4"; to confirm: the purpose name, P-256 support on revision v3.2 and public-key export)**. | `PROV_GENKEY` generates a key with the settled entropy source (2.4) and writes it to `tfw_id`, which flash encryption covers once it is marked `encrypted` (ADR decision 6 leaves `tfw_id` unencrypted today). The DICE engineering option A1 ("random DeviceID", `docs/dice-attestation/engineering.md`, `CONFIG_DICE_STRICT_DEVICEID`). |
| No key leaves the part in the clear | Yes: the UDS never leaves the eFuse, the scalar never leaves SRAM. A signed application can recompute it, the same gap the ADR states for the CDI (ADR, Consequences, "A gap in the ESP32 HMAC model"). | Yes, the strongest: no software ever holds the scalar. Only if the key is generated on the part; a key burned from the host was in the clear on the host. | Only with flash encryption on. In QEMU flash encryption is off (ADR, Consequences), so the key would sit in the clear in the emulated flash. |
| Testable on QEMU `esp32c3` | **Yes.** QEMU models the C3's HMAC peripheral (ADR, "What the emulator offers"); micro-ecc is portable C. | **No.** No QEMU machine models an ECDSA peripheral (ADR QOM table); the C3 has none. Bench only. | Partly: generation and the wire, not the confidentiality. |
| DICE fit | DICE-shaped: DeviceID from the UDS through the device's own one-way function (TCG DICE Layering Architecture; not in the repo). | Element-tier shape: an independent hardware key, as on the ATECC608A (ADR, "The element-tier precedent"). | Element-tier shape, software-held. |
| Flash | micro-ecc sign and public key in the application; to be measured in slice 1 (RISC-V, not the AVR's figures). | ESP-IDF's driver; small, to be measured. | As (a), plus the generator. |
| RAM | One 32-byte scalar and micro-ecc's stack during a sign. | None of ours. | As (a). |
| Boot time | One public-key derivation per boot (or on first `PUBKEY`); to be measured on QEMU and on the bench. | None. | One flash read. |
| eFuse use | **None new**: the `HMAC_UP` block layer 0 already uses. Frees the DS key block. | One more key block, burned once, irreversibly. | None new; needs flash encryption's key block. |

**Recommendation: (a).** It is the only option QEMU can test, it costs no new
eFuse, it keeps the DeviceID stable across updates, and it is the same
derivation the AVR soft root needs once that row signs with P-256 (`docs/SPEC.md`
row `dice-attestation`). Under L1 that derivation is one portable file, used
by both; whichever lane lands first writes it, and the other reuses it. ECDSA
nonces come from RFC 6979 deterministic signing (`uECC_sign_deterministic`;
RFC 6979 is not in `docs/reference/`, so slice 1 adds it there), so signing
needs no entropy at all. Option (b) stays the hardening path for a later
silicon-only row, an owner decision because it burns a key block and changes
every enrolled DeviceID.

**The DS peripheral** leaves the identity path entirely. `deviceid_check`, the
DS self-test, `id_record.h` and the RSA record go; the QEMU harness stops
burning the `HMAC_DOWN_DIGITAL_SIGNATURE` key (KEY2) and generating the RSA
key. The `tfw_id` subtype `0x41` stays reserved, never reused (as status codes
extend and never renumber), and the partition is either dropped from the table
or kept for option (c) later. DS's RSA is not needed for secure boot either:
secure boot v2's RSA is the ROM's and the bootloader's (`esp_image_verify`),
not DS. The provider file is renamed for what it is (`crypto_hmac_p256.c`,
tier name `hmac-p256`), a whole-file swap in the object list.

**The tier and root type.** The provider reports P-256 in
`tfw_crypto_algorithms`, a 64-byte `PUBKEY` and an ES256 `SIGN`, so `ATTEST`
turns on with the existing P-256 alias template. The root is neither the
element nor the EEPROM soft root: it is an on-die eFuse UDS. The chain carries
the root type (go-xkms item 76, `docs/SPEC.md` row `dice-attestation`), and
that vocabulary is go-xkms's; a value for "on-die eFuse UDS" is a seam
question for go-xkms, listed in section 6.

### 2.2 Layer 0 and tfw-boot: trailer v1 and the SUIT manifest inside the signed image

Every tfw-boot row verifies a signature (owner ruling 2026-10-17, CA m453); on
the P4 tfw-boot is layer 0 in the second-stage bootloader (ADR decision 3).
Two signatures, two keys, two owners:

1. **Secure boot v2 (RSA-3072, ESP-IDF's).** The ROM verifies the bootloader;
   `esp_image_verify` in layer 0 verifies the app. Composed, unchanged (ADR
   decision 2).
2. **Our ES256 gate (layer 0's).** Trailer v1 with `board_id` `0x80` and the
   SUIT-subset manifest, inside the span secure boot v2 signs (`docs/SPEC.md`
   row `ota`, item 23; `docs/ota/adr-ota-image-format-and-trust.md` §1 "Composed
   vendor boot" and §2).

**Where the trailer sits.** ESP-IDF's app image is its segments, a checksum and
an appended SHA-256 (the digest layer 0 already uses as the measurement); the
secure boot v2 signature block follows at the next 4 KB boundary **(ESP-IDF,
not in the repo: Security › Secure Boot v2, "Signature Block Format" and
"Verifying an image"; to confirm: that the signed digest covers the padding
up to the signature block)**. The build appends trailer v1, then the SUIT
envelope, after the appended SHA-256 and before `espsecure.py` signs, so both
lie inside the RSA-signed span. The trailer's `image_len` is ESP-IDF's image
length up to the appended digest, so the trailer's `measurement` equals
`esp_image_metadata_t.image_digest`: the measurement, the CDI and the trailer
name the same 32 bytes, and layer 0 hashes nothing twice. If the padding is
not signed, the fallback is a fixed `.rodata` section in the image holding the
trailer with `measurement` excluded from it, which is a different trailer
semantics and would need the owner; slice 2 settles which holds before any
code.

**The order in layer 0**, before `bootloader_utility_load_boot_image`:

1. `esp_image_verify`: secure boot v2 and the image digest (on main).
2. `tfw_boot_trailer_parse` at `image_len` past the slot base.
3. `tfw_boot_binding_permit` with `TFW_BOARD_ID` (`0x80` or `0x81`).
4. `tfw_boot_rollback_permit` against the floor (2.3).
5. The trailer's `measurement` against `image_digest`, constant-time
   (`tfw_boot_equal`).
6. The key pin: `tfw_boot_check_pubkey_pin` against the hash in `tfw_plat`.
7. `tfw_boot_verify` (ES256) over the trailer's signed span.
8. Before activation of a new slot, `tfw_suit_gate`, then the `OTA_ACTIVATE`
   authorization (2.3).
9. Only then `derive_cdi`. A refusal at any step hands off no tier, as a
   failed `esp_image_verify` does today, and the slot is refused (2.3).

**Reused unchanged:** `boot/boot_image.c` (`tfw_boot_trailer_parse`,
`tfw_boot_binding_permit`, `tfw_boot_rollback_permit`),
`boot/verify/verify_p256_soft.c` and `boot/verify/p256_key_slot.c` with
`src/third_party/micro-ecc/`, `src/crypto/sha256.c`, `src/core/suit.c`
(`tfw_suit_read`, `tfw_suit_gate`), `boot/boot_handoff.c`. **Changed:**
`boot/boot_policy.c`'s `tfw_boot_evaluate_at` derives the trailer address from
the fixed `TFW_BOOT_TRAILER_ADDR` (`boot/boot_policy.c`, first line of the
body); the ESP32 trailer follows the image, so the address becomes an input.
That is a small refactor of a shared file with every AVR row's tests as its
guard, or a separate ESP32 evaluation in `boot/hal/esp32/` that calls the same
primitives; slice 2 picks the one that keeps `boot_policy.c` unchanged for
AVR. **Not reused:** the AVR copy, swap and session code (`boot_swap.c`,
`boot_store.c`, `boot_session.c`, `boot_stage.c`), replaced by ESP-IDF's OTA
(2.3).

**Cost.** Bootloader flash: micro-ecc, the trailer parser, the SUIT reader and
SHA-256 join layer 0. The bootloader with layer 0 is 44–52 KiB today and the
partition table was moved to `0x10000` for it (ADR decision 6); the P4's
bootloader starts at `0x2000`, so 56 KiB is the ceiling before the table moves
again. Slice 2 measures it; a move to `0x18000` shifts `tfw_plat`, which is a
reflash, not a migration. Boot time: one ES256 verify in the bootloader per
boot, to be measured (QEMU first, then the bench). eFuse: none; the image key's
hash is pinned in `tfw_plat`, which flash encryption protects on the board.

### 2.3 OTA on `esp_ota`

The ruling composes ESP-IDF's OTA (`docs/SPEC.md` row `ota`;
`docs/ota/adr-ota-image-format-and-trust.md` §7 "ESP32-P4: ESP-IDF's
`ota_0`/`ota_1` and `otadata`, and its app rollback" and the composed-platform
table). ESP-IDF facts below are from **ESP-IDF v5.5.5, API Reference › System
API › Over The Air Updates (OTA), sections "OTA Process Overview", "App
Rollback" and "Anti-rollback" (not in the repo)**; the slice that uses each
confirms it first.

| Wire (`docs/ota/adr-ota-image-format-and-trust.md` §8) | On the P4 |
|---|---|
| `OTA_BEGIN` (`0x68`) | `esp_ota_get_next_update_partition`, then `esp_ota_begin` with the announced size. `TFW_ERR_BAD_STATE` (19) while an image is pending, as on AVR. |
| `OTA_DATA` (`0x69`) | `esp_ota_write` in order; an out-of-order offset is refused, as the AVR stager does. |
| `OTA_END` (`0x6A`) | `esp_ota_end`. The wire meaning stays "every byte arrived, nothing verified" (§8): `esp_ota_end` also runs ESP-IDF's image check, so on the P4 a bad image can already fail here, with the existing verify-failure status. A stronger refusal, not a different meaning; stated in `docs/ota/api.md` by the OTA slice. |
| `OTA_STATUS` (`0x6B`) | The session's own counters, as on AVR. |
| `OTA_INFO` (`0x6C`) | `esp_ota_get_running_partition`, `esp_ota_get_state_partition` and `esp_ota_get_partition_description` for each slot; `floor` is the eFuse secure version; `state` maps `ESP_OTA_IMG_PENDING_VERIFY` to `TRIAL`; `flags` bit 0 from the bootloader's rollback. |
| `OTA_AUTH` (`0x6F`, proposed) | Stores the COSE_Sign1 ES256 authorization in `tfw_plat` for layer 0 (`docs/SPEC.md` row `ota`, CA m502 item 2). |
| `OTA_ACTIVATE` (`0x6D`) | The application checks the trailer and the manifest as layer 0 will, then `esp_ota_set_boot_partition`; mode 1 resets. At that reset layer 0 checks the stored authorization with the image key, as tfw-boot does on every tier (row `ota`, #137). Refused: layer 0 does not boot the slot and selects the previous one, the slot marked invalid in `otadata` (**to confirm**: the bootloader-side call that marks it, with a custom `call_start_cpu0`). |
| `OTA_CONFIRM` (`0x6E`) | In `TRIAL` only: the authorization checked at the next reset as for activate, then `esp_ota_mark_app_valid_cancel_rollback`, and the floor advanced (below). |

**Trial boot and rollback.** With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, a new
slot boots once in `ESP_OTA_IMG_PENDING_VERIFY`; if the app has not marked it
valid before the next reset the bootloader marks it aborted and boots the
previous slot. That is the one trial boot of owner ruling 16:15Z (Q9: a failure
means another OTA round). The per-board trial deadline is the application's,
as on AVR (row `ota`, "What runs today"); at the deadline the app resets
without confirming, so ESP-IDF rolls back. Layer 0 keeps
`bootloader_utility_get_selected_boot_partition` as the slot chooser, so the
rollback state machine stays ESP-IDF's (L2).

**Anti-rollback and `secure_version`.** The P4's floor is the eFuse secure
version through ESP-IDF's anti-rollback (`docs/ota/adr-ota-image-format-and-trust.md`,
the floor table: "one-way bits; a small fixed number of steps for the part's
life"). The build sets `CONFIG_BOOTLOADER_APP_SECURE_VERSION` from the
trailer's `image_version`, and layer 0 refuses an image whose two values
differ, so one fact has one value. ESP-IDF advances the eFuse when a confirmed
image carries a higher secure version. **Each advance burns eFuse bits and the
supply is finite**: the release policy (which releases advance it) is an owner
decision, listed in section 4.

**Geometry (ADR follow-up 7).** `esp_ota` owns the slot writes, so the AVR
page and counter widths never reach flash on the P4. What remains is the wire:
`OTA_BEGIN`'s size and `OTA_DATA`'s offset must address a 6 MB slot, and the
OTA slice checks both field widths against `docs/ota/api.md` before code.

**`ARCH_UPDATE_REQUEST := none`** and `RESET(1)` → `UNSUPPORTED` (ADR,
Consequences) are dropped by the slice that serves `OTA_ACTIVATE`.

### 2.4 Entropy and provisioning (ADR follow-ups 4 and 7)

The brief numbers these 4 and 7; follow-up 7 in the ADR is the boot geometry,
answered in 2.3. Provisioning is follow-up 4.

**What must be settled before any key is generated on the part:**

1. **Entropy source.** `tfw_crypto_random` is `esp_fill_random`, true random
   only while a noise source runs (ADR, Consequences, "Entropy"). The P4 board
   runs no radio: the C6, the board's radio, is held in reset from the first
   instruction (ADR, Consequences, "The C6 co-processor"). The
   source is ESP-IDF's `bootloader_random_enable` (the SAR ADC) for the length
   of a generation **(ESP-IDF, not in the repo: API Reference › System API ›
   Random Number Generation; to confirm for the P4)**. With option (a) and
   RFC 6979 signing, the application needs entropy only for `AUTH_CHALLENGE`
   nonces; key generation happens once, at provisioning.
2. **Who generates the UDS.** The UDS is the `HMAC_UP` key. Burned from the
   host, as the QEMU harness does today, it was in the clear on the host,
   which breaks "no key leaves the part in the clear" for a real part. On the
   part: a provisioning image (`PROVISION=1`, refused today by `esp32-build`)
   generates 32 bytes under the settled source, burns them with
   `esp_efuse_write_key` into a free block with purpose `HMAC_UP`, read- and
   write-protected, and never returns them.
3. **What the wire returns.** `PROV_GENKEY` returns a 64-byte point (ADR
   follow-up 4), which is exactly the option (a) DeviceID public key. The RSA
   modulus problem of follow-up 4 is gone with the RSA DeviceID.
4. **Which key blocks.** One for the UDS; secure boot v2's digest block(s);
   flash encryption's key. No DS block. Option (b) would add one.
5. **The QEMU path keeps host-generated keys**, because the harness must compute
   the expected CDI and DeviceID; the on-part path is tested on QEMU only for
   its wire and its refusals (a second `PROV_GENKEY` refused, no UDS readable).

## 3. Slices

Each is one lane (≤300 turns). Each slice's gate is `make esp32-qemu-test`
(both machines) plus `make ci` for any shared file, unless it says the bench.

| # | Slice | Files | Gate | Proves |
|---|---|---|---|---|
| 1 | **P-256 DeviceID from the HMAC UDS; DS retired.** **First slice QEMU alone verifies.** | `src/hal/esp32/crypto_hmac_ds.c` → `crypto_hmac_p256.c`; `id_record.h` removed; the portable DeviceID derivation file (with the AVR soft root, section 2.1) and its host unit test; `mk/firmware.mk` (tier and micro-ecc in the ESP32 list); `boards/*esp32*/profile.mk` tier name; `test/integration/esp32/qemu_test.py` and `wirecheck`; `docs/reference/` RFC 6979; ADR, `architecture.md` | `make esp32-qemu-test` on `esp32c3`; `make ci` | The DeviceID point equals the host's derivation from the UDS, is the same across soft reset and cold start, `SIGN` verifies as ES256 on the host, CAPS reports P-256 and `ATTEST` on; no KEY2 burned. |
| 2 | **Trailer v1 and ES256 in layer 0.** | `boot/hal/esp32/boot_l0.c`, its bootloader `CMakeLists.txt`; `boot/boot_policy.c` or a new `boot/hal/esp32/` evaluation; `mk/arch/esp32-targets.mk` (trailer append and sign before `espsecure.py`, key pin baked into `tfw_plat` with the existing `platform-bake`); `boards/*esp32*/partitions.csv` if the table moves; harness | `make esp32-qemu-test`, both machines | Images for another board, with board id 0, with a flipped trailer signature, or with `image_version` ≠ `secure_version` are refused with no CDI; a good image boots with the same measurement as before; bootloader size and boot time measured. |
| 3 | **The SUIT manifest gate in layer 0.** | `boot/hal/esp32/boot_l0.c`; `mk/suit.mk` and `suit-emit` in the ESP32 build; harness | `make esp32-qemu-test` | `tfw_suit_gate` refuses a manifest for another digest, board or a lower sequence number, before activation; its first caller on any board. |
| 4 | **OTA session on `esp_ota`.** | A new ESP32 update-path file in `src/hal/esp32/` chosen by the object list (item 20); `mk/firmware.mk`; `wirecheck` streaming over Ethernet; `docs/ota/` | `make esp32-qemu-test` on `esp32c3` | `OTA_BEGIN`/`OTA_DATA`/`OTA_END`/`OTA_STATUS`/`OTA_INFO` write and report the inactive slot; a bad image fails at `OTA_END`; nothing activates. |
| 5 | **`OTA_AUTH`, activate, trial, confirm, rollback, floor.** | The update-path file; `boot/hal/esp32/boot_l0.c` (the authorization at reset); `sdkconfig.defaults` (rollback and anti-rollback); harness | `make esp32-qemu-test` on `esp32c3` (QEMU's file-backed eFuse holds the secure version) | An unauthorized activate is refused at reset; an unconfirmed trial rolls back by itself; a confirmed image advances the emulated floor; an older image is then refused; `RESET(1)` stops answering `UNSUPPORTED`. |
| 6 | **Provisioning image and entropy.** | `PROVISION=1` for ESP32 in `mk/arch/esp32-targets.mk`; the provisioning file; harness | `make esp32-qemu-test` on `esp32c3` | `PROV_GENKEY` burns a UDS the harness cannot read back, returns the DeviceID point, refuses a second call. |
| 7 | **Bench HIL-0: factory record verified, pins, first boot with no eFuse burned.** | `boards/esp32p4-poe/` (pins against the schematic, flash size, the stale comment); `docs/development/bench/esp32-p4.md` | The bench | The record matches; our bootloader and app boot with layer 0's ES256 check, tier `none` (no UDS yet); the IP101 link; PING, CAPS (`0x80`) and SYS_INFO over the network; `make board-restore` brings the factory image back. |
| 8+ | **Bench HIL-1 to HIL-3**, one owner decision each (section 4). | `boards/esp32p4-poe/`; the bench page | The bench | The QEMU suite's checks repeated on silicon at each step. |

Slices 1–6 need no board. Slice 1 can run now; 2 before 3; 4 before 5; 6 after
1. Slice 7 can run beside any of 1–6.

## 4. Hardware-in-the-loop on the bench host's P4

**Before any write:** read the chip id, flash id and `espefuse summary`, and
read the whole flash again; compare against
[`../development/bench/esp32-p4.md`](../development/bench/esp32-p4.md): the
flash SHA-256, and every security eFuse in its table (all unburned,
`KEY_PURPOSE_0`–`5` `USER`, `SECURE_VERSION` 0). Any difference stops the lane
and is reported; nothing is written over a state that is not the recorded
one. One device per lane, and no serials, MACs, addresses or host names in the
repository.

| Step | What it writes | Reversible | Decision |
|---|---|---|---|
| HIL-0 | Flash only: our bootloader, partition table at `0x10000`, `tfw_plat` with the image key pin, the app. Secure boot and flash encryption **off**, which needs a bench sdkconfig overlay through `ESP_SDKCONFIG_EXTRA` (the profile turns flash encryption on in release mode, `boards/esp32p4-poe/sdkconfig.defaults`). Layer 0's ES256 check still runs, so the row verifies a signature (m453). | Yes: `make board-restore` | The lane's (m346: flashing is ours) |
| HIL-1 | **Burns the UDS** into a key block, purpose `HMAC_UP`, read-protected. | No | **Owner** |
| HIL-1b | Option (b) only: **burns an ECDSA key block**. | No | **Owner** (not recommended now) |
| HIL-2 | **Enables secure boot v2**: burns the key digest and `SECURE_BOOT_EN`. From then on only a bootloader signed with that key boots; the factory bootloader is unsigned. | No, and it ends the flash restore (below) | **Owner** |
| HIL-3 | **Enables flash encryption**: burns its key and `SPI_BOOT_CRYPT_CNT`. Development mode allows reflashing plaintext a limited number of times; release mode forbids it (ADR, Consequences; profile comment). | No | **Owner**, and separately release vs development mode |
| HIL-4 | **Advances `SECURE_VERSION`** on a confirmed OTA. | No; finite bits | **Owner** (the release policy, 2.3) |
| HIL-5 | Disables download mode or JTAG (`DIS_DOWNLOAD_MODE`, `ENABLE_SECURITY_DOWNLOAD`, `DIS_PAD_JTAG`, `DIS_USB_JTAG`). | No; may end all reflashing | **Owner** |

No ESP-IDF first boot is allowed to burn an eFuse on its own: every image
flashed before an owner decision is built with secure boot and flash
encryption off, so its bootloader cannot burn them at first boot.

**Restore.** `make board-restore BOARD=esp32p4-poe` with the factory image from
the bench host's record directory, checked for size and SHA-256 before the
port opens (`docs/development/bench/esp32-p4.md`, "Restore"). It restores the
flash only and never touches an eFuse. It is a full restore through HIL-0 and
HIL-1 (an unused `HMAC_UP` block does not stop the factory app). After HIL-2
the factory image no longer boots, because its bootloader is unsigned; after
HIL-3 a plaintext image no longer boots. So every burn is recorded on the bench
page before it is made, and the factory restore is declared ended at HIL-2.

## 5. Proposed SPEC row `esp32-p4`

For the coordinator to take to the owner. `[changed]` marks a rewritten
sentence, `[new]` an added one; unmarked sentences are as on main.

```text
| esp32-p4 | in progress | The ESP32-P4 on a PoE Ethernet board with an
ESP32-C6 co-processor, the first MCU onboarded beside the Raspberry Pi nodes.
The firmware core is a HAL and boot-HAL port on ESP-IDF. ESP-IDF's secure boot
v2 (RSA), flash encryption, partition table and OTA are the hardware
implementation behind those HALs, composed and never re-implemented.
[changed] The DICE identity is keyed from an eFuse UDS through the HMAC
peripheral; the DeviceID is ECDSA P-256 (ES256), derived from that UDS through
the HMAC peripheral and never stored, the DS peripheral is not used, and no key
leaves the part in the clear (owner ruling 2026-10-17, CA m499 and m522, #48;
docs/multi-arch/esp32-p4-design.md §2.1).
[new] tfw-boot on this row is layer 0 in ESP-IDF's second-stage bootloader:
secure boot v2 verifies the image's RSA signature, and layer 0 verifies trailer
v1 (board id 0x80) and the SUIT-subset manifest with ES256 inside the signed
image before it derives the CDI (item 23; CA m453).
[new] OTA composes esp_ota and the eFuse secure version: OTA_BEGIN, OTA_DATA and
OTA_END write the inactive slot, OTA_ACTIVATE and OTA_CONFIRM map onto
esp_ota_set_boot_partition and esp_ota_mark_app_valid_cancel_rollback with
layer 0 checking the OTA_AUTH authorization at reset, a trial boots once and
ESP-IDF rolls back an unconfirmed one, and each advance of the eFuse floor is
an owner decision (§2.3).
The device speaks the wire protocol over Ethernet. The C6 stays off: no
firmware of ours runs on it and its radio is never enabled. Per-architecture
code lives only behind the HAL, the boot HAL and the board profiles, and the
board takes the next free id in the ESP32 range (0x80–0xBF).
[changed] The gate is lifted in full (owner ruling 2026-10-18, CA m623):
identity tests run on QEMU esp32c3 and hardware-in-the-loop on the bench host
after its factory state is recorded; every eFuse burn, and enabling secure boot
or flash encryption on the board, is an owner decision; tracked in #48.
| docs/multi-arch/ |
```

## 6. Proposed issues (not filed)

1. **P-256 DeviceID from the HMAC UDS, DS retired** (slice 1), with the shared
   derivation the AVR soft root's P-256 row also needs.
2. **Trailer v1 and ES256 in ESP32 layer 0** (slice 2), including the secure
   boot v2 padding question and the bootloader size.
3. **SUIT manifest gate in ESP32 layer 0** (slice 3).
4. **OTA on `esp_ota`** (slices 4 and 5), closing ADR follow-up 5.
5. **ESP32 provisioning image and entropy source** (slice 6), closing ADR
   follow-up 4.
6. **ESP32-P4 bench HIL-0** (slice 7), closing the first half of ADR follow-up 1.
7. **Seam to go-xkms: a root-type value for an on-die eFuse UDS** in the DICE
   chain (go-xkms item 76), filed against go-xkms, not worked around here.
8. **Owner decisions** for HIL-1 to HIL-5 and the secure-version release
   policy (section 4).
