# Changelog

All notable changes to this project are documented here.

The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-1.0, MINOR carries breaking changes.

## [Unreleased]

### Added

- **`ATTEST_CERT` (0x63): DICE evidence in the extension the spec defines for
  it.** The verifier's nonce travels in `tcg-dice-UCCS-evidence`
  (2.23.133.5.4.6, TCG DICE Attestation Architecture r23 §6.1.4) rather than in
  a repurposed `DiceTcbInfo.vendorInfo`, so a verifier that has never heard of
  this product can still read it. The device mints an alias certificate per
  challenge, builds the whole `TBSCertificate` itself from a PROGMEM DER
  template and signs it with the DeviceID key in the element — it never signs a
  digest a host supplied, because a signature over 32 opaque bytes is a
  signature over whatever the holder of those bytes says it is.
- **A CBOR encoder for one claims set and nothing else** (`src/caps/uccs.c`):
  four emit routines over one head primitive, carrying `nonce` (10), `ueid`
  (256) and `measurements` (273) with the firmware digest in a minimal RFC 9393
  evidence CoSWID. The output is core-deterministic CBOR (RFC 8949 §4.2.1) —
  definite lengths, smallest head, ascending keys — because §6.1.4 makes the
  issuer answerable for the extension's CDI contribution, and that only means
  something if one claims set has exactly one encoding.
- **`AG_ALIAS_NONCE_LEN` is 32 and is not `AG_ATTEST_NONCE_LEN`.** The enrolment
  challenge in `eat_nonce` is `SHA-256(keyAuthorization)`, because ACME
  `device-attest-01`'s raw key authorization is 87 bytes and RFC 9711 bounds
  `eat_nonce` at 8–64 — the raw form cannot be carried by a conforming claims
  set at all. The host hashes it; the device receives 32 bytes and places them
  verbatim. The evidence frame's 16-byte nonce is unchanged and is runtime
  freshness, not enrolment; the dispatcher refuses each command the other's
  width so the two cannot be confused on the wire.
- **`testdata/uccs_vectors.h`** — the claims set and the certificate as frozen
  literals, plus two deliberately malformed certificates. One of them is
  accepted by OpenSSL and by Python `cryptography`, because neither descends
  into an extension it does not recognise; only walking the extension's own DER
  finds it, and the suite does.

- **`ag-boot`, the trusted bootloader** — a second, separate image that measures
  the application with SHA-256, verifies a signature over the image trailer,
  refuses a version below the anti-rollback floor, derives the DICE layer-0 CDI
  and only then jumps. Its OTA path receives a signed image over the existing
  framed protocol, stages it in place and commits only after verification; an
  interrupted update leaves the device in the bootloader, reachable and honest
  about its state, because a part with 28 KB of application space has nowhere to
  keep a second slot. **No boot-loader-section callable is exposed to the
  application** — one trampoline would void the BLB mode 3 read barrier the
  software-tier UDS sits behind.
- **`make bootloader`, `boot-size-check`, `bootloader-budget`, `boot-core-size`,
  `boot-test`, `bootloader-reproducible`.** The size check is a gate on the boot
  section, exactly as `size-check` is on the application.
- **ed25519-donna vendored** and pinned by commit in
  `test/third_party/MANIFEST.md`, unmodified — the one declaration upstream
  expects an integrator to supply comes through `-include` rather than a patch,
  so the recorded hashes pin upstream's code and not ours.
- **81 host assertions for the bootloader**, including Ed25519 against RFC 8032
  §7.1 as literal hex, and a suite that proves a correctly signed image is
  accepted, a tampered one is refused and a downgrade is refused — against the
  real primitive, with a real key, over a real flash array.

### Changed

- **The ATmega32U4 does not carry the alias certificate.** The builder is 2 540
  bytes and an element-backed image there measures 14 706 against a 14 336
  budget — half of 28 672 usable bytes rather than half of 32 768, because
  4 096 of that part's flash are a DFU bootloader. `ATTEST` still works and its
  evidence still carries the measurement and the nonce; `CAPS` reports the
  feature bit clear so a host learns this before it depends on the answer.
- **The `.init3` early init is shared** between the application and the
  bootloader (`src/hal/avr/hal_early.c`). With `BOOTRST` programmed it is the
  first code after every watchdog bite, and two copies of "clear MCUSR and
  disarm the watchdog" would be two chances to brick a part.
- **`make lint` and `make coverage` now cover `boot/`** on the same terms as
  `src/` — logic in, register-poking out.

### Fixed

- **`docs/attestation/dice-profiles.md` claimed ed25519-donna's verify path was
  "about 2 KB".** It is **72 930 bytes** on AVR, with a 2 950-byte stack frame,
  against a 4 096-byte boot section and 2 048 bytes of SRAM. The claim was wrong
  by a factor of thirty-six and is corrected. No substitute primitive has been
  chosen; `docs/security/bootloader.md` records the measurement and the options.

## [0.2.0-alpha] - 2026-09-23

### Added

- **PWM, tone, pulse measurement, edge capture, I²C, SPI and 1-Wire** — the
  firmware now exposes the part's capabilities in full.
- **`PULSE_CAPTURE`** returns edge intervals, which is what makes timing-critical
  sensors work with no sensor code in the firmware. A DHT22 distinguishes a bit
  by a 26- versus 70-microsecond pulse; no protocol with a round trip per bit
  can read that. The same primitive reads DHT11/22, HC-SR04, IR and RC PPM.
- **The secure element's I²C address is refused** by the generic bus primitive,
  as is a reserved chip-select on SPI. Otherwise a host could reach the element
  around the firmware's crypto layer and the device's own root of trust would be
  an ordinary peripheral.

### Added

- **Two more boards: ATmega2560 and ATmega32U4.** Adding one is data — a header
  of constants, a table of pins, a profile and a test — and `make board-sweep`
  compiles each descriptor against its own board's header. Every register
  address is now a plain number that **avr-libc checks**: a `_Static_assert`
  compares each against `<avr/io.h>` on the firmware build, so a transposed digit
  is a compile error naming the register rather than a wrong pin on a bench.
- **The ATECC608A driver and the DICE-HW tier.** The element holds the UDS in a
  never-readable slot, computes `HMAC(UDS, H(firmware))` without it crossing the
  bus, and signs with a P-256 key that never leaves slot 0. Tested against a
  software model of the element, so it needs no part in hand; the one
  non-circular assertion is the CRC, anchored on the published wake token.
- **Provisioning commands, absent from every production image.**
  `AG_FEATURE_PROVISION` defaults to 0 and the gate is file selection, so a
  fielded device has no handler at all — not a disabled one. A device that can be
  re-provisioned over its own command channel has no identity worth attesting to.
  `agprov` drives them behind `--dry-run` / `--verify` / `--commit`.
- **`CAPS` answers per-pin pages** and reports an algorithm bitmap, so a host
  discovers a board rather than hardcoding one.
- **A simavr integration tier.** `make integration-test` runs the real `.elf` on
  an emulated ATmega328P, which covers what no host build can reach — the baud
  divisor, the interrupt vectors, and the tick ISR's actual rate.

### Fixed

- **Three 328P facts were written into code that claimed to be generic**, and
  each fails silently on another part: the UART's interrupt vector (a misspelled
  ISR is not a link error — it compiles to a function nothing calls and the
  device never answers), the tone pin's direction bit, and the SPI pins' direction
  bits. All three now come from the board.
- **The `CAPS` pin page could not report a port**, because it was built from a
  pin reference carrying a memory address rather than a port index, so a constant
  went out for every pin on every board. And its three-state PWM byte could never
  emit the third state — "this build spent that timer on the tick" — which is the
  one a host acts on.
- **CI raced itself.** `github.job` does not evaluate per job on Gitea, so every
  job computed the same Docker image tag; the expression written to prevent the
  collision caused it.

### Changed

- **`sdk/go` uses typed errors throughout**, replacing twenty `fmt.Errorf` sites,
  grouped by what a caller does next rather than by where they are raised.
- **The crypto tier is part of the build path.** The software and element images
  are different firmware with different measurements, and writing both to one
  path meant whichever built last silently became "the" image.

## [0.1.0-alpha] - 2026-09-22

### Added

- **The protocol core, the host SDK and the first firmware image.** CRC-16
  framing with resync, bounded payload cursors, command dispatch, a transport
  that never blocks, and the ATmega328P board descriptor. `sdk/go` is a Layer 0
  Go module with zero in-house dependencies, tested against `agsim` — the
  firmware compiled for the host — so the C and Go codecs are proven to agree
  with no hardware involved.
- **Gates the rest of the fleet does not have**: a per-board size budget that
  fails the build, and a reproducibility check that builds twice and diffs the
  image, because the firmware hash is signed evidence and a verifier needs a
  measurement computable from source.
- **GPIO and the pin scheduler.** `timer_set` drives a pin and the DEVICE
  reverts it — the one behaviour carried forward from the irrigation firmware,
  generalised from "channel index" to any pin and from "on then off" to "level
  then its complement". It stays in firmware deliberately: a host that crashes
  with a pump running must not leave the pump running.
- Pins the firmware is itself using are refused for writes. Driving the UART
  transmit pin would cut the line the command arrived on.
- **Analog input and EEPROM, with a protected region.** `analog_read` returns
  raw converter counts rather than a voltage or a percentage: calibration is a
  property of the sensor and its wiring, both of which the host knows and the
  device cannot.
- EEPROM `0x000`–`0x07F` is platform-owned and refuses writes. Without that
  refusal the generic EEPROM primitive would let anyone on the serial line
  overwrite the hash the bootloader verifies against.
- **A crypto provider and the attestation command.** SHA-256 and HMAC-SHA256,
  pinned to the published NIST and RFC 4231 vectors, and a 164-byte evidence
  frame bound to a verifier-supplied nonce.
- **`CAPS` reports the crypto tier**, so a verifier learns what root of trust it
  is talking to *before* it decides what any evidence is worth.
- An **unprovisioned device refuses to attest**. Evidence signed with a secret
  every build of this source shares would verify and prove nothing — the one
  failure a verifier cannot detect.
