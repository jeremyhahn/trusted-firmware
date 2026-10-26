# OP-TEE on QEMU `virt`: the design of row `optee-trustzone`

> **Status: PROPOSED** · Issue: [#253](../../../../issues/253) · SPEC row:
> `optee-trustzone` (future) · Scope: trusted-firmware; go-enclave (seam only,
> [go-enclave#206](../../../../../go-enclave/issues/206)) · Last verified: 2026-10-26
>
> Nothing here is implemented. This page is the row's first deliverable: what is
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
None is on file in [`../reference/`](../reference/README.md) yet (see "To file").

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
below. Sources are fetched by the target into `build/optee/`; nothing is
vendored and no upstream file is patched.

Upstream sources are listed in `docs/reference/README.md`, "Not
redistributable" style (title, revision, URL), when slice 2 pins them. optee_os
and optee_client are BSD-2-Clause; they are fetched, not copied.

## What this project contributes

| Path (proposed) | What |
|---|---|
| `optee/ta/` | The TA, in C, built with optee_os's TA dev kit (`TA_DEV_KIT_DIR`). Same preprocessor rule as the firmware: no `#if` in logic. `user_ta_header_defines.h` holds `#define`s only |
| `optee/ta/include/tfw_ta.h` | The UUID and command ids below, as literals, shared by the TA and the test client |
| `optee/check/` | `tfw-ta-check`, a small C program in the guest that calls the TA through `libteec` and compares each answer to the literal vectors |
| `optee/testdata/vectors.h` | The literal vectors: UUID bytes, command ids, parameter types, expected return codes |
| `mk/optee.mk` | The pin and the targets below. Outside the AVR and Cortex-M object lists |

### Targets

| Target | Does |
|---|---|
| `make optee-tools` | Checks every tool the build needs (list under "CI"). A missing tool fails with its name and what to install, then exits non-zero. No skip |
| `make optee-qemu` | `optee-tools`, then fetches the pinned manifest, builds the stack, builds the TA and `tfw-ta-check` and installs both into the guest image (TA into tee-supplicant's TA directory, `/lib/optee_armtz/`) |
| `make optee-run` | Boots QEMU `virt` with the stack, headless. `OPTEE_SHARE=<dir>` mounts a host directory in the guest over virtio-9p; `OPTEE_GUEST_CMD=<cmd>` runs a command there after `tee-supplicant` starts, and the target exits with that command's status |
| `make integration-test-optee` | `optee-run` with `xtest` (upstream's smoke subset) and then `tfw-ta-check`. Fails on any failure of either |

How the TA reaches the guest (Buildroot rootfs overlay, or an early TA linked
into optee_os) is chosen in slice 3 from what the pinned `build` supports
without a patch.

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
| `0x00000001` | `GET_INFO` | `VALUE_OUTPUT`: `a` = interface version (1), `b` = 0 (flags, reserved) | | | `TEE_SUCCESS` | proposed, v1 |
| `0x00000002` | `GET_RANDOM` | `MEMREF_TEMP_OUTPUT`: 1 to 4096 bytes, filled by `TEE_GenerateRandom` | | | `TEE_SUCCESS`; `TEE_ERROR_BAD_PARAMETERS` for size 0 or above 4096 | proposed, v1 |
| `0x00000003` | `SEAL` | `MEMREF_TEMP_INPUT`: plaintext | `MEMREF_TEMP_INPUT`: associated data, may be empty | `MEMREF_TEMP_OUTPUT`: sealed blob | `TEE_SUCCESS`; `TEE_ERROR_SHORT_BUFFER` (`0xFFFF0010`) with the needed size | reserved: waits on open question 1 |
| `0x00000004` | `UNSEAL` | `MEMREF_TEMP_INPUT`: sealed blob | `MEMREF_TEMP_INPUT`: associated data | `MEMREF_TEMP_OUTPUT`: plaintext | `TEE_SUCCESS`; `TEE_ERROR_MAC_INVALID` (`0xFFFF3071`) for a tampered blob; `TEE_ERROR_SHORT_BUFFER` | reserved: waits on open question 1 |

The parameter type names are the TEE Client API's; the TA sees the matching
`TEE_PARAM_TYPE_*` values of the Internal Core API. v1 holds no key: `GET_INFO`
and `GET_RANDOM` need none. `SEAL` and `UNSEAL` have their ids and shapes held
so go-enclave can plan against them. Their blob format, algorithm and key are
not designed here: they depend on open question 1. The set itself is confirmed
with go-enclave#206 before slice 3 (seam, below).

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
| `tfw-ta-check`: each command against the literal vectors, including unknown id and bad parameters | this project | here, `make integration-test-optee` |
| `backendtest` on `arm-trustzone`: the enclave contract | go-enclave | go-enclave's CI, calling `make optee-run` at a trusted-firmware tag (open question 4) |

This project runs no go-enclave code in its own gate, and go-enclave writes no
test of the TA's internals.

## CI

The lane image has none of these; the CI job needs them all.

| Need | Why |
|---|---|
| `repo` (Google's tool) and `git` | fetch the pinned manifest |
| aarch64 and arm (32-bit) GNU cross toolchains, as the pinned `build` asks (`make toolchains` there fetches them) | optee_os, TF-A, Linux, the TA and `tfw-ta-check` |
| The host packages OP-TEE's build documents for the pinned release (among them Python 3 with `cryptography` and `pyelftools` for TA signing, `ninja`, `meson`, `glib`, `pixman` for QEMU, `expect`) | build the stack and QEMU |
| Several GB of disk and a build of tens of minutes, once per pin | the full stack, Linux and Buildroot included; cached by pin (open question 6) |

The QEMU binary is the one the manifest builds, not a distribution package, so
the pin covers it. When a tool is missing, `make optee-tools` names it and the
install step, and the job fails. A test that cannot run is a failure.

## Seam to go-enclave#206

| go-enclave needs | This project provides | Where |
|---|---|---|
| A TA to call | the TA, UUID and command table above | slice 3 |
| A secure world on QEMU `virt` | `make optee-run`, with `OPTEE_SHARE` and `OPTEE_GUEST_CMD` | slices 2 and 3 |
| A stable contract | the UUID and literal vectors; ids extend, never renumber | slice 3 |
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
3. **The TA, v1:** `optee/ta/` with `GET_INFO` and `GET_RANDOM`,
   `optee/testdata/vectors.h`, `tfw-ta-check`, and
   `make integration-test-optee`.
4. **CI and the hand-off:** the CI job with the toolchain and the cached stack,
   running `make integration-test-optee`; the tag go-enclave pins; this page's
   status to the built state.
5. **`SEAL` and `UNSEAL`**, only after open question 1 is ruled: blob format,
   algorithm and key as ruled, with their vectors.

## Open questions for the owner

1. **Keys in the TA.** Does any key ever live in the TA (a sealing key in OP-TEE
   secure storage, or one derived from OP-TEE's hardware unique key; signing
   keys)? On QEMU the hardware unique key is a fixed test value, so nothing
   sealed there is secret. If keys may live there, who owns them alongside
   go-xkms, and does go-enclave or this project define the blob? Until ruled, v1
   holds no key and ids 3 and 4 stay reserved.
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
