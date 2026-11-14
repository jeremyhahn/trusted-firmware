# OP-TEE on QEMU `virt`: the design of row `optee-trustzone`

> **Status: BUILT** · Issue: [#253](../../../../issues/253) · SPEC row:
> `optee-trustzone` (shipped, #430) · Scope: trusted-firmware; go-enclave (seam only,
> [go-enclave#206](../../../../../go-enclave/issues/206)) · Last verified: 2026-11-12
>
> Slice 2 is proven: the pin (OP-TEE 4.11.0) and `make optee-tools`,
> `optee-qemu` and `optee-run` are in `mk/optee.mk`. On 2026-10-26, from an
> empty `OPTEE_CACHE` on a 24-thread host, `make optee-qemu` fetched the release
> and its toolchains and built the stack in 1154 s (19 min 14 s). `make optee-run
> OPTEE_GUEST_CMD='xtest -l 0'` booted TF-A v2.14.0, U-Boot 2025.07, Linux and
> OP-TEE OS on QEMU `virt` and ran `xtest -l 0` in 320 s: 149 test cases and
> 43508 subtests, 0 failed, 0 skipped, guest status 0. `make optee-tools` names
> every missing host package before it fetches anything.
>
> Slices 3 to 5 are built and proven in the guest: the TA (`optee/ta/`, with
> `GET_INFO`, `GET_RANDOM`, `SEAL` and `UNSEAL`), `tfw-ta-check`
> (`optee/check/`), `optee/testdata/vectors.h` and `make integration-test-optee`.
> On the host, the TA's dispatch and the check pass `test_optee_ta`
> (`make host-test`). In the guest, `make integration-test-optee` boots the
> pinned stack, runs `xtest -l 0` and then `tfw-ta-check`, and fails unless the
> guest exits 0 and the log carries `tfw-ta-check: ok` for `get-info`,
> `get-random`, `seal-unseal`, `unseal-mac-invalid`, `seal-max`, `seal-over` and
> `unseal-over`, and `tfw-ta-check: PASS, 21 of 21 passed`. Main's CI run 138, at
> commit `ec7d341d`, passed it in the `OP-TEE [optee-qemu, integration-test-optee]`
> job.
>
> The CI jobs (slice 4) are in `.github/workflows/ci.yml`, in the `optee`
> image, with `OPTEE_CACHE` cached by the pin (key `optee-<release>`, open
> question 6 as proposed): `OP-TEE stack [optee-tools, optee-qemu]` builds and
> saves the stack when the cache misses, and `OP-TEE [optee-qemu,
> integration-test-optee]`, after it, only restores the stack and runs the
> tests. In run 138 the stack job hit the cache and finished in seconds; the
> test job ran in 30 minutes.
>
> This page is the row's first deliverable: what is
> composed, what this project adds, the trusted application's interface, the
> test split and the slices. SPEC rows change at merge.

## The row in one paragraph

go-enclave's `arm-trustzone` backend (go-enclave#206) runs on the host side of
Arm TrustZone-A and talks to a trusted application (TA) through the
GlobalPlatform TEE Client API. Its software-only slice needs OP-TEE running on
QEMU `virt` with that TA loaded, so the backend passes go-enclave's
`backendtest`. No hardware is involved. This project contributes the TA and one
`make` target that builds and boots the stack with it. Everything else is
upstream OP-TEE, composed and pinned. The enclave abstraction, its Go API and
`backendtest` are go-enclave's (L1); this project reimplements none of it (L2).

## Boundary

| In this row | Not in this row |
|---|---|
| TrustZone-A on Armv8-A: TF-A at EL3, OP-TEE OS at S-EL1, the TA at S-EL0, Linux in the normal world | TrustZone-M on Armv8-M (rows `stm32h5`, `nrf54l15`; [`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md), #15). No source, header or build rule is shared with the Cortex-M port or TF-M |
| QEMU `virt` (aarch64) only | Any board. The i.MX 95's Cortex-A55 cluster is not in row `imx95`, which is the M7 only (open question 5) |
| One TA, a test client and the target that runs them | A HAL or boot-HAL port, a board profile, a board id, `CAPS`, the AVR wire protocol, `sdk/go`. The TA speaks GP only |
| | The enclave API, `backendtest` and the Go side of libteec (go-enclave); DICE, certificates, attestation verification and any signing key (go-xkms) |

"TF-A" here is upstream Trusted Firmware-A, the EL3 monitor OP-TEE's build
composes. It is not this repository.

## Standards first

Each standard is implemented as written; nothing is designed in its place.
Each is on file in [`../reference/`](../reference/README.md): RFC 9562 is copied
there, and the GlobalPlatform specifications are listed by title, revision and
URL. The pinned release declares Internal Core API v1.3.1.

| Standard | Used for | Licence and file |
|---|---|---|
| GlobalPlatform, *TEE Client API Specification* v1.0 (GPD_SPE_007) | The normal-world API the test client and go-enclave's backend call: `TEEC_InitializeContext`, `TEEC_OpenSession`, `TEEC_InvokeCommand`, the `TEEC_Operation` parameter types, `TEEC_UUID`. OP-TEE's `libteec` implements it | GP licence forbids a copy: title and URL <https://globalplatform.org/specs-library/tee-client-api-specification/> |
| GlobalPlatform, *TEE Internal Core API Specification* (GPD_SPE_010), at the version the pinned optee_os declares in `TEE_CORE_API_MAJOR_VERSION`/`_MINOR_VERSION` | The TA's entry points (`TA_CreateEntryPoint`, `TA_OpenSessionEntryPoint`, `TA_InvokeCommandEntryPoint`, …), `TEE_Param` types, return codes, `TEE_GenerateRandom`, and any crypto or storage the TA uses | Same: title and URL <https://globalplatform.org/specs-library/tee-internal-core-api-specification/> |
| GlobalPlatform, *TEE System Architecture* (GPD_SPE_009) | Terms only (REE, TEE, TA, client) | Same: <https://globalplatform.org/specs-library/tee-system-architecture/> |
| RFC 9562, *Universally Unique IDentifiers (UUIDs)* | The TA's UUID, version 4, and its byte layout in `TEEC_UUID` | IETF: copy to `docs/reference/rfc9562.txt` |

Return codes are GP's. The TA defines no status code of its own.

## What is composed from upstream, and the pin

| Component | Upstream | Role |
|---|---|---|
| OP-TEE `manifest`, `qemu_v8.xml` | <https://github.com/OP-TEE/manifest> | The one pin. A release manifest fixes the revision of every component below |
| OP-TEE `build`, `qemu_v8.mk` | <https://github.com/OP-TEE/build> | Builds the stack and starts QEMU `virt` (`make run`) |
| optee_os | <https://github.com/OP-TEE/optee_os> | Secure OS (`PLATFORM=vexpress-qemu_armv8a`) and the TA dev kit (`out/export-ta_arm64`) the TA builds against |
| optee_client | <https://github.com/OP-TEE/optee_client> | `libteec` and `tee-supplicant` in the guest |
| optee_test | <https://github.com/OP-TEE/optee_test> | `xtest`, upstream's own regression suite; proves the composed stack before our TA is added |
| TF-A, Linux, Buildroot, QEMU, the normal-world boot loader | as the manifest pins them | EL3 monitor, guest kernel with the TEE driver (`/dev/tee0`), guest rootfs, the `virt` machine |

The pin is one OP-TEE release tag, recorded in `mk/optee.mk` with the
manifest's commit, and the GP Internal Core API version that release declares.
Slice 2 sets it to the newest release on the day it lands; this page is updated
in the same PR. A newer release is a PR of its own that re-runs every test
below. Sources are fetched by the target into `OPTEE_CACHE`, outside the
checkout (default `$(HOME)/.cache/trusted-firmware/optee`, one directory per
release), so a clean of this repository never refetches the multi-gigabyte
stack and CI can cache it; a cache inside the checkout is refused. Nothing is
vendored and no upstream file is patched.

**The pin, as slice 2 set it on 2026-10-26:** release `4.11.0` (the newest tag
on the manifest), manifest commit `b95d496b47c6548e3e196adad75e78bde115a18c`,
every repository at the revision `qemu_v8.xml` names; GP Internal Core API
v1.3.1; Google's `repo` tool at `v2.70`
(`46e9866ca0f161604bd45a0c3b7c7887c9415dcb`). Each commit is checked after the
fetch, and a mismatch fails it. `make -s optee-pin` prints the release tag,
which is also CI's cache key for `OPTEE_CACHE`.

Upstream sources are listed in `docs/reference/README.md`, "Not
redistributable" style (title, revision, URL). optee_os
and optee_client are BSD-2-Clause; they are fetched, not copied.

## What this project contributes

| Path | What |
|---|---|
| `optee/ta/` | The TA, in C, built with optee_os's TA dev kit (`TA_DEV_KIT_DIR`). Same preprocessor rule as the firmware: no `#if` in logic. `user_ta_header_defines.h` holds `#define`s only |
| `optee/ta/include/tfw_ta.h` | The UUID and command ids below, as literals, shared by the TA and the test client |
| `optee/check/` | `tfw-ta-check`, a small C program in the guest that calls the TA through `libteec` and compares each answer to the literal vectors |
| `optee/testdata/vectors.h` | The literal vectors: UUID bytes, command ids, parameter types, expected return codes |
| `mk/optee.mk` | The pin and the targets below. Outside the AVR and Cortex-M object lists |

### Targets

| Target | Does |
|---|---|
| `make optee-pin` | Prints the pinned release tag (`make -s optee-pin`), CI's cache key |
| `make optee-tools` | Checks every tool the build needs (list under "CI"). A missing tool fails with its name and what to install, then exits non-zero. No skip |
| `make optee-qemu` | `optee-tools`, then fetches the pinned manifest, builds the stack, builds the TA and `tfw-ta-check` and installs both into the guest image (TA into tee-supplicant's TA directory, `/lib/optee_armtz/`) |
| `make optee-run` | Boots QEMU `virt` with the stack, headless. `OPTEE_SHARE=<dir>` mounts a host directory in the guest over virtio-9p; `OPTEE_GUEST_CMD=<cmd>` runs a command there after `tee-supplicant` starts, and the target exits with that command's status |
| `make integration-test-optee` | `optee-run` with `xtest` (upstream's smoke subset) and then `tfw-ta-check`. Fails on any failure of either |

The TA reaches the guest through a second Buildroot rootfs overlay, chosen in
slice 3 because the pinned `build` supports it without a patch: `qemu_v8.mk`
sets `BR2_ROOTFS_OVERLAY` with a plain `=` and `common.mk` hands every `BR2_*`
variable to Buildroot, so `mk/optee.mk` passes upstream's overlay and then
`$(OPTEE_CACHE)/<release>/tfw/overlay`. `make optee-qemu` runs upstream's `all`
twice: the first pass exports the TA dev kit and stages `libteec`; the TA and
`tfw-ta-check` build against those into the overlay (`/lib/optee_armtz/<uuid>.ta`,
`/usr/bin/tfw-ta-check`); the second pass puts the overlay in the root
filesystem. An early TA would need `optee_os` rebuilt for every TA change.

## The trusted application

**UUID:** `23751b49-3a76-4e49-b63c-e5853742fa1f` (RFC 9562 version 4, random).
As `TEEC_UUID`: `timeLow` `0x23751b49`, `timeMid` `0x3a76`,
`timeHiAndVersion` `0x4e49`, `clockSeqAndNode`
`{0xb6, 0x3c, 0xe5, 0x85, 0x37, 0x42, 0xfa, 0x1f}`. The UUID never changes; an
incompatible TA would take a new one.

**Session:** login `TEEC_LOGIN_PUBLIC`; open-session parameters all
`TEEC_NONE`. Multi-session, not single-instance. The TA keeps no state across
sessions.

**Commands.** A command id is a 32-bit `commandID`. Ids extend and never
renumber; an id the TA does not know answers `TEE_ERROR_NOT_SUPPORTED`
(`0xFFFF000A`), so an older TA reads a newer command as unrecognised. A wrong
`paramTypes` answers `TEE_ERROR_BAD_PARAMETERS` (`0xFFFF0006`). Slots not
listed are `TEEC_NONE`.

| Id | Name | Param 0 | Param 1 | Param 2 | Returns | Status |
|---|---|---|---|---|---|---|
| `0x00000000` | — | | | | `TEE_ERROR_NOT_SUPPORTED` | reserved, never assigned |
| `0x00000001` | `GET_INFO` | `VALUE_OUTPUT`: `a` = interface version (1), `b` = 0 (flags, reserved) | | | `TEE_SUCCESS` | implemented (#253) |
| `0x00000002` | `GET_RANDOM` | `MEMREF_TEMP_OUTPUT`: 1 to 4096 bytes, filled by `TEE_GenerateRandom` | | | `TEE_SUCCESS`; `TEE_ERROR_BAD_PARAMETERS` for size 0 or above 4096 | implemented (#253) |
| `0x00000003` | `SEAL` | `MEMREF_TEMP_INPUT`: plaintext, 0 to 8192 bytes | `MEMREF_TEMP_INPUT`: associated data, must be empty | `MEMREF_TEMP_OUTPUT`: sealed blob, at most 8238 bytes | `TEE_SUCCESS`; `TEE_ERROR_SHORT_BUFFER` (`0xFFFF0010`) with the needed size; `TEE_ERROR_BAD_PARAMETERS`, also for a plaintext above 8192 bytes | implemented (#288) |
| `0x00000004` | `UNSEAL` | `MEMREF_TEMP_INPUT`: sealed blob, at most 8238 bytes | `MEMREF_TEMP_INPUT`: associated data, must be empty | `MEMREF_TEMP_OUTPUT`: plaintext | `TEE_SUCCESS`; `TEE_ERROR_MAC_INVALID` (`0xFFFF3071`) for a tampered blob; `TEE_ERROR_BAD_FORMAT` (`0xFFFF0005`) for a blob that does not parse; `TEE_ERROR_SHORT_BUFFER` with the needed size; `TEE_ERROR_BAD_PARAMETERS`, also for a blob above 8238 bytes | implemented (#288) |

The parameter type names are the TEE Client API's; the TA sees the matching
`TEE_PARAM_TYPE_*` values of the Internal Core API. `GET_INFO` and `GET_RANDOM`
need no key. `SEAL` and `UNSEAL` implement go-xkms's sealed blob
(`docs/optee-sealed-blob/README.md` at `06ca5ec59`) and design nothing of their
own: the blob is a COSE_Encrypt0 under AES-256-GCM, and its key is HKDF-SHA-256
(RFC 5869, no salt, `info` = the label `go-xkms optee-sealed-blob v1`, a zero
byte and the TA's UUID) over the TA unique key, the
`PTA_SYSTEM_DERIVE_TA_UNIQUE_KEY` output, not the HUK. The HMAC is the Internal
Core API's `TEE_ALG_HMAC_SHA256`. The TA reads its UUID from `gpd.ta.appID`,
draws a fresh 12-byte nonce from `TEE_GenerateRandom` on every `SEAL`, and wipes
the key on every path. The associated data (param 1) must be empty, because
go-xkms's format has no field for it and data a caller believes is bound would
not be; a non-empty one answers `TEE_ERROR_BAD_PARAMETERS`. The TA bounds
the plaintext at 8192 bytes (`TFW_TA_SEAL_MAX`, #321), so
that `UNSEAL`, which holds a private copy of the blob and a private plaintext
scratch, opens every blob `SEAL` returns within the TA's 32 KiB heap. The set itself is
confirmed with go-enclave#206 (seam, below).

`SEAL` and `UNSEAL`'s vectors in `optee/testdata/vectors.h` are go-xkms's at
`06ca5ec59`, keyed by the TA unique key, not the HUK.

Every row of this table becomes a literal in `optee/testdata/vectors.h`. A
failure there is an interface break, not a test to update.

## How go-enclave's `backendtest` reaches the TA

```text
host CI job
 └─ make optee-run OPTEE_SHARE=<go-enclave build> OPTEE_GUEST_CMD=<backendtest>
     └─ QEMU virt (aarch64)
         ├─ secure world: TF-A → OP-TEE OS → our TA (UUID above)
         └─ normal world: Linux, /dev/tee0, tee-supplicant
             └─ backendtest (linux/arm64) → go-enclave arm-trustzone backend
                 → libteec (TEE Client API) → TEEC_InvokeCommand → TA
```

go-enclave cross-builds `backendtest` for `linux/arm64`. How its backend binds
`libteec` (cgo against the guest's `libteec.so`, or another route it owns) is
go-enclave's choice; the TA only sees GP calls.

| Test | Owner | Runs where |
|---|---|---|
| `xtest` smoke subset: the composed stack works | upstream OP-TEE; we only run it | here, `make integration-test-optee` |
| `tfw-ta-check`: each command against the literal vectors, including unknown id and bad parameters; SEAL and UNSEAL on the device's own TA unique key: a round trip, a fresh nonce per SEAL, `TEE_ERROR_MAC_INVALID` for a flipped ciphertext byte, `TEE_ERROR_SHORT_BUFFER` with the size needed, and `TEE_ERROR_BAD_PARAMETERS` for wrong types | this project | here, `make integration-test-optee` |
| `test_optee_ta`: the TA's entry points and dispatch against the vectors, and `tfw-ta-check` against the TA and every broken answer, over host doubles of the GP headers (`test/mock/gp/`, `test/mock/gp_bridge.c`) | this project | here, `make host-test` and `make coverage` |
| `backendtest` on `arm-trustzone`: the enclave contract | go-enclave | go-enclave's CI, calling `make optee-run` at a trusted-firmware tag (open question 4) |

This project runs no go-enclave code in its own gate, and go-enclave writes no
test of the TA's internals.

## CI

The lane image has none of these; the CI job needs them all.

| Need | Why |
|---|---|
| `repo` (Google's tool) and `git` | fetch the pinned manifest |
| aarch64 and arm (32-bit) GNU cross toolchains, as the pinned `build` asks (`make toolchains` there fetches them) | optee_os, TF-A, Linux, the TA and `tfw-ta-check` |
| The host packages OP-TEE's build documents for the pinned release (among them Python 3 with `cryptography` and `pyelftools` for TA signing, `ninja`, `meson`, `glib`, `pixman` for QEMU, the OpenSSL headers (`libssl-dev`) for U-Boot's host tools, `expect`) | build the stack and QEMU |
| Several GB of disk and a build of tens of minutes, once per pin | the full stack, Linux and Buildroot included; cached by pin (open question 6) |

The QEMU binary is the one the manifest builds, not a distribution package, so
the pin covers it. When a tool is missing, `make optee-tools` names it and the
install step, and the job fails. A test that cannot run is a failure.

## Seam to go-enclave#206

| go-enclave needs | This project provides | Where |
|---|---|---|
| A TA to call | the TA, UUID and command table above | slice 3, written: `optee/ta/`; tests `test_optee_ta` (host) and `tfw-ta-check` in `make integration-test-optee` (guest, green in main's CI run 138) |
| A secure world on QEMU `virt` | `make optee-run`, with `OPTEE_SHARE` and `OPTEE_GUEST_CMD` | slices 2 and 3 |
| A stable contract | the UUID and literal vectors; ids extend, never renumber | slice 3, written: `optee/testdata/vectors.h`, pinned by `test_optee_ta` and `scripts/test_optee_mk.py` (`OpteeTa`) |
| A version to pin | a trusted-firmware tag that carries the target | slice 4 |

Before slice 3 starts, go-enclave#206 confirms the command set, or names what
its software-only slice calls instead. A command go-enclave needs that is not in
the table is added here, with the next free id, before it is implemented on
either side.

## Slices

One PR each (m751).

1. **Design** (this page, #253): the row's design, its SPEC link and the
   standards to file.
2. **Pin and boot:** `mk/optee.mk` with the release pin, `make optee-tools`,
   `make optee-qemu`, `make optee-run`; `xtest`'s smoke subset passes on QEMU
   `virt`. The GP specifications and the upstream sources are entered in
   `docs/reference/README.md`, RFC 9562 is copied there.
3. **The TA, v1** (done): `optee/ta/` with `GET_INFO` and `GET_RANDOM`,
   `optee/testdata/vectors.h`, `tfw-ta-check`, and
   `make integration-test-optee`.
4. **CI and the hand-off:** the CI job with the toolchain and the cached stack,
   running `make integration-test-optee`; the tag go-enclave pins; this page's
   status to the built state. The CI jobs and this page are done; the tag
   is not yet cut.
5. **`SEAL` and `UNSEAL`** (#288, done): go-xkms's blob format and key
   derivation, their vectors, and HKDF-SHA-256 over the GP HMAC.

## Open questions for the owner

1. **Keys in the TA.** Does any key ever live in the TA (a sealing key in OP-TEE
   secure storage, or one derived from OP-TEE's hardware unique key; signing
   keys)? On QEMU the hardware unique key is a fixed test value, so nothing
   sealed there is secret. If keys may live there, who owns them alongside
   go-xkms, and does go-enclave or this project define the blob? Answered for
   the sealed blob by owner item 177, option (a) (CA m1656, ruling 251), and
   shipped in #288: go-xkms defines the blob, and the TA holds no stored key,
   only one derived on each call from the TA unique key. Still open for signing
   keys and anything stored in secure storage.
2. **TA signing key.** OP-TEE verifies a TA's signature at load. On QEMU,
   upstream's development key is used. For any image beyond QEMU, is the TA
   signed by go-xkms (this project holds no private key)?
3. **Who owns the TA's command set.** This page proposes it and go-enclave
   confirms it. Should go-enclave instead define the operations and this
   project only implement them?
4. **Where `backendtest` runs.** Proposed: go-enclave's CI calls `make
   optee-run` at a pinned trusted-firmware tag. The alternative is this
   project's CI running a pinned go-enclave `backendtest`, which puts a
   go-enclave dependency in this gate.
5. **Hardware.** Does the row ever leave QEMU, for example to the i.MX 95's
   Cortex-A55 cluster? Not in scope until ruled.
6. **CI cost.** Build the stack in the job with a cache keyed on the pin, or
   publish a CI image with the pinned stack prebuilt (a spend decision).
