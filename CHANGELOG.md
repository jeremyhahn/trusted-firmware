# Changelog

All notable changes to this project are documented here.

The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-1.0, MINOR carries breaking changes.

## [Unreleased]

### Added

- **`SYS_INFO` reports the framer's `err_crc`/`err_format` counters**, appended
  after `reset_cause` so a host built before they existed keeps parsing the
  same seven bytes it always has (#5). `sdk/go`'s `SysInfo` gained `ErrCRC`
  and `ErrFormat`, `agctl sys` prints them, and a new literal wire vector
  (`AGV_SYSINFO_OLD_REPLY`/`AGV_SYSINFO_NEW_REPLY`, mirrored in
  `sdk/go/internal/vectors`) pins both reply shapes.
- **`test/bench/targets.h` gained two restated performance rows** (#11): the
  scheduler sweep per armed slot, and `DIGITAL_WRITE`'s device-only share
  excluding wire time. The device-only row is met; the per-slot row is not,
  but is now comparable across boards where the raw sweep total was not.

### Changed

- **The AVR firmware and bench images link a fast CRC-16 behind the HAL**
  (`src/hal/avr/hal_crc16.c`, avr-libc's `_crc_xmodem_update`), while the host,
  `agsim`, the bootloader and the cross-implementation suite keep the portable
  bit-serial form, now in its own file, `src/proto/crc16_update.c` (#12).
  Per-byte cost on the shipped build's measurement drops from ~103 to ~31
  cycles on the 328P (~105 to ~33 on the 2560); the golden check value
  (`0x29B1` for `"123456789"`) is unchanged and holds on both definitions.
- **The performance ceiling policy is ratified** (#11): a ceiling in
  `test/bench/targets.h` only ever moves down. CRC-16 and frame-decode's
  ceilings were lowered to match #12's measurement.

### Fixed

- **`src/hal/avr/hal_tick.c`'s tick ISR comment claimed ten cycles; it is 58 on
  the 328P and 59 on the 2560**, including the part's four-cycle interrupt
  response, and now says so along with what the cycles are spent on (#13).
- **`test/integration/simavr/harness.c`'s `run_cycles()` counted AVR
  instructions, not cycles** — 1.436 cycles per call on average for this
  firmware, so its "one simulated second" was actually about 1.44 (#14). It is
  now `run_instructions()` for generous settling and `run_until_cycle()` for
  the one assertion that needed an actual span of simulated time, and that
  assertion's bound is derived from the span rather than guessed. The same
  file's `send()` comment now explains, rather than merely stating, why the
  emulated link runs at half the real baud rate (simavr recomputes the USART
  divisor on `UBRRnL`/`H` and not on `U2Xn`, and the firmware writes the
  divisor first).
- **`sdk/go`'s `Device` sent its first frame into the bootloader's window on
  every connect to a stock, auto-resetting Arduino.** Closing a serial port
  drops DTR; the next open raises it and the board's own bootloader pulses
  RESET, which meant `agctl ping` against a freshly opened `/dev/ttyACM0`
  failed every single invocation, not just the first. `Device.New` now waits
  up to `DefaultBootWait` (~2 s, configurable with the new `WithBootWait`
  option and disabled entirely with `WithBootWait(0)`) for the firmware's own
  `AG_EVENT_BOOT`, a real readiness signal rather than a guessed settle time,
  and falls through unharmed if none arrives -- an adapter that does not reset
  the board pays the bound once and works exactly as before. The BOOT event
  New consumes at connect is not recorded in `Device.Events()`, so it is never
  misreported as a reset happening mid-session.

## [0.4.0-alpha] - 2026-09-23

### Added

- **`agctl` reaches every command on the wire.** It had twelve verbs against
  thirty-three `Device` methods: analog, the converter reference, PWM, tone,
  both pulse commands, both EEPROM commands, all three buses and both
  attestation commands were reachable only by writing a Go program. The rule
  they now satisfy is the project's own — a command that exists on the wire and
  is not reachable from `agctl` is a gap in `agctl`. The attestation nonce is
  the caller's and the tool does not generate one: a client that invents its
  own challenge is a client attesting to itself. The tests drive every verb
  against `agsim` rather than a fake and fail on "unknown command", which is
  what a verb losing its dispatch looks like while the SDK method underneath it
  keeps working.

### Changed

- **`docs/` is six five-file feature folders**, per the handbook's
  `feature-docs.md` §8, which names topic folders with no five-file set as
  unconverted rather than conformant. Content was moved and verified against
  code, not rewritten: every identifier was grepped before it was written,
  which is what surfaced the defects below.

### Fixed

- Twelve comments in source and build files cited documents the conversion
  retired. Each now names the section that holds the content.
- **`src/crypto/atecc608a/provider.c` claimed the CDI "is zeroized immediately
  after use"** — inside the section headed *"WHAT THIS TIER STILL DOES NOT MEET,
  stated because a conformance claim that hides its gaps is worse than no
  claim."* There is no `memset` of `cdi[]` anywhere; it is a file-scope static,
  resident for the lifetime of the image, because every derivation re-reads it.
  The bootloader does zeroize its own copy in `boot/boot_main.c` — a different
  buffer, which is how the claim survived.
- `boot/boot_verify.h` credited a retired document with the ed25519-donna size
  and said that document was corrected alongside the file.
- `agprov`'s `-config` help advertised YAML. The parser is deliberately not
  YAML for the reason its own doc comment gives: a YAML library is a dependency,
  and `sdk/go`'s dependency list is imported by go-xkms at Layer 3 and
  go-trusted-platform at Layer 5. The help text advertised the one change the
  constraint exists to prevent.
- Documentation named `templates/atecc608a-dice.yaml`, a file that does not
  exist, and invoked `sdk/go/bin/agctl`, a path nothing produces.

### Known gaps, filed

`ATTEST`'s `DeviceID` and `FWHash` are **zero in every build** — nothing writes
the device serial and the application never reads the bootloader's block (#6).
Seven command ids are allocated with no handler, two of them the authenticated
session (#4). `SYS_INFO` omits the framer error counters its own header says it
reports (#5). Two DICE conformance rows stated as met are false (#7). Five of
six performance targets are missed by 3x to 11x (#11, #12, #13).


### Added

- **`make bench-check`: a cycle-accurate performance gate, and five of the
  project plan's six targets are missed.** Every number is counted on simavr's
  cycle counter running a real `.elf`, never a host build. It is **run
  deliberately and gates nothing** (owner ruling, 2026-09-23): with five rows
  over budget a gate would be either permanently red or pinned at today's cost
  and green while the firmware is that far over, and a green `make ci` has to
  keep meaning what it has always meant. What
  it found: the tick ISR is 58 cycles against a target of 12 (`ticks` is a
  32-bit volatile, so `ticks++` compiles to twenty-seven instructions, and the
  comment above that ISR claiming "ten cycles" was never true); `ag_crc16_update`
  costs 102 cycles a byte against a target of 20, where avr-libc computes the
  same polynomial branch-free in fourteen instructions; frame decode is 120
  cycles a byte, of which all but 18 is that CRC; a sixteen-slot scheduler sweep
  is 2158 cycles against 200, and an *empty* one is already 242. The sustained
  command rate is met with margin, 673 cmd/s against 400, and the gate asserts
  what the plan's reasoning actually claimed — that the device is not the
  bottleneck. The 300 us round-trip target is below the link's own floor: fifteen
  frame bytes at 115200 is 1302 us before any firmware runs. Targets stay written
  down as the plan stated them; the ceilings the gate enforces are pinned at
  today's cost, so the debt is visible on every run and cannot grow. See
  `docs/development/performance.md`.
- **simavr clocks this firmware's USART at half rate unless corrected, and the
  benchmark harness corrects it.** The emulator recomputes cycles-per-byte when
  `UBRRnL` is written and not when `U2Xn` changes; `hal_uart.c` writes the
  divisor first and `UCSRnA` second, which is correct on silicon and captures
  `U2X0 = 0` in the model. Measured: 2992 cycles a byte where the programmed
  divisor gives 1496. `test/integration/simavr/harness.c` is affected too — its
  "~87 us at 115200" is ~187 us in that simulation — though nothing it asserts
  depends on the rate.

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
- **`make integration-test-boot`: `ag-boot` on emulated silicon, in `make ci`.**
  The real bootloader `.elf` at its link address with the program counter set
  there, the real application `.elf` in the flash below it with a trailer, a
  provisioned reserved EEPROM, and an **ATECC608A modelled as a TWI slave on
  simavr's bus** — so the element protocol arrives through
  `src/hal/avr/hal_bus.c`'s real TWI state machine rather than through the
  `ag_hal_i2c_xfer` seam `test/model/atecc608a.c` sits above. Being below the
  driver lets it assert what no status byte carries: that the bus was slow
  enough for the wake pulse, that a tampered image cost a full measurement and
  no `Verify`, and that a downgrade was refused before the element was asked to
  hash anything. Every wait is bounded in cycles, so a bootloader that stops
  answering fails the job rather than hanging it. It found all three of the
  device defects below.

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

- **The bootloader refused every image, on every supported configuration.**
  `ag_boot_evaluate` asked the signature backend for a public key so it could
  check the SHA-256 pinned in reserved EEPROM. The element backend correctly
  declines to produce one — the key lives in a locked slot and is named by
  number, which is the entire point of verifying in stored mode — so every boot
  ended in `AG_ERR_UNSUPPORTED` before the signature was ever checked. LTO then
  proved `ag_boot_verify` unreachable and **deleted the verify path from the
  image**, which is why `ag-boot` measured 2 368 bytes on the 328P and now
  measures 3 426. `ag_boot_evaluate` reads `AG_ERR_UNSUPPORTED` as the tier
  capability answer it is and passes no key, exactly as `ag_boot_floor_read`
  does on a tier that keeps no floor of its own. Both halves already had tests;
  nothing built the composition until something ran it.
- **`ag-boot` never reached the application on the ATmega2560.** Above 128 KB an
  indirect call is `eicall` and its target is `EIND:Z`; the C runtime leaves
  `EIND` pointing at the page `__vectors` is in, so `((void(*)(void))0)()` from
  a bootloader at `0x3E000` jumped to `0x20000`. That is erased flash, which
  decodes as a long run of `SBRS`, so the part walked the upper half of its
  flash and fell back into the bootloader — measuring, verifying and jumping
  again, forever, with nothing on the wire to say why.
  `ag_boot_jump_application` clears `EIND` first.
- **The element-tier bootloader never configured the TWI bus.** `TWBR` stayed at
  reset, which is 1 MHz at 16 MHz, so the wake pulse `ag_atecc_wake` produces
  was 9 us against the 60 us the part needs. The element would have read as
  absent on a board that had one, and every boot would have failed
  `AG_ERR_CRYPTO` with nothing to point at. `ag_boot_cdi_init` now calls
  `ag_hal_i2c_init(100000)`, as `src/crypto/atecc608a/provider.c` already did
  for the application.
- **`BOOT_SIG=atecc608a` hashed the signed span in software**, which linked
  `src/crypto/sha256.c` — 4 052 bytes — into a 4 096-byte boot section and was
  the reason the 328P row stopped linking once verification became reachable.
  It now uses `ag_boot_hash`, the build's one hash, so the element computes the
  48-byte digest as well. The cost argument in the old comment was about the
  image, which is tens of kilobytes; this is one `SHA Start` and one `SHA End`,
  once per boot.
- **`docs/attestation/dice-profiles.md` claimed ed25519-donna's verify path was
  "about 2 KB".** It is **72 930 bytes** on AVR, with a 2 950-byte stack frame,
  against a 4 096-byte boot section and 2 048 bytes of SRAM. The claim was wrong
  by a factor of thirty-six and is corrected. No substitute primitive has been
  chosen; `docs/security/bootloader.md` records the measurement and the options.

## [0.3.0-alpha] - 2026-09-23

### Added

- **UCCS evidence and a device-minted alias certificate.** The verifier's nonce
  travels in `tcg-dice-UCCS-evidence` — the mechanism TCG DICE r23 §6.1.4
  defines — rather than a repurposed field, so any conforming DICE verifier can
  find it. The device builds the whole TBSCertificate from a PROGMEM template
  and signs it; it does not sign bytes a host supplied, because a device that
  did could not know what it had attested to.
- **`Device.Attest` and `Device.AttestCert` in `sdk/go`**, which page internally.
  Without them a consumer could not reach either command without copying this
  project's wire format into itself.

### Fixed

- **`Device.ProvInfo` panicked on every legal reply** — it bounds-checked at 36
  bytes and then indexed 90 and 91 for lock bytes that live in config block 2,
  which the firmware never read. `provision.go` had no test; it does now.
- **`make ci` built one board while CI built six.** A change that overflowed the
  ATmega32U4 passed locally and went red on the server. `firmware-matrix` builds
  every board and tier.

### Changed

- **The `PROV_INFO` reply is 38 bytes, not 36** — the two lock bytes are
  explicit. An older 36-byte reply is refused rather than read past.

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
