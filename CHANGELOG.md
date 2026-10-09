# Changelog

All notable changes to this project are documented here.

The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-1.0, MINOR carries breaking changes.

## [Unreleased]

### Added

- **Codex code review on the CI server** (#24). A pull request labelled
  `code-review` is reviewed by Codex on the `codex` runner, which approves it
  or requests changes as the `codex` user, pinned to the head commit. No
  repository secret is involved. See `docs/development/code-review.md`.

## [0.1.0-alpha] - 2026-10-08

This project continues arduino-generic `v0.7.0-alpha` (archived repository
`jeremyhahn/arduino-generic`, where the earlier history stays).

### Changed

- **Renamed from arduino-generic to trusted-firmware** (#2). Behaviour is
  unchanged; the bytes on the wire, in EEPROM and in evidence are the same.
  - C identifiers take the `tfw_` prefix, macros `TFW_`, and the headers live
    in `include/tfw/`.
  - The tools are `tfwsim`, `tfwsim-atecc608a`, `tfwctl` and `tfwprov`; the
    boot loader is `tfw-boot`.
  - The Go module is `github.com/jeremyhahn/trusted-firmware/sdk/go`, package
    `tfw`, and the simulator wrapper is `sdk/go/tfwsim`. Error strings start
    `tfw: `.
  - The firmware image is `trusted-firmware.{elf,hex}`, the SBOM format is
    `trusted-firmware/sbom/v1`, and the CI image is `trusted-firmware-ci:local`.
  - The simavr harness default ELF path now includes the `<crypto>/` segment.
  - The test-vector macros in `testdata/` take the `TFWV_` prefix (were
    `AGV_`; #3). Their values are unchanged by the rename.
- **BREAKING: the persisted, wire and evidence constants say `tfw`** (#3). No
  device built by arduino-generic interoperates with this release: its images,
  EEPROM, evidence and auth sessions are all rejected or differ. Old → new:
  - Boot image trailer magic `TFW_BOOT_TRAILER_MAGIC`: `0x4D494741`
    (`'A','G','I','M'`) → `0x4D494654` (`'T','F','I','M'`).
  - Platform EEPROM block magic `TFW_BOOT_EE_MAGIC`: `0x52504741`
    (`'A','G','P','R'`) → `0x52504654` (`'T','F','P','R'`). The baked
    factory-fresh block for serial `000102…0f` changes its CRC from `0x8206` to
    `0xADD5`.
  - Evidence CoSWID tag-id: uuid5(URL,
    `https://github.com/jeremyhahn/arduino-generic/coswid/firmware`) =
    `5eec7874-aaa5-57fd-aaae-f3f9fe0ccfb4` → uuid5(URL,
    `https://github.com/jeremyhahn/trusted-firmware/coswid/firmware`) =
    `bcc71007-1a66-53c1-967e-f0d3d5ede421`.
  - Evidence CoSWID software-name `"ag-fw"` → `"tfw"`, entity-name `"ag"` →
    `"tfw"`. The CoSWID shrinks from 85 to 84 bytes (`TFW_UCCS_COSWID_LEN`), the
    UCCS claims set from 152 to 151 (`TFW_UCCS_LEN`), and every DER length
    around the claims set in the TBS template moves down by one (`B2`→`B1`,
    `AF`→`AE`, `AC`→`AB`, `9E`→`9D`, `9B`→`9A`, `98`→`97`).
  - Alias certificate issuer `CN=ag DeviceID` → `CN=tfw DeviceID` and subject
    `CN=ag Alias` → `CN=tfw Alias`. Each RDNSequence grows by one byte
    (issuer `30 16 31 14 30 12 … 13 0B` → `30 17 31 15 30 13 … 13 0C`, subject
    `30 13 31 11 30 0F … 13 08` → `30 14 31 12 30 10 … 13 09`), and the subject
    public key moves from TBS offset 137 to 139. The TBS template goes from 230
    to 232 bytes; with the CoSWID change the TBSCertificate goes from 382 to 383
    (`01 7A` → `01 7B`) and the alias certificate from 470–472 to 471–473.
  - The UCCS and alias-certificate vectors in `testdata/uccs_vectors.h` are
    re-pinned, and `TFWV_ALIAS_SIG` is now a signature by the TEST DeviceID key.
  - Auth-session domain separator `"ag-auth-v1"` → `"tfw-auth-v1"`: the
    operator now signs SHA-256(`"tfw-auth-v1"` ‖ serial ‖ nonce), 59 bytes of
    message rather than 58. A signature made for an older firmware is refused.
  - The placeholder measurement used when no valid boot handoff exists,
    `"ag-unmeasured-application-v1"` → `"tfw-unmeasured-application-v1"`;
    its SHA-256 changes from `9cef89fe…b133` to
    `199ceb245029d3506c739c7ad1d96f75271d891dc7a406e126837913cff80d74`. On the
    software tier the CDI is HMAC(UDS, that digest), so a device behind a stock
    bootloader derives a different CDI and HMAC identity. The element tier
    uses it only to probe for a UDS and discards the result.
  - The `tfwsim-atecc608a` TEST identity: the seeds become
    `"tfwsim-atecc608a TEST DeviceID key v1"`, `"tfwsim-atecc608a TEST UDS v1"`
    and `"tfwsim-atecc608a TEST GenKey sequence v1"` (were `agsim-…`). The TEST
    DeviceID private key changes from `0df3d0b5…f76d` to
    `283706f57629c2daa7b161e5e6423077e65260187fbbd8673473ef7aea81551d`, and its
    public point from `41fbd565…dbf09` / `a3a696b6…daea4` to X
    `41035035faf782ef1cab6a669f1c3809b89fbf1cd096771d12d87e1cb3c35aca`, Y
    `eda3e5021ff3ee9e48329bcc44c1061e1d5949f1fbba0742a5b6ac6799efe78a`. The
    TEST UDS, and every CDI and GenKey key derived from it, change with them.
  - `tfwsim.TestSerial`, the test identity's DeviceID and UEID,
    `"agsim-test-id-01"` → `"tfwsim-testid-01"`.
