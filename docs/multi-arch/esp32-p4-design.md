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
| `mk/arch/esp32-targets.mk` | `esp32-build`: evaluates `FW_SRCS` from `mk/firmware.mk`, generates the RSA-3072 secure boot v2 signing key and the ES256 image key into the build directory on first use, pins the image key in `tfw_plat.bin` (`:63-75`), and runs `idf.py` with the source list, board directory and feature defines; then `l0_seal.py` (`ESP_L0_SEAL`, `:22`) appends trailer v1 and the SUIT envelope the board's `suit-emit` builds, and re-signs the image for secure boot v2 (`:93-95`). Under anti-rollback (`ESP_ANTI_ROLLBACK_TARGETS`, `:30`) it refuses an `image_version` above the eFuse field (`:53`) and sets `CONFIG_BOOTLOADER_APP_SECURE_VERSION` from it (`:80`). `PROVISION=1` builds the provisioning image for a tier with `ARCH_PROV_SRCS_<tier>` (`hmac-p256`) and refuses the others (`:55`). `esp32-qemu-test`: builds `esp32-qemu` for `esp32c3` (`hmac-p256` tier) and its `PROVISION=1` image (`:111`) and for `esp32` (`none` tier), builds `wirecheck`, runs `test/integration/esp32/qemu_test.py` on each (`mk/arch/esp32-targets.mk:115-118`). |
| `mk/arch/esp32/CMakeLists.txt`, `main/CMakeLists.txt`, `main/Kconfig.projbuild`, `sdkconfig.defaults` | The ESP-IDF project (ADR decision 1, option 1c). One component registers exactly `TFW_SRCS`. `Kconfig.projbuild` carries `CONFIG_TFW_ETH_STATIC_IP` (ADR follow-up 9). |
| `boot/hal/esp32/bootloader_components/main/CMakeLists.txt` | Layer 0's bootloader component. Its sources: `boot_l0.c`, the gate `l0_gate.c` and the boot HAL, the trailer parser and policy primitives of `boot/`, the board's verify (`ESP_L0_VERIFY`), the key slot, `src/core/suit.c` with `l0_crypto.c` under it, the CRC (section 2.2). |
| `boot/hal/esp32/boot_l0.c` | Replaces `call_start_cpu0` (ADR decision 3, option 2c). `bootloader_init`, partition-table load, `bootloader_utility_get_selected_boot_partition`, then `first_passing` (`:301`), then `bootloader_utility_load_boot_image` (`:344`). `check_slot` (`:205`) runs `esp_image_verify(ESP_IMAGE_VERIFY, …)` (`:215`), layer 0's gate `tfw_l0_gate` (`:226`: trailer, board binding, floor, ES256, SUIT manifest), holds the trailer's `image_version` to `data.secure_version` (`:233`), and on a trial boot reads the activate authorization from `tfw_auth` and checks it (`:244-246`). Only a slot that passes all of them is measured: the image's appended SHA-256, and `derive_cdi` (`:282`) computes `HMAC-SHA256(UDS, measurement)` through `ets_hmac_calculate_message` on the `HMAC_UP` key block. `first_passing` walks ESP-IDF's fallback order under the same gate and gives the loader the first slot that passes; a refused slot is never given to it, and with none passing nothing runs (`:292-300`, `:339-343`). The handoff carries `data.secure_version` as the image version. |
| `boot/hal/esp32/l0_gate.c`, `l0_gate.h`, `l0_crypto.c`, `verify_p256_rom.c`, `verify_digest_soft.c`, `l0_seal.py` | Layer 0's gate. `tfw_l0_gate` (`l0_gate.c:55`) parses trailer v1, permits the board binding (`:80`) and the floor (`:84`), verifies ES256 under the key pinned in `tfw_plat` through the board's `ESP_L0_VERIFY` (`p256-rom` on the P4, its ROM's ECDSA in `verify_p256_rom.c`; `p256-soft` on the QEMU board), then holds the SUIT manifest after the trailer against the image (`tfw_suit_gate`, `:52`); `tfw_l0_authorized` (`:127`) checks an activate authorization. `l0_seal.py` is the build's sealing, which the harness also uses for its mis-sealed images. |
| `boot/hal/esp32/boot_hal_esp32.c` | The boot HAL: flash read and page write on 4096-byte sectors, NVM on the `tfw_plat` partition, the handoff in RTC-retained memory, reset cause, jump and reset. |
| `src/hal/esp32/hal_sys.c` | `app_main` hosts the superloop in one FreeRTOS task; tick, reset, reset-cause decode (`TFW_RESET_SOFTWARE`, `TFW_RESET_PANIC`; ADR follow-up 6). |
| `src/hal/esp32/hal_eth.c`, `hal_uart.c` | The TCP byte link on port 21574 behind `tfw_hal_uart_*` (ADR decision 5); DHCP or the static address. |
| `src/hal/esp32/hal_gpio.c`, `hal_adc.c`, `hal_pwm.c`, `hal_tone.c`, `hal_bus.c` | The peripheral HAL on ESP-IDF's drivers (ADR decision 2). |
| `src/hal/esp32/hal_pulse_rmt.c`, `hal_pulse.c` | `PULSE_IN` and `PULSE_CAPTURE`, one file of the two chosen by the object list (`ESP_PULSE` in the board's `profile.mk`, read by `mk/arch/esp32.mk`). `rmt` on the P4: the RMT receiver times the edges in hardware (ADR follow-up 8); a level held past the timeout ends the capture, and partial receive needs the RX ping-pong the classic `esp32` lacks, so `esp32-build` refuses `rmt` there. `poll`, the default and the QEMU board's: the pin read against `esp_timer`. |
| `src/hal/esp32/hal_eeprom.c` | The EEPROM capability on the `tfw_eeprom` partition, sector read-modify-write (ADR decision 6). |
| `src/hal/esp32/hal_handoff.c` | `tfw_hal_handoff_block`: the RTC-retained handoff to `src/core/handoff.c`. |
| `src/hal/esp32/hal_board.c`, `hal_esp32.h`, `hal_types.h` | Board glue; partition names; the C6 enable held low (ADR, Consequences). |
| `src/hal/esp32/crypto_hmac_p256.c` | The `hmac-p256` provider. `tfw_crypto_init` (`:113-141`) takes the CDI from the handoff (tier `SECURE_ELEMENT`) and derives the DeviceID: the HMAC peripheral computes `HMAC-SHA256(UDS, "tfw-deviceid-v1")` on the `HMAC_UP` key block (`deviceid_seed`, `:76-80`; `deviceid_init`, `:84-111`), and `src/crypto/deviceid_p256.c` (`:125`, `:132`) turns it into a P-256 scalar and point; the scalar is never stored. `tfw_crypto_algorithms` (`:146-150`) reports ECDSA P-256 once a UDS is found; `tfw_crypto_sign` (`:181-199`) signs ES256 with RFC 6979 nonces (`deviceid_p256.c:191`) and `tfw_crypto_pubkey` (`:202-209`) returns the 64-byte point; `tfw_crypto_verify_image` refuses (`:227-231`); `tfw_crypto_random` is `esp_fill_random` (`:153-156`). No DS, no `id_record.h`; `tfw_id` is reserved and not read (`:28-29`). |
| `src/hal/esp32/hal_ota.c` | The OTA session on `esp_ota`, chosen by the object list (`ARCH_OTA_SRCS`): `OTA_BEGIN`/`DATA`/`END`/`STATUS` write the inactive slot (`:112-187`), `esp_ota_end` verifying the whole image at `OTA_END` (`:178`); `OTA_ACTIVATE` (`:290`) after an `OTA_AUTH` writes the authorization to `tfw_auth` (`:254-274`); `OTA_CONFIRM` and `OTA_INFO` (`:327`, `:363`). |
| `src/hal/esp32/prov_uds.c` | The part HAL of `src/caps/provision_uds.c` in the `PROVISION=1` image: the UDS from the noise source (`:46`), its burn as the read- and write-protected `HMAC_UP` key (`:58`), and the HMAC over it (`:88`). |
| `boards/esp32p4-poe/` | `board_profile.h`, `board.c`, `profile.mk` (`ESP_TARGET := esp32p4`, `BOARD_TIERS := hmac-p256` at `profile.mk:13`, the factory flash size and SHA-256), `partitions.csv` (`tfw_plat` `0x11000`, `tfw_id` `0x12000`, `tfw_eeprom` `0x13000`, `otadata` `0x14000`, `tfw_auth` `0x16000`, `ota_0` `0x20000` and `ota_1` `0x620000`, 6 MB each), `sdkconfig.defaults` (EMAC and IP101, 32 MB flash since slice 7, flash encryption in release mode). |
| `boards/esp32-qemu/` | The QEMU board, `0x81`, for both machines (ADR decision 8), with `sdkconfig.defaults.esp32` for the Xtensa run. |
| `test/integration/esp32/qemu_test.py`, `wirecheck/` | The QEMU harness, its steps in the module docstring (`qemu_test.py:20-83`): a fixed UDS pinned to its DeviceID point (`:141-142`, derived on the host by `deviceid_point`, `:237`), which `provision_efuses` (`:315`) burns as `HMAC_UP` in KEY1 (`:323`) with the secure boot digest through QEMU's download mode, and no DS key; `boot` and `boot_facts` (`:388`, `:409`) read layer 0's and the provider's log lines; `release` (`:493`) seals the OTA releases with `l0_seal.py`, which also builds step 6's mis-sealed images; `ota` (`:400`) bounds every OTA stream by the image's length; `provisioning` (`:435`) and `read_back` (`:358`) run the `PROVISION=1` image. `wirecheck` drives `sdk/go` over the emulated Ethernet: CAPS, `ATTEST` against `-deviceid`, the EEPROM mark, `-ota`, `-activate`, `-confirm`, `-info`, `-genkey`. |
| `test/unit/test_board_esp32p4-poe.c`, `test_board_esp32-qemu.c` | Host unit tests of the two board profiles, in `make test`. |
| `test/unit/test_esp_crypto_hmac_p256.c` | The `hmac-p256` provider on the host, over the mock HMAC peripheral, eFuse purpose lookup and RNG in `test/mock/esp32/`: the DeviceID point and ES256 signature pinned to `test_deviceid_p256`'s literals. The row's requirements, met or partial: [`esp32-p4-engineering.md`](esp32-p4-engineering.md). |
| `test/unit/test_esp_board.c` | `hal_board.c` against the P4's own profile, over the mock gpio driver in `test/mock/esp32/driver/`: the C6 enable is driven low before it is an output, pulled down and held, never driven high, and a refused step stops the init. |
| `docker/Dockerfile.esp32`, the `esp32-build` and `esp32-qemu-test` CI jobs | ESP-IDF v5.5.5 and Espressif QEMU; `esp32-build` runs `make esp32-build BOARD=esp32p4-poe`, `esp32-qemu-test` runs `make esp32-qemu-test` on a push to main and nightly only (ruling 220(j); ADR, Consequences, last bullet). |

### 1.2 What `make esp32-qemu-test` proves today

From the ADR's Consequences, first bullet, and the harness above, on each
machine with keys generated at test time:

- the ROM verifies the bootloader and the bootloader the app (secure boot v2,
  RSA-PSS); layer 0's measurement equals the image's own SHA-256;
- on `esp32c3`, the CDI fingerprint equals `HMAC-SHA256(UDS, measurement)`
  computed on the host, the DeviceID fingerprint the provider logs
  (`crypto_hmac_p256.c:110`) is that of the P-256 point the host derives from
  the UDS (`qemu_test.py:237`, pinned at `:142`), and `ATTEST` verifies as
  ES256 under that point (`qemu_test.py:563`, `wirecheck -deviceid`);
- all three survive a soft reset and a cold start, and an EEPROM mark written
  over the wire survives both;
- `sdk/go` answers PING, CAPS (board `0x81`, the expected tier) and SYS_INFO
  over the emulated Ethernet;
- an image with one byte flipped and its checksum and digest fixed up is
  refused: no measurement, no wire;
- layer 0's gate (section 2.2) refuses, with no measurement and no wire, an
  image sealed for another board, signed by an ES256 key the device was not
  paired with, with its measurement or ES256 signature flipped, with a SUIT
  manifest byte flipped, or, under anti-rollback on the `esp32c3`, with a
  trailer version other than its `secure_version`;
- the OTA session (section 2.3): a tampered image streamed over
  `OTA_BEGIN`/`DATA` fails at `OTA_END`, the image itself is taken and lands in
  the inactive slot only, and `OTA_ACTIVATE` without `OTA_AUTH` is refused;
- on the `esp32c3`, an activate authorized by a key the device was not paired
  with is refused at reset and ota_0 runs; an authorized but unconfirmed trial
  rolls back at its deadline; a confirmed release stays, its secure version
  in `OTA_INFO` and QEMU's file-backed eFuse floor raised;
- on the `esp32c3`, the `PROVISION=1` image (section 2.4) burns a UDS from the
  noise source and returns its DeviceID point, refuses a second burn, leaves
  the ROM in Secure Download Mode, and the eFuse file read offline holds one
  protected `HMAC_UP` key that derives the point; the next normal boot
  attests under it.

It does **not** prove anything on the P4 itself: the P4 image is built in CI
but runs nowhere (slice 7).

### 1.3 Gaps found on main (docs and data, for the slices)

- **The DeviceID was RSA in DS** (ADR decision 4), against the owner ruling
  of 2026-10-17 (CA m499, m522): ECDSA P-256 on every board, ES256 only.
  Closed by slice 1: the DeviceID is P-256, derived from the UDS through the
  HMAC peripheral (`src/hal/esp32/crypto_hmac_p256.c:84-111`). Section 2.1.
- **The SPEC row `esp32-p4`** said "the DeviceID key is held by the DS
  peripheral" and, in the same sentence, quoted the ruling that there is no RSA
  DeviceID (`docs/SPEC.md` row `esp32-p4`). DS is an RSA engine, so the row
  contradicted itself. Closed: the row now says the P-256 DeviceID is derived
  from the UDS and never stored (`hmac-p256`), and that DS is not used.
- **Flash size.** The bench record reads a 32 MB part
  (`docs/development/bench/esp32-p4.md`, first table); the profile builds for
  16 MB (`boards/esp32p4-poe/sdkconfig.defaults`, `partitions.csv` header). A
  16 MB image runs on a 32 MB part; the slots could be larger. Settled by slice
  7: the profile builds for 32 MB and the slots stay below 16 MB, where ESP-IDF
  v5.5.5 maps an app on quad flash (bench page, "Pins").
- **Stale comment.** `boards/esp32p4-poe/profile.mk` says "the board has not
  arrived"; it is on the bench (`docs/development/bench/esp32-p4.md`). Fixed by
  slice 7, with `board_profile.h`'s, whose pins were the reference design's and
  are now checked against the board's schematic.
- **The soft P-256 DeviceID is built (#48, #130).** The AVR soft provider
  (`src/crypto/soft/provider.c`) delegates `tfw_crypto_sign` and
  `tfw_crypto_pubkey` to the identity its object list picks
  (`src/crypto/soft/id.h`). `SOFT_ID=p256`, the ATmega2560 soft and auto
  builds and tfwsim, links `id_p256.c`: a P-256 DeviceID whose seed is
  HMAC(UDS, `TFW_DEVICEID_LABEL`), signing ES256 (alg id `0x01`) over micro-ecc
  (`src/third_party/micro-ecc/`). `SOFT_ID=hmac`, the ATmega328P and
  ATmega32U4, links `id_hmac.c` and keeps the HMAC-SHA256 signer, with no
  public key. On a device the UDS store (`uds_flash.c`) still returns none, so
  a soft-rooted board answers `NOT_PROVISIONED` until it lands.
- **The manifest gate had no caller.** `tfw_suit_gate` (`src/core/suit.c`) was
  called by no file in `boot/` or `src/` (`docs/SPEC.md` row `ota`, "The
  manifest gate is built but nothing calls it yet"). Slice 3 makes ESP32
  layer 0 its first caller (`boot/hal/esp32/l0_gate.c`, section 2.2).

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
"Verifying an image")**. The signed digest covers the padding up to the
signature block: `espsecure.py sign_data --version 2` pads the image to the
4 KB boundary with `0xFF` and signs all of it, and the bootloader's
`verify_secure_boot_signature` (`bootloader_support/src/esp_image_format.c`)
hashes the same padding before it checks the signature. Neither file is in the
repo or on the build host of slice 2, so the citation is by function, and
`make esp32-qemu-test` checks it on both machines: one trailer byte flipped and
nothing re-signed is refused by secure boot v2
(`test/integration/esp32/qemu_test.py`, step 5).

The build (`mk/arch/esp32-targets.mk`, `boot/hal/esp32/l0_seal.py`) appends
trailer v1 after the appended SHA-256 and re-signs with `espsecure.py`, so the
trailer lies inside the RSA-signed span, and the SUIT envelope after the
trailer. The image to flash is `build/esp32/<board>-<target>-<tier>/trusted-firmware.bin`
with `tfw_plat.bin` beside it, the platform block carrying the image key's pin
(`tfwprov --bake-eeprom`); the `idf/` image under it has no trailer, and layer 0
refuses it. ESP-IDF builds a signed app with `--secure-pad-v2`, which pads with a
zero segment so the SHA-256 ends exactly on a 64 KB boundary -- where the
bootloader looks for the signature sector, leaving no room. The seal grows that
zero segment until the SHA-256 ends 16 bytes past a 4 KB boundary, recomputes
the SHA-256 (zeros leave the XOR checksum alone), and refuses an image whose
last segment is not that zero segment. That leaves 4080 bytes for the trailer
and the envelope, and moves the signature sector one 4 KB sector off the
64 KB boundary **(to confirm on the bench: that nothing but the alignment
depends on it)**. The trailer's `image_len` is ESP-IDF's image length up to
the appended digest, so the trailer's `measurement` equals
`esp_image_metadata_t.image_digest`: the measurement, the CDI and the trailer
name the same 32 bytes, and layer 0 hashes nothing twice. Layer 0 finds the
trailer from the segment headers `esp_image_verify` returned, not from
`image_len`.

**Where the envelope sits.** Right after trailer v1's 128 bytes: a `u16`
little-endian length, then that many bytes of SUIT envelope, the profile of
`src/core/suit.h` exactly. The rest of the 4080 bytes is the seal's `0xFF`
padding up to the signature sector, so the envelope is inside the span secure
boot v2 signs. The length is at most `TFW_SUIT_ENVELOPE_MAX` (320); 0, a length
past it or past the slot, and erased flash's `0xFFFF` are refused as
`TFW_ERR_RANGE` before a byte is parsed. `l0_seal.py` builds the envelope with
the board's `suit-emit` (`mk/suit.mk`), so the vendor and class ids are the ones
the gate compares against: the manifest names `image_digest` (the trailer's
`measurement`), `image_len` and the trailer's `image_version` as its sequence
number, and its Sig_structure is signed ES256 with the build's image key, the
key in the bootloader's slot that signs the trailer. No signing code is in
`suit-emit`; the seal signs, as it signs the trailer.

**The order in layer 0**, before `bootloader_utility_load_boot_image`:

1. `esp_image_verify`: secure boot v2 and the image digest (on main).
2. `tfw_boot_trailer_parse` right after the appended digest.
3. `tfw_boot_binding_permit` with `TFW_BOARD_ID` (`0x80` or `0x81`).
4. `tfw_boot_rollback_permit` against the floor (2.3).
5. The trailer's `measurement` against `image_digest`, constant-time
   (`tfw_boot_equal`).
6. The key pin: SHA-256 of the image key against the hash in `tfw_plat`'s
   platform block, parsed with its CRC (`tfw_boot_platform_parse`).
   `tfw_boot_check_pubkey_pin` asks the backend for a 32-byte key, which
   p256-soft does not hold, so the gate hashes the 64-byte slot itself.
7. `tfw_boot_verify` (ES256) over the trailer's signed span.
8. `tfw_suit_gate` over the envelope after the trailer: its signature with the
   same key (through `tfw_crypto_verify_image`, the verify's digest entry), the
   vendor, the board's class id and the trailer's board id, the image digest
   against `image_digest` and the trailer, the size against `image_len` and the
   trailer, the sequence number against the trailer's `image_version`, so a
   manifest below the floor is refused with the trailer. Every boot, not only
   after an update.
9. Before activation of a new slot, the `OTA_ACTIVATE` authorization (2.3).
10. Only then `derive_cdi`. A refusal at any step hands off no tier, as a
   failed `esp_image_verify` does today, and the slot is refused (2.3): it is
   never given to the loader, the next slot in ESP-IDF's fallback order is
   gated instead, and with none passing nothing runs.

Steps 2 to 8 are `tfw_l0_gate` (`boot/hal/esp32/l0_gate.c`), with the host
tests in `test/unit/boot/test_boot_l0_esp32.c`. Step 9 is slice 5's
`tfw_l0_authorized` (`l0_gate.c`), which `boot/hal/esp32/boot_l0.c` calls on a
trial boot alone, after `tfw_l0_gate`, with the record `tfw_l0_auth_read`
(`boot_hal_esp32.c`) reads from `tfw_auth`; step 10 follows both. The
floor in step 4 is `esp_efuse_read_secure_version()`. Tying the trailer's
`image_version` to the app descriptor's `secure_version` needs
`CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK`; slice 5 turns it on for the C3 QEMU
machine only (2.3, "Anti-rollback and `secure_version`").

**Reused unchanged:** `boot/boot_image.c` (`tfw_boot_trailer_parse`,
`tfw_boot_binding_permit`, `tfw_boot_rollback_permit`),
`boot/verify/verify_p256_soft.c` and `boot/verify/p256_key_slot.c`,
`src/core/suit.c`
(`tfw_suit_read`, `tfw_suit_gate`), joined by the bootloader's source list, and
`boot/boot_handoff.c`. Under `suit.c`, `boot/hal/esp32/l0_crypto.c` gives
`tfw_crypto_sha256` as the bootloader SHA, and the digest entry
`tfw_crypto_verify_image` is `verify_p256_rom.c`'s on the P4 and
`boot/hal/esp32/verify_digest_soft.c` beside `verify_p256_soft.c` on the C3 and
the ESP32, which leaves the AVR file as it is. **Changed:**
`boot/boot_policy.c`'s `tfw_boot_evaluate_at` derives the trailer address from
the fixed `TFW_BOOT_TRAILER_ADDR` and measures the image itself; the ESP32
trailer follows the image, and ESP-IDF has measured it. Slice 2 took the
separate ESP32 evaluation, `boot/hal/esp32/l0_gate.c`, which calls the same
primitives, so `boot_policy.c` is unchanged for AVR. The bootloader compiles
the slot from the build's image key (`l0_seal.py keyslot`), since an RSA-signed
bootloader cannot be patched afterwards. The curve arithmetic and the hash are
the bootloader's own, not copies of ours. The hash is its bootloader SHA-256
(`boot/hal/esp32/measure_esp32.c`, whole words only, as the classic ESP32's
peripheral takes them). The verify is the board's `ESP_L0_VERIFY`
(`boards/*/profile.mk`). The P4 takes `p256-rom`: the ROM's ECDSA through
ESP-IDF's `ets_ecdsa_verify` (`esp32p4/rom/ecdsa.h:24`), which the P4 build
links as the patch in `esp_rom/patches/esp_rom_ecdsa.c:92` (it range-checks r
and s, then calls the ROM); `boot/hal/esp32/verify_p256_rom.c` reverses our
big-endian X, Y, r and s into the little-endian halves the ROM reads
(`esp32p4/rom/secure_boot.h:100,102`), and micro-ecc is not linked. The C3 and
the ESP32 have no ROM ECDSA and take `p256-soft`, `verify_p256_soft.c` over
ESP-IDF's `micro-ecc` component. Both verify the same signature over the same
SHA-256, and `test_boot_l0_esp32` runs the gate against each. ESP-IDF's
bootloader linker script places micro-ecc and the SHA in `iram_loader_seg`.
Our `src/third_party/micro-ecc/` and `src/crypto/sha256_small.c` landed in
`iram_seg`, the small region that takes every `.text` the script does not
name, and the calls from it into `iram_loader_seg` stopped linking
(`R_RISCV_RVC_JUMP` truncated). **Not reused:** the AVR copy, swap and session code (`boot_swap.c`,
`boot_store.c`, `boot_session.c`, `boot_stage.c`), replaced by ESP-IDF's OTA
(2.3).

**Cost.** Bootloader flash: the verify (micro-ecc on the C3 and the ESP32, the
ROM's on the P4), the trailer parser, the SUIT reader and SHA-256 join layer 0. The bootloader with layer 0 is 44–52 KiB today and the
partition table was moved to `0x10000` for it (ADR decision 6); the P4's
bootloader starts at `0x2000`, so 56 KiB is the ceiling before the table moves
again. Slice 2 adds the gate; the measurement is `ls -l
build/esp32/<board>-<target>-<tier>/idf/bootloader/bootloader.bin` after
`make esp32-build`, to be taken where ESP-IDF is installed; a move to `0x18000` shifts `tfw_plat`, which is a
reflash, not a migration. The tighter bound is the P4's `iram_loader_seg`,
0x7000 bytes: with micro-ecc its `.iram_loader.text` was 30318 bytes, 1646
over even with micro-ecc cut to P-256 at optimisation level 0, and micro-ecc's
one object was 3602 of them. The ROM verify takes that object out; the patch
and our backend land in `iram_seg` instead. Both are to be measured on the
next P4 build. Boot time: one ES256 verify in the bootloader per boot, to be
measured (QEMU first, then the bench). eFuse: none; the image key's
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
| `OTA_BEGIN` (`0x68`) | `esp_ota_get_next_update_partition`, then `esp_ota_begin` with `OTA_WITH_SEQUENTIAL_WRITES`, so the slot is erased sector by sector as `OTA_DATA` reaches it and no command waits on a 6 MB erase (confirmed, slice 4: ESP-IDF v5.5.5 `esp_ota_ops.h`). `TFW_ERR_BAD_STATE` (19) while an image is pending, as on AVR. |
| `OTA_DATA` (`0x69`) | `esp_ota_write` in order; an out-of-order offset is refused, as the AVR stager does. |
| `OTA_END` (`0x6A`) | `esp_ota_end`. The wire meaning stays "every byte arrived, nothing verified" (§8): `esp_ota_end` also runs ESP-IDF's image check, so on the P4 a bad image can already fail here, with the existing verify-failure status. A stronger refusal, not a different meaning; stated in `docs/ota/api.md` by the OTA slice. |
| `OTA_STATUS` (`0x6B`) | The session's own counters, as on AVR. |
| `OTA_INFO` (`0x6C`) | `esp_ota_get_running_partition`, `esp_ota_get_state_partition` and `esp_ota_get_partition_description` for each slot; `floor` is the eFuse secure version; `state` maps `ESP_OTA_IMG_PENDING_VERIFY` to `TRIAL`; `flags` bit 0 from the bootloader's rollback. |
| `OTA_AUTH` (`0x6F`) | Holds the 64-byte ES256 signature (r‖s over the COSE_Sign1 Sig_structure, `boot/boot_cose.c`) for the next command only (`src/core/ota_auth.c`, `docs/SPEC.md` row `ota`, CA m502 item 2). `OTA_ACTIVATE` writes it, in the `TFW_BOOT_AUTH_*` record (`include/tfw/boot.h`), to the `tfw_auth` partition for layer 0, not to `tfw_plat`: slice 5's row says why. |
| `OTA_ACTIVATE` (`0x6D`) | After `OTA_AUTH`, on an ended image and not on trial: the application checks the trailer as layer 0 will (`tfw_boot_trailer_parse`, the board binding, the eFuse floor, `image_len` and the measurement against `esp_image_get_metadata` and the appended digest, and under anti-rollback `image_version` = the app's `secure_version`), writes the `tfw_auth` record, then `esp_ota_set_boot_partition`; mode 1 resets (`src/hal/esp32/hal_ota.c`). Each refusal is layer 0's status for the same fault. At that reset layer 0 checks the record with the image key, as tfw-boot does on every tier (row `ota`, #137), after the trailer and the SUIT manifest and before the CDI (2.2, step 9). Refused: layer 0 gives no CDI and the fallback boots the previous slot; at the next reset ESP-IDF finds the entry still `PENDING_VERIFY` and marks it `ABORTED` (confirmed, ESP-IDF v5.5.5 `bootloader_utility.c` 378–629; `boot/hal/esp32/boot_l0.c`). |
| `OTA_CONFIRM` (`0x6E`) | After `OTA_AUTH`, in `TRIAL` only: `esp_ota_mark_app_valid_cancel_rollback`. As on AVR (#125) the authorization is required and not verified by the application. Under anti-rollback ESP-IDF's confirm raises the eFuse floor to the image's secure version (`esp_ota_ops.c` 903–948): ESP-IDF's write, not ours (below). |

**Trial boot and rollback.** With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, a new
slot boots once in `ESP_OTA_IMG_PENDING_VERIFY`; if the app has not marked it
valid before the next reset the bootloader marks it aborted and boots the
previous slot. That is the one trial boot of owner ruling 16:15Z (Q9: a failure
means another OTA round). The per-board trial deadline is the application's,
as on AVR (row `ota`, "What runs today"); at the deadline the app resets
without confirming, so ESP-IDF rolls back. Layer 0 keeps
`bootloader_utility_get_selected_boot_partition` as the slot chooser, so the
rollback state machine stays ESP-IDF's (L2).

**Anti-rollback and `secure_version`.** The ESP32 floor is the eFuse secure
version through ESP-IDF's anti-rollback (`docs/ota/adr-ota-image-format-and-trust.md`,
the floor table: "one-way bits; a small fixed number of steps for the part's
life"). Where a board turns it on (`ESP_ANTI_ROLLBACK_TARGETS` in its
`profile.mk`), the build sets `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` and
`CONFIG_BOOTLOADER_APP_SECURE_VERSION` from the trailer's `image_version`
(`mk/arch/esp32-targets.mk`, which refuses a version past the 16-bit field),
and layer 0 refuses an image whose two values differ
(`boot/hal/esp32/boot_l0.c`), so one fact has one value. ESP-IDF advances the
eFuse when a confirmed image carries a higher secure version. **Each advance
burns eFuse bits and the supply is finite**, so today only `esp32-qemu`'s C3
machine turns it on, where the eFuse is a file QEMU writes. The P4 board does
not: its rollback runs, its floor does not, and nothing is burned on any chip
until the owner sets the release policy (which releases advance it, section 4).

**Geometry (ADR follow-up 7).** `esp_ota` owns the slot writes, so the AVR
page and counter widths never reach flash on the P4. What remains is the wire:
`OTA_BEGIN`'s size and `OTA_DATA`'s offset must address a 6 MB slot. Confirmed
by slice 4: both are little-endian u32 in `docs/ota/api.md` §Wire and in the
Go SDK, reaching 4 GiB, so `0x600000` fits with no wire change.

**`ARCH_UPDATE_REQUEST := none`** and `RESET(1)` → `UNSUPPORTED` (ADR,
Consequences) **stay** (slice 5, accepted by the coordinator). An earlier
draft of this page dropped them with `OTA_ACTIVATE`. They are kept because
layer 0 never reads a boot request: on the AVR, `RESET(1)` arms the request
tfw-boot reads (`src/caps/sys.c`, `tfw_update_request_arm`). On the ESP32 the
update path is `OTA_ACTIVATE` and ESP-IDF's `otadata`, so an armed request
would be a record nothing reads. `RESET(1)` answering `UNSUPPORTED` says that
truthfully.

### 2.4 Entropy and provisioning (ADR follow-ups 4 and 7)

The brief numbers these 4 and 7; follow-up 7 in the ADR is the boot geometry,
answered in 2.3. Provisioning is follow-up 4.

**What must be settled before any key is generated on the part:**

1. **Entropy source.** `tfw_crypto_random` is `esp_fill_random`, true random
   only while a noise source runs (ADR, Consequences, "Entropy"). The P4 board
   runs no radio: the C6, the board's radio, is held in reset from the first
   instruction (ADR, Consequences, "The C6 co-processor"). **Confirmed
   against ESP-IDF v5.5.5:** the second-stage bootloader turns the source
   *off* before it jumps to the app, unconditionally
   (`components/bootloader_support/src/bootloader_utility.c:792`,
   `bootloader_random_disable()`), so the app starts with none running, the
   P4 included, whatever `docs/en/api-reference/system/random.rst:72` says of
   the SAR ADC staying on; output is true random only while a source runs
   (`random.rst:41-42`). The provisioning draw therefore turns the source on
   itself, for exactly its own length: `bootloader_random_enable()` starts
   the SAR ADC noise source and, on the P4 from revision 3.0, the TRNG beside
   it (`components/bootloader_support/src/bootloader_random_esp32p4.c:26`,
   `:69`), and `bootloader_random_disable()` (`:73`) stops it. The call is
   unsafe while another driver holds the ADC
   (`components/bootloader_support/include/bootloader_random.h:33`), so the
   draw first takes the ADC1 lock (`adc_lock_try_acquire`,
   `components/esp_hw_support/include/esp_private/adc_share_hw_ctrl.h:122`)
   and a held lock answers `BUSY`, never a weaker draw. The path is
   `tfw_prov_uds_random` in `src/hal/esp32/prov_uds.c`. With option (a) and
   RFC 6979 signing, the application needs entropy only for `AUTH_CHALLENGE`
   nonces; key generation happens once, at provisioning.
2. **Who generates the UDS.** The UDS is the `HMAC_UP` key. Burned from the
   host, as the QEMU harness does today, it was in the clear on the host,
   which breaks "no key leaves the part in the clear" for a real part. On the
   part: a provisioning image (`PROVISION=1` on the `hmac-p256` tier)
   generates 32 bytes under the settled source, burns them with
   `esp_efuse_write_key` into a free block with purpose `HMAC_UP`, read- and
   write-protected (`components/efuse/src/efuse_controller/keys/with_key_purposes/esp_efuse_api_key.c:270`
   sets the purpose, read protection for `HMAC_UP` and write protection in
   one batch), confirms both protections before it answers, and never
   returns them. A free block is `esp_efuse_find_unused_key_block()`; none
   is `NO_SLOT`, a failed or unconfirmed burn `CRYPTO`, a second
   `PROV_GENKEY` `PROTECTED_REGION`, all before any draw.
3. **What the wire returns.** `PROV_GENKEY` returns a 64-byte point (ADR
   follow-up 4), which is exactly the option (a) DeviceID public key. The RSA
   modulus problem of follow-up 4 is gone with the RSA DeviceID.
4. **Which key blocks.** One for the UDS; secure boot v2's digest block(s);
   flash encryption's key. No DS block. Option (b) would add one.
5. **The QEMU path keeps host-generated keys**, because the harness must compute
   the expected CDI and DeviceID; the on-part path is tested on QEMU for its
   wire and its refusals (a second `PROV_GENKEY` refused, no UDS readable)
   and for the one thing it can be held to without knowing the UDS: the next
   normal boot's DeviceID is the point `PROV_GENKEY` returned.

## 3. Slices

Each is one lane (≤300 turns). Each slice's gate is `make esp32-qemu-test`
(both machines) plus `make ci` for any shared file, unless it says the bench.

| # | Slice | Files | Gate | Proves |
|---|---|---|---|---|
| 1 | **P-256 DeviceID from the HMAC UDS; DS retired.** **First slice QEMU alone verifies.** | `src/hal/esp32/crypto_hmac_ds.c` → `src/hal/esp32/crypto_hmac_p256.c`; `src/hal/esp32/id_record.h` removed; `src/crypto/deviceid_p256.c` (the portable DeviceID derivation, shared with the AVR soft root, section 2.1) and `test/unit/test_deviceid_p256.c`; `mk/arch/esp32.mk` and `mk/arch/esp32/main/CMakeLists.txt` (the `hmac-p256` tier and micro-ecc); `boards/*esp32*/profile.mk` and `board_profile.h` (tier name); `sdk/go/commands.go`; `test/integration/esp32/qemu_test.py` and `wirecheck` (`-deviceid`, `ATTEST` as ES256) | `make esp32-qemu-test` on `esp32c3`; `make ci` | The DeviceID point equals the host's derivation from the UDS, is the same across soft reset and cold start, `SIGN` verifies as ES256 on the host, CAPS reports P-256 and `ATTEST` on; no KEY2 burned. **Landed (#48):** `crypto_hmac_ds.c` and `id_record.h` removed, `src/hal/esp32/crypto_hmac_p256.c` added; `src/crypto/deviceid_p256.c` and `deviceid_p256.h` with `test/unit/test_deviceid_p256.c` (in `mk/host.mk`); the `hmac-p256` tier in `mk/arch/esp32.mk`, `mk/arch/esp32-targets.mk` and `mk/arch/esp32/main/CMakeLists.txt`; both ESP32 boards' `profile.mk` and `board_profile.h`, `test/unit/test_board_esp32p4-poe.c`; `sdk/go/commands.go`; `qemu_test.py` and `wirecheck` (`-deviceid`). |
| 2 | **Trailer v1 and ES256 in layer 0.** | `boot/hal/esp32/l0_gate.c` and `l0_gate.h` (the trailer and ES256 gate), called from `boot/hal/esp32/boot_l0.c`; `boot/hal/esp32/verify_p256_rom.c` (the P4 ROM's ECDSA); `boot/hal/esp32/bootloader_components/main/CMakeLists.txt`; `boot/hal/esp32/l0_seal.py` and `mk/arch/esp32-targets.mk` (trailer append and sign before `espsecure.py`, the key pin baked into `tfw_plat`); `mk/boot.mk`, `mk/host.mk`; `test/unit/boot/test_boot_l0_esp32.c`; `test/integration/esp32/qemu_test.py` steps 4–6 | `make esp32-qemu-test`, both machines | Images for another board, with board id 0, with a flipped trailer signature, or with `image_version` ≠ `secure_version` are refused with no CDI; a good image boots with the same measurement as before; bootloader size and boot time measured. **Landed (#48):** `boot/hal/esp32/l0_gate.c` and `l0_gate.h`, `measure_esp32.c`, `verify_p256_rom.c`, `l0_seal.py`, `boot_l0.c` and the bootloader `CMakeLists.txt`; `mk/arch/esp32-targets.mk`, `mk/boot.mk`, `mk/host.mk`; both ESP32 boards' `profile.mk` (`ESP_L0_VERIFY`); `test/unit/boot/test_boot_l0_esp32.c` and `test_boot_p256_rom.c` over `test/mock/esp_rom_ecdsa_mock.c` and `test/mock/rom/ecdsa.h`; `qemu_test.py` steps 5–6. |
| 3 | **The SUIT manifest gate in layer 0.** | `boot/hal/esp32/l0_gate.c` and `l0_gate.h` (`tfw_l0_gate` holds the manifest after the trailer against the image), `boot/hal/esp32/l0_crypto.c`, `boot/hal/esp32/verify_digest_soft.c`; `src/core/suit.h`; `mk/boot.mk` and the bootloader `CMakeLists.txt`; `boot/hal/esp32/l0_seal.py --suit-emit`, `tools/suit-emit/main.c` and `mk/arch/esp32-targets.mk` (the envelope sealed after the trailer); `boards/*esp32*/board_profile.h` (SUIT class ids); `test/unit/boot/test_boot_l0_esp32.c`; `test/integration/esp32/qemu_test.py` step 6 (`manifest-flip`) | `make esp32-qemu-test` | `tfw_suit_gate` refuses a manifest for another digest, board or a lower sequence number, before activation; its first caller on any board. **Landed (#48):** `boot/hal/esp32/l0_gate.c` and `l0_gate.h`, `l0_crypto.c`, `verify_digest_soft.c`, `verify_p256_rom.c`, `l0_seal.py` and the bootloader `CMakeLists.txt`; `src/core/suit.h`; `tools/suit-emit/main.c`; `mk/arch/esp32-targets.mk`, `mk/boot.mk`; both ESP32 boards' `board_profile.h` (SUIT class ids); `test/unit/boot/test_boot_l0_esp32.c`; `qemu_test.py` step 6 (`manifest-flip`). |
| 4 | **OTA session on `esp_ota`.** | `src/hal/esp32/hal_ota.c`, chosen by the object list (`ARCH_OTA_SRCS` in `mk/arch/esp32.mk`, item 20); `mk/firmware.mk`, `include/tfw/config.h`, `src/core/commands.def`; `mk/arch/esp32/main/CMakeLists.txt`; `test/mock/esp_ota_mock.c` and `test/unit/test_esp_ota.c`; `wirecheck -ota` streaming over Ethernet; `docs/ota/api.md`, `docs/ota/engineering.md`; `test/integration/esp32/qemu_test.py` step 7 | `make esp32-qemu-test` on `esp32c3` | `OTA_BEGIN`/`OTA_DATA`/`OTA_END`/`OTA_STATUS`/`OTA_INFO` write and report the inactive slot; a bad image fails at `OTA_END`; nothing activates. **Landed (#48):** `src/hal/esp32/hal_ota.c`, `hal_esp32.h`, `hal_uart.c` (a live host keeps the wire); `mk/arch/esp32.mk`, `mk/arch/esp32-targets.mk`, `mk/arch/esp32/main/CMakeLists.txt`, `mk/firmware.mk`, `mk/host.mk`, `include/tfw/config.h`, `src/core/commands.def`; `test/mock/esp_ota_mock.c`, `test/mock/lwip_mock.c` and the ESP-IDF headers under `test/mock/esp32/`, `test/unit/test_esp_ota.c` and `test_esp_wire.c`; `wirecheck -ota`; `docs/ota/api.md`, `docs/ota/engineering.md`, the ADR; `qemu_test.py` step 7. |
| 5 | **`OTA_AUTH`, activate, trial, confirm, rollback, floor.** The authorization lives in its own `tfw_auth` partition (`boards/*esp32*/partitions.csv`, `0x16000`, one sector), **not in `tfw_plat`**, for two reasons. First, `tfw_plat` is read-only to the application, written once by the provisioning jig (`src/hal/esp32/hal_eeprom.c` 12–15; ADR `adr-esp-idf-integration.md` 156). Second, it is one 4 KB encrypted sector (`partitions.csv` line 7) holding the image key's pin, and the only write this port has, `tfw_esp_part_write` (`hal_eeprom.c` 51–81), erases and rewrites the whole sector. A power loss during an activate would lose the pin, and layer 0 would then refuse every image. A torn `tfw_auth` loses only that activate's record: layer 0 refuses the trial and the previous image boots. | `src/hal/esp32/hal_ota.c`; `boot/hal/esp32/boot_l0.c` (the authorization at reset); `mk/arch/esp32/sdkconfig.defaults` (rollback), `boards/esp32-qemu/profile.mk` (`ESP_ANTI_ROLLBACK_TARGETS := esp32c3`, never the P4), `mk/arch/esp32-targets.mk` (`CONFIG_BOOTLOADER_APP_SECURE_VERSION` from the trailer's `image_version`); `test/unit/test_esp_ota.c`; `test/integration/esp32/wirecheck` (`-activate`, `-confirm`, `-info`), `test/integration/esp32/qemu_test.py` steps 8–10 | `make esp32-qemu-test` on `esp32c3` (QEMU's file-backed eFuse holds the secure version; no eFuse is burned on any board, and which releases advance the floor is the owner's decision, section 4) | An unauthorized activate is refused at reset; an unconfirmed trial rolls back by itself; a confirmed image stays, with its secure version in `OTA_INFO`. `RESET(1)` keeps answering `UNSUPPORTED` (2.3). **Landed (#48):** `src/hal/esp32/hal_ota.c`, `hal_esp32.h`, `hal_sys.c`; `boot/hal/esp32/boot_l0.c`, `boot_hal_esp32.c`, `l0_gate.c` and `l0_gate.h`, `verify_p256_rom.c`; `boot/boot_cose.c`, `boot/boot_stage.c`, `boot/boot_verify.h`, `boot/verify/verify_p256_soft.c`, `include/tfw/boot.h`; `src/core/ota_trial.c` and `ota_trial.h`, `ota_trial_block.c`; both ESP32 boards' `partitions.csv` (`tfw_auth`) and `board_profile.h`, `boards/esp32-qemu/profile.mk`; `mk/arch/esp32/sdkconfig.defaults`, `mk/arch/esp32.mk`, `mk/arch/esp32-targets.mk`, `mk/boot.mk`, `mk/firmware.mk`, `mk/host.mk`, `include/tfw/config.h`, `src/core/commands.def`; the ESP-IDF headers under `test/mock/esp32/`, `test/mock/esp_ota_mock.c`, `test/unit/test_esp_ota.c`, `test/unit/boot/test_boot_l0_esp32.c`; `wirecheck` (`-activate`, `-confirm`, `-info`) and `wirecheck/main_test.go`; `docs/SPEC.md`, `docs/ota/api.md`, the ADR; `qemu_test.py` steps 8–10. |
| 6 | **Provisioning image and entropy.** | `src/caps/provision.c`, `src/caps/provision_atecc.c`, `src/caps/provision_uds.c` and `provision_uds.h`, `src/hal/esp32/prov_uds.c`; `ARCH_PROV_SRCS_hmac-p256` in `mk/arch/esp32.mk`, `PROV_SRCS` in `mk/firmware.mk`, `PROVISION=1` in `mk/arch/esp32-targets.mk`; `test/mock/prov_uds_mock.c`, `test/unit/test_provision_uds.c`; `wirecheck -genkey`, `test/integration/esp32/qemu_test.py` step 11 | `make esp32-qemu-test` on `esp32c3` | `PROV_GENKEY` burns a UDS the harness cannot read back, returns the DeviceID point, refuses a second call. **Landed (#48):** `src/caps/provision.c` is the wire alone, over a backend chosen by the object list (`ARCH_PROV_SRCS_<tier>` in `mk/firmware.mk`): `provision_atecc.c` for the element, `provision_uds.c` over the part HAL `provision_uds.h` for the ESP32 (`src/hal/esp32/prov_uds.c`); `test_provision_uds` on the mock part; `qemu_test.py` step 11 with `wirecheck -genkey`. |
| 6a | **Pulse timing on the RMT receiver** (ADR follow-up 8). | `src/hal/esp32/hal_pulse_rmt.c`, chosen by the object list (`ESP_PULSE := rmt` in `boards/esp32p4-poe/profile.mk`, `ARCH_PULSE_SRCS_<backend>` in `mk/arch/esp32.mk`, refused on the classic `esp32` by `mk/arch/esp32-targets.mk`); `esp_driver_rmt` in `mk/arch/esp32/main/CMakeLists.txt`; `test/mock/esp_rmt_mock.c` and its headers under `test/mock/esp32/`, `test/unit/test_esp_pulse_rmt.c` (in `mk/host.mk`) | `make host-test`; CI's `esp32-build BOARD=esp32p4-poe` compiles it; the signal itself only on the bench | Intervals run edge to edge from the first level at `start_level`, in 1 µs ticks (2 µs above a 32767 µs timeout); a quiet line ends the capture, `max_edges` stops it mid-frame, and the call is bounded by `timeout_us × (max_edges + 2)`; the channel is given back on every path; no free channel is `BUSY`. The QEMU board keeps the poll, so `esp32-qemu-test` is unchanged. **Landed (#48).** |
| 7 | **Bench HIL-0: factory record verified, pins, first boot with no eFuse burned.** **Landed, preparation:** every pin checked against the vendor schematic (`docs/reference/README.md`, "Board sources"), 32 MB flash with the slots below 16 MB, the stale comments, the overlay `boards/esp32p4-poe/sdkconfig.hil0` (secure boot and flash encryption off, built `CRYPTO=none`) and the bench page's "HIL-0" steps. **The flash itself is the next device slice**; slice 2, which puts the ES256 check and the `tfw_plat` key pin in layer 0, has landed, so it waits only for board time. **Run 2026-10-21, steps 1 to 5** (`docs/development/bench/esp32-p4.md`, "Runs"). | `boards/esp32p4-poe/` (pins against the schematic, flash size, the stale comment); `docs/development/bench/esp32-p4.md` | The bench | The record matches; our bootloader and app boot with layer 0's ES256 check, tier `none` (no UDS yet); the IP101 link; PING, CAPS (`0x80`) and SYS_INFO over the network; `make board-restore` brings the factory image back. |
| 8+ | **Bench HIL-1 to HIL-3**, one owner decision each (section 4). | `boards/esp32p4-poe/`; the bench page | The bench | The QEMU suite's checks repeated on silicon at each step. |

Slices 1–6 need no board. Slice 1 can run now; 2 before 3; 4 before 5; 6 after
1. Slice 7's preparation ran beside them; its flash waited for slice 2, which has landed.

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
| HIL-0 | Flash only: our bootloader, partition table at `0x10000`, `tfw_plat` with the image key pin, the app. Secure boot and flash encryption **off**, which needs a bench sdkconfig overlay through `ESP_SDKCONFIG_EXTRA`, `boards/esp32p4-poe/sdkconfig.hil0` (the profile turns flash encryption on in release mode, `boards/esp32p4-poe/sdkconfig.defaults`). Layer 0's ES256 check still runs, so the row verifies a signature (m453). | Yes: `make board-restore` | The lane's (m346: flashing is ours) |
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
