# Changelog

All notable changes to this project are documented here.

The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-1.0, MINOR carries breaking changes.

## [Unreleased]

New entries go to `changelog.d/<issue>-<slug>.md`; the release step concatenates
them into this file.

## [0.4.0-alpha] - 2026-10-15

### Added

- **The Ethernet transport over a WIZnet W5100 or W5500** (#55, #59). `make
  ETH=1` links the Ethernet link beside serial. The device dials its endpoint
  and dispatches only `CAPS`, `APP_INFO`, `ATTEST`, `ATTEST_CERT` and
  `ATTEST_CSR` over it; every other command is `TFW_ERR_UNKNOWN_CMD`.
  `tfwsim --eth` runs it on the host.
- **`sdk/go`: `tfw.Listen`, `tfw.NewListener`, `Listener` and `TCPDevice`**
  (#59), the host side of the Ethernet transport: the host listens, the device
  connects, `Accept` returns the device.
- **`sdk/go`: the network config** (#59). `NetConfig`, `NetConfigError`,
  `Device.WriteNetConfig`, `DerivedMAC`, `NetConfigAddr` and `NetConfigLen`
  write the 32-byte network block at EEPROM `0x0C0` over serial.
- **`sdk/go`: `tfwsim.StartElementEthernet`** (#59), the element-tier simulator
  as an Ethernet device, returning its serial and its TCP link.
- **`make wiznet-probe`** (#55, #58), a probe image that reports which WIZnet
  chip a shield carries.
- **The application SDK** (#51, #57): an application linked by naming an
  `APP_DIR` (`TFW_FEATURE_APP`), its runner, and `TFW_CAPBIT_*` naming the CAPS
  bits; `make app-sdk-check`, `app-firmware-check` and `tfwsim-app-example`.
- **`APP_INFO`, command 0x06** (#51, #64), the linked application's descriptor
  and runner state in a 63-byte reply, with literal vectors. An image with no
  application answers `TFW_ERR_UNKNOWN_CMD`, so it is unchanged on the wire.
- **`sdk/go`: `Device.AppInfo`, `AppInfo`, `AppState` and `CmdAppInfo`** (#51,
  #64).
- **`make hw-test BOARD=<board> PORT=<dev>`** (#7) runs the SDK's
  cross-implementation suite, the one `make sdk-test` runs against `tfwsim`,
  against a flashed board on a serial port. The suite gets its connection
  through one seam, `sdk/go/internal/peer`. That seam resets the board by
  pulsing DTR and waits for its BOOT event. The suite checks the pins of the
  board it is told it has, and `CAPS` must report that board. A missing or
  silent port fails the run. The target is not part of `make ci`.
- **`make hw-enroll DEVICE=<serial port> [DEVICEID=<hex>] [ACME_DIRECTORY=<url>]`**
  (#7) enrolls a provisioned element-tier board through device-attest-01 and
  writes a run record (board, transport, tier, ueid, verdict, UTC time, the
  issued chain in PEM) under `docs/development/enrollment-runs/`. The harness
  is `TestHardwareEnrollment` in `xkms/e2e`; `make xkms-test` runs it against
  `tfwsim`. It is outside `make ci`, as `bench-check` is. A board over TCP waits
  for an `sdk/go` tag carrying #59. Procedure:
  `docs/development/hardware-enrollment.md`.
- **`TFW_ELEMENT_I2C_ADDR` and `TFW_ELEMENT_I2C_HZ`** (#7, #60), the secure
  element's I2C address and clock as board-profile data for the ATECC608C
  variants; the default stays 0x60, and `I2C_XFER` refuses the element's address.
- **`make platform-bake` and `make platform-bake-flash`** (#7, #68), a bench
  image that writes the platform block through the stock bootloader and checks
  it on the part.
- **The ESP-IDF port** (#16, #48, #65): the HAL and boot HAL on ESP-IDF v5.5.5,
  boards `esp32-qemu` and `esp32p4-poe`, `make esp32-build` and `make
  esp32-qemu-test` (secure boot, identity and the wire in Espressif's QEMU).
- **An `esp32` CI job** (#48, #68), in a pinned ESP-IDF image, part of the `CI
  Gate`.
- **The xkms adapter's possession signer and go-xkms factory** (#28, #44):
  `Backend.PossessionSigner` for ACME finalize over `ATTEST_CSR`, and
  `avr.NewKeyProvider` with `ConfigKeyDevice` and `ConfigKeyConfig`.
- **`make releasability`** (#1) fails on a private-fleet address, hostname,
  owner path or owner account name in any file that would be published (the
  tracked files minus the `export-ignore` entries in `.gitattributes`), and
  prints `file:line: class` for each. `make releasability-test` proves it on
  planted values and proves `git archive` drops the agent files and the
  forge-only workflows. Both are part of `make ci`.
- **A qwen review job** (dept-infra#164) for pull requests handed to opencode.
- **Cortex-M ADRs** (#63): the reference part and emulation, and the trust model
  against TF-M.
- **The TrustFLEX 608C slot-map ADR** (#7, #63).
- **The OTA architecture and image-format ADR** (#52, #66), rewritten to the
  owner's answers (#52, #15, #70), with the i.MX 95 M7 facts from RM00284 and
  UM12527 (#80).
- **The application SDK's measured flash cost** (#51, #66).

### Changed

- **The xkms adapter registers the AVR backend with go-xkms as `tfw`** (#28).
  `xkms/go.mod` requires go-xkms `v0.53.0-alpha`, which carries the
  external-backend hook (go-xkms#1439 step 1). Importing
  `github.com/jeremyhahn/trusted-firmware/xkms`, blank if nothing else is needed,
  makes `tfw` a backend type that `AutoInitialize` constructs from
  `AutoConfig.BackendConfigs["tfw"]`. That entry carries `device` (the attached
  `*tfw.Device`, required) and `config` (an `*avr.Config`, optional).
  - go-xkms `v0.53.0-alpha` still requires `arduino-generic/sdk/go` for that
    built-in backend, so it stays in `xkms/go.mod` as an indirect requirement
    until step 3.
- **BREAKING: Key IDs beginning `avr:` no longer resolve to the xkms adapter's
  backend** (#28, #44). The backend's `Type()` and its keys' store type are now
  `tfw`, so its Key IDs read `tfw:...`. go-xkms keeps `avr` for its own built-in
  backend until go-xkms#1439 step 3, and a backend type is held once per
  process. A stored `avr:` Key ID that meant this device has to be rewritten as
  `tfw:`. The adapter does not migrate it.
- **`xkms/go.mod` requires `sdk/go v0.3.0-alpha`** (#28, #38) and no longer
  `replace`s it with `../sdk/go`. A tagged module with a local replace is not
  consumable, so this unblocks tagging `xkms/`. Release order is now: tag
  `sdk/go`, bump `xkms/go.mod`, tag `xkms/`. The `xkms` tests find
  `tfwsim-atecc608a` through `TFWSIM_DIR` (the directory holding it), since
  the SDK now sits in the module cache rather than beside the adapter.
- **`tfwprov bake-eeprom`'s advice** (#7, #68): the Mega 2560's STK500v2
  bootloader writes EEPROM correctly but reads it back from the wrong address,
  so avrdude's verify cannot be trusted there.
- **Each CI job removes its own images when it ends** (#61, #62).
- **The code-review workflow is template v4.2** (DS-1570, #42).
- **`docs/SPEC.md` and `ROADMAP.md` agree** (#39, #41).
- **xkms-adapter doc headers under the deletion rule** (#39, #43).
- **SPEC Purpose and Boundaries under the layered-stack ruling** (#45).
- **SPEC device-framework rows, architecture ports and dev complete** (#46).
- **SPEC hsm-self-defence row, i.MX 95 M7 tamper response** (#47).
- **ROADMAP issue links for the device-framework and hsm-self-defence rows** (#54).
- **SPEC ethernet-transport row, Cortex-M in QEMU, AVR dev complete** (#56).
- **SPEC: AVR dev complete before the element** (#55, #58).
- **SPEC states the CI-standard release criterion** (DS-1925, #69).
- **SPEC and ROADMAP pass the audit** (DS-1935, #73).

### Fixed

- **The xkms concurrency test's deadline** (#40) was too tight under CI load.

## [0.3.0-alpha] - 2026-10-10

### Added

- **The xkms adapter module** (#28). `github.com/jeremyhahn/trusted-firmware/xkms`
  is the AVR device as an xkms key provider, moved here from go-xkms's
  `pkg/backend/avr` rather than rewritten. It is a separate module from `sdk/go`
  on purpose: it imports go-xkms (Layer 3) and `sdk/go` (Layer 0), and `sdk/go`
  must stay free of in-house dependencies.
  - Provenance: the paths were imported verbatim, in their own commit, from
    go-xkms tag `v0.51.0-alpha` (`73ea01410dc9791c97dd2ae105945d26b1a12e0a`):
    `pkg/backend/avr/*`, `sdk/go/backends_avr*.go`,
    `docs/avr-device-backend/*` and `pkg/acme/client/device_attest_dice_e2e_test.go`.
    Later commits move them into `xkms/` (`avr/`, `backends_avr*.go`, `e2e/`) and
    `docs/xkms-adapter/`, and adapt them.
  - The adaptation is the rename (`sdk/go` import, the `tfw` alias, `tfwsim`, the
    `tfw` certificate subjects), protocol 2 (the fake CAPS header claims 2.0, the
    major-refusal test covers 1 and 3) and the `uint32` `FreeRAM`.
  - `xkms/go.mod` requires go-xkms `v0.51.0-alpha` and replaces the SDK with
    `../sdk/go`. `xkms/register.go` marks where the go-xkms#1439 registration
    goes and implements nothing.
  - The end-to-end test enrols the real firmware compiled for the host
    (`tfwsim`, `tfwsim-atecc608a`), not a binary somebody downloaded.
- **`make xkms-test` and `make xkms-coverage`** (#28), their own targets and not
  part of `make ci`: `make docker-ci` runs `make ci` in a container with no route
  to the private go-xkms module.
- **An `xkms` CI job** (#28), joined to the `CI Gate`, which runs both targets. It
  runs on the runner rather than in the toolchain image, because the image cannot
  reach the private go-xkms module and a credential must not be baked into a
  layer.
- **`make header-deps-check`** (#29), part of `make ci`. For `tfwsim`, a
  unit test and a boot test it asserts the binary is up to date after a build,
  touches one header it includes, and fails unless the binary is then out of
  date. The header's mtime is restored on every exit path. The motivating case
  was a stale `tfwsim` that answered protocol 1.0 after `protocol.h` had moved
  to 2.
- **`ATTEST_CSR`, command 0x64** (#30). Proof of possession of the DeviceID key
  at ACME finalize, option (c) of the possession ruling on #6.
  - Request `[b:32]`, response `[sig:64]`: `r‖s`, the DeviceID key's P-256
    signature over SHA-256 of a PKCS#10 `CertificationRequestInfo`. Any other
    request length is `TFW_ERR_BAD_ARGC`. One exchange: nothing is paged and
    nothing is kept, so it does not disturb an `ATTEST_CERT` in progress.
  - CAPS feature bit 15, `TFW_FEATURE_ATTEST_CSR`. Selected only beside
    `ATTEST_CERT`; a board profile can turn it off alone.
  - The device builds the request itself from a fixed template: 365 bytes, version
    0, an empty subject, its `SubjectPublicKeyInfo`, and an `extensionRequest`
    holding a critical subjectAltName `otherName` `id-on-permanentIdentifier`
    and the critical `tcg-dice-UCCS-evidence` extension carrying the claims set
    with `b` as the nonce. The host rebuilds the same bytes; it chooses `b` and
    nothing else.
  - The permanent identifier is the lowercase hex of the full 17-byte UEID, type
    byte included: 34 hex digits, per go-xkms#1441.
  - `src/caps/csr.c` is a second table for the one template walker,
    `src/caps/tmpl.c` (`tfw_tmpl_emit`, `tfw_tmpl_digest`), which the alias
    certificate now also goes through. No new static RAM; the key buffer doubles
    as the signature buffer.
- **Vectors** (#30), literals from an independent encoder, read back by OpenSSL:
  `TFWV_CSR_CRI` (365 bytes) and `TFWV_CSR_CRI_DIGEST` in `testdata/vectors.h`,
  beside `TFWV_ALIAS_PUBKEY` and `TFWV_UCCS` (151 bytes) in
  `testdata/uccs_vectors.h`. `test_csr` also pins the alias certificate beside
  them and checks that none of the three signed structures parses as another.
- Board docs, `docs/boards/atmega32u4.md`, recording which commands that part
  leaves out.
- **`Device.AttestCSR`** (#31). `AttestCSR(ctx, b [AliasNonceLen]byte)` returns the
  64-byte `r‖s` signature the device's DeviceID key made over its
  CertificationRequestInfo (`ATTEST_CSR`, #30), as a `[CSRSigLen]byte`.
  - Transport only. This repo does not own certificate profiles: go-xkms
    reproduces the 365-byte CertificationRequestInfo itself, verifies the
    signature over it and builds the PKCS#10 request (go-xkms#1441). `sdk/go`
    holds no template, no ASN.1, no OIDs and no Go mirror of the CSR vectors; the
    C vectors and C tests stay and pin what the device emits.
  - `b` is the same 32 bytes handed to `AttestCert`. No alias certificate is
    minted: the call is `CAPS`, then `ATTEST_CSR`.
  - A device whose CAPS do not report `FeatureAttestCSR` is refused with
    `StatusErrUnsupported`, wrapped, before any attestation command is sent. A
    reply of any length but 64 is a `*ShortReplyError`.
    `sdk/go` still has no in-house dependencies.
- **`FeatureAttestCSR`** (#31), CAPS feature bit 15, the `sdk/go` name for the
  firmware's `TFW_FEATURE_ATTEST_CSR`, and **`CSRSigLen`** (64), the length of the
  reply.
- **`tfwctl attest-csr --nonce <hex32>`** (#31). Prints the signature as 128 hex
  digits on stdout, or `{"signature": <hex>, "len": 64}` with `--json`. The nonce
  is a flag, not a positional argument.
- **`tfwsim.StartElementVia`** (#31). `StartElement` with the connection to the
  simulator passed through a `wrap` function before the `Device` sees it, so a
  test can stand between a host and a real device and rewrite a frame in flight.
- Documentation of `Device.AttestCSR` and `tfwctl attest-csr` in
  `docs/dice-attestation` (`api.md`, `cli.md`, `user.md`, `engineering.md`) and
  `docs/host-sdk/api.md`.
- **The possession ADR** (#6, #36), accepted as option (c) with Amendments 1 and
  2. Only the 2560 element tier links `ATTEST_CSR`, and `sdk/go` `AttestCSR` is
  transport only.

### Changed

- **The CI image's Go toolchain is 1.27.2** (#28), was 1.26.5, because go-xkms
  requires Go 1.27.1 or later. `GOTOOLCHAIN` stays `local`, and the `xkms` job
  pins the same version.
- **Host and AVR objects rebuild when an included header changes** (#29). An
  edited header used to leave stale objects and test binaries behind, because a
  target depended on its `.c` files and nothing else.
  - Host binaries are compiled and linked by one `cc` command over many sources,
    where gcc's `-MMD` keeps only the last source's headers. `mk/ccdeps.sh`
    wraps that command and writes `<output>.d` with `-MM -MP -MT`, naming every
    header of every source; a deleted header is not a build error.
  - The AVR firmware and tfw-boot objects (`mk/firmware.mk`, `mk/boot.mk`) read
    their per-object `.d` files the same way. All of them are read with
    `-include`, after every rule, because the first build has none.
- **Only the ATmega2560 element tier carries `ATTEST_CSR`** (#30). The ATmega328P
  profile turns it off by name because it puts that part's element image over
  its flash budget (16 884 against 16 384 bytes); its alias certificate stays.
  The ATmega32U4 profile turns it off by name under the owner's ruling on #6
  (2026-10-09). The ATtiny85 and the ATmega32U4 have no `ATTEST_CERT`, and the
  command follows it. Everywhere else 0x64 answers `TFW_ERR_UNKNOWN_CMD` and CAPS
  bit 15 is clear.
- **The host build no longer forces `ATTEST_CSR` on** (#30). CAPS bit 15 is set
  only where `ATTEST_CSR` is linked: the 2560 element tier and the simulator's
  element tier. `make board-sweep` pins this per board.

## [0.2.0-alpha] - 2026-10-09

### Changed

- **BREAKING: boot handoff version 3** (#10). The block keeps its layout, but
  byte 76 now holds the abstract `tfw_reset_cause_t` instead of the raw `MCUSR`,
  so neither side decodes a register. A version 2 block is refused rather than
  reinterpreted. The decode lives only in `src/hal/avr/hal_reset_decode.h`.
- **Board-driven boot geometry and NVM** (#26). A board profile states the
  application and boot regions as a base and a size each
  (`TFW_APP_REGION_BASE/SIZE`, `TFW_BOOT_REGION_BASE/SIZE`, `TFW_OTA_PAGE_MAX`)
  rather than "the boot section is what is left at the top of flash";
  `TFW_BOOT_SECTION_SIZE` is now `TFW_BOOT_REGION_SIZE`. Image measurement and
  OTA offsets count from the application base, and `include/tfw/boot.h` asserts
  at compile time that the regions fit the part and do not overlap.
  - The platform-reserved EEPROM is reached through `tfw_boot_nvm_*` in tfw-boot
    and `tfw_hal_nvm_read` in the application.
  - The boot session idle window counts ticks of the boot HAL
    (`TFW_BOOT_TICK_HZ`) instead of `TFW_BOOT_SESSION_POLL_CYCLES`.
  - `mk/arch/avr.mk` refuses a profile whose `BOOT_BASE + BOOT_SECTION` is not
    the end of flash.
- **Arch-neutral HAL contract** (#9, #13). `tfw_pinref_t` and `tfw_pwmref_t`
  come from the architecture's `hal_types.h`, and `tfw_hal_irq_enable()` is
  added so core no longer assumes what unmasking interrupts means.
- **BREAKING: wire protocol 2.0** (#12). `TFW_PROTO_VERSION_MAJOR` and the Go
  `ProtoVersionMajor` are 2 (were 1), minor 0, because of the six-byte pin page
  below: a frame an older host cannot parse is a major change.
  - Wire: CAPS board-header byte 0 is `0x02` (was `0x01`); byte 1 stays `0x00`.
  - A 1.x host refuses this firmware, and this host refuses 1.x firmware, with
    `ProtocolVersionError` instead of misreading the shifted pin-page fields.
    Firmware and host must be upgraded together.
- **BREAKING: the CAPS pin page is six bytes** (#12), so a part with 32-bit
  ports can be described. A host built before this reads the wrong bytes.
  - Was `[pin][flags][port<<4|bit][adc][pwm]` (5 bytes); is
    `[pin][flags][port][bit][adc][pwm]` (6 bytes).
  - Wire byte 2 is now the port index alone, byte 3 the bit, each a whole byte;
    ADC channel moves from byte 3 to byte 4, PWM state from byte 4 to byte 5.
  - "No digital port" is `0xFF` in both port and bit (was a high nibble of
    `0x0F`).
  - Go: `PinInfo` is unchanged in shape; `capsPinPageLen` is 6.
  - The firmware now fills port and bit whole from the board table. The pin
    coordinate is per-architecture (`tfw_pin_coord_t`, `TFW_PB`,
    `TFW_PORT_NONE` in `src/hal/<arch>/hal_types.h`): the AVR keeps one byte of
    two nibbles, and the host build and `tfwsim` store a byte each. Core no
    longer unpacks nibbles, so a non-AVR part can report port 18 or bit 31.
  - `tfw_board_pin_port_bit()` is removed. Its contract was the AVR's packed
    byte, and the pin page replaces it.
- **SYS_INFO gains an appended u32 `free_ram`** (#12). The reply is 15 bytes
  (was 11): `[uptime:4][free_ram:2][reset_cause:1][err_crc:2][err_format:2]
  [free_ram32:4]`.
  - Wire bytes 11-14 are new. Bytes 0-10 keep their offsets, so a host that
    reads only the earlier shapes still parses correctly.
  - The u16 at bytes 4-5 is kept rather than replaced, since the issue asks
    for an *appended* field and an older host keeps working. It now saturates at
    `0xFFFF` instead of wrapping; `free_ram32` is exact and wins when present.
  - `tfw_hal_free_ram()` returns `uint32_t`; the Go `SysInfo.FreeRAM` is `uint32`.
- Vectors re-pinned in `testdata/vectors.h` and `sdk/go/internal/vectors`:
  `TFWV_SYSINFO_WIDE_REPLY` and `TFWV_SYSINFO_BIG_REPLY` added (`NEW` and `OLD`
  unchanged, now prefixes of the firmware's reply). `TFWV_CAPS_PROTO_REPLY`
  (`02 00`) and `TFWV_CAPS_PIN13_REPLY` (`0d 81 00 05 ff 00`, the 328P's LED
  pin page) added, with Go mirrors `CapsProtoReply` and `CapsPin13Reply`;
  `scripts/verify-vectors.sh` checks they agree. No earlier vector carried the
  version, so none changed.

### Added

- **Arch-leak gate** (#13). `make lint-arch-leak-selftest` is part of
  `make ci`, so architecture-specific names cannot leak into shared code.
- **Codex code review on the CI server** (#24). A pull request labelled
  `code-review` is reviewed by Codex on the `codex` runner, which approves it
  or requests changes as the `codex` user, pinned to the head commit. No
  repository secret is involved. See `docs/development/code-review.md`.
- Board-id registry, `docs/boards/board-id-registry.md`, with per-family ranges
  (`0x01`-`0x3F` AVR, `0x40`-`0x7F` Cortex-M, `0x80`-`0xBF` ESP32/other), and
  the Go `tfw.BoardID` type with `String()` and `Family()`. `Caps.BoardID` is
  now that type; `tfwctl caps` prints the name beside the raw id.

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
