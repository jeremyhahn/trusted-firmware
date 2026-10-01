# Changelog

All notable changes to this project are documented here.

The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-1.0, MINOR carries breaking changes.

## [Unreleased]

### Added

- **`RESET` mode 1 now opens an OTA session in `ag-boot` on a healthy device**
  (#28). The mode byte used to be stored and never read, so both modes took the
  same watchdog reset and a working device could not be updated over serial:
  `ag-boot` only entered its OTA loop when there was nothing bootable. Mode 1
  leaves a one-shot request (a 32-bit magic and its complement) in the handoff
  SRAM region, which both images already place identically; `ag-boot` reads and
  clears it before anything else, honours it only after a pure watchdog reset,
  and runs the existing recovery loop bounded by an idle timeout
  (`AG_BOOT_SESSION_IDLE_SECONDS`, 10 s, counted in loop iterations since
  `ag-boot` has no timer, and independently in received bytes, derived from the
  baud rate, so a peer streaming junk with no idle gap is bounded too). Accepted
  `OTA_BEGIN`/`OTA_DATA`/`OTA_END` restart those two counts, but an accepted frame
  is not authenticated, so the session also has a **ceiling nothing restarts** --
  60 s of loop iterations (`AG_BOOT_SESSION_MAX_SECONDS`) and a byte total of the
  application size plus framing and slack (`AG_BOOT_SESSION_MAX_BYTES`) -- and
  **one attempt**: the first `OTA_END` that does not verify ends it. A full
  56 KB ATmega2560 update at 115200 baud needs about 9 s and 66 KB of the
  60 s / 83 KB. Any of these falls through to the normal measure, verify and
  jump; if the failed attempt had already overwritten pages, that lands in
  recovery, which stays unbounded (single-slot residual risk). The entry `EVENT` is status `AG_OK` with no payload. Mode 1 needs the
  operator auth session on the element tier (`AG_ERR_AUTH_REQUIRED`); mode 0 is
  never gated. A board with no boot section (ATtiny85) answers mode 1 with
  `AG_ERR_UNSUPPORTED` and does not reset; a `BOOT_OTA=0` bootloader ignores the
  request. The simavr bootloader suite now drives RESET(1), PING, the timeout,
  RESET-in-session, one-shot clearing and a power-on request being ignored.
  No wire change: no new command, status or event id.

## [0.5.0-alpha] - 2026-10-01

### BREAKING: devices must be re-baked, and ag-boot and the application upgraded together

- **The platform-reserved EEPROM block is layout v2** (`EEPROMReservedEnd`
  `0x80` → `0xC0`, to hold the operator public key). A v1 block parses as
  *unprovisioned*, not partially, so an element-tier device reports no serial
  until `agprov --bake-eeprom` is re-run and flashed **with an ISP programmer**.
- **The CDI handoff is layout v2** (it now carries `MCUSR`). ag-boot and the
  application must come from the same release; a mismatched pair reads as "no
  valid handoff" and falls back as the Fixed section below describes.
- `sdk/go` has no source-level break. go-xkms (`pkg/backend/avr`, `sdk/go`) and
  go-trusted-platform (`pkg/firmware/avr/attestation`) were built, vetted and
  tested against this tree before tagging.

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
- **`I2C_CONFIG` (`0x55`) and `SPI_CONFIG` (`0x56`)** let a host choose the
  generic bus's clock (I2C) or mode/clock-divider/bit-order (SPI), which
  nothing on the wire previously exposed at all. `Device.SetI2CClock` and
  `Device.ConfigureSPI` in the Go SDK, `i2c-config`/`spi-config` verbs in
  `agctl`, and new frozen wire vectors in `testdata/vectors.h` /
  `sdk/go/internal/vectors/vectors.go` pinning both commands' payload layout.
- `main.c` now calls `ag_hal_adc_init()` and the new `ag_bus_init()`
  (`src/caps/bus.c`) at boot, applying `AG_I2C_DEFAULT_HZ` (100 kHz) and
  `AG_SPI_DEFAULT_MODE`/`_CLKDIV`/`_LSB_FIRST` (mode 0, slowest, MSB-first).
  Every board that has none of I2C/SPI/1-Wire links `src/caps/bus_none.c`'s
  no-op stub instead, so `main.c` calls the same symbol unconditionally with
  no `#if` — the same file-selection shape `CRYPTO_SRCS` already used for
  `ag_crypto_init()`.
- `test/integration/simavr/harness.c` now exercises `ANALOG_READ` and
  `SPI_XFER` against the emulated ADC and SPI peripherals, which model
  `ADEN`/`SPE` where the host mock cannot. Every exchange is bounded by a
  fixed cycle count, so a regression that reintroduces the hang below fails
  the suite instead of blocking it.
- **The operator-signature auth session — `AUTH_CHALLENGE`/`AUTH_RESPONSE`
  (0x61/0x62), element tier only.** A host proves it holds an operator
  private key by signing `domain||device_serial||nonce` (P-256) against the
  operator public key `agprov --bake-eeprom --operator-pubkey` now bakes into
  the platform-reserved EEPROM block; the device verifies it inside the
  ATECC608A (`ag_atecc_verify_external`, a new Verify-External-mode command
  staged on the caller's stack rather than growing the shared element
  command buffer for every other caller). A granted session gates one
  resource: `EEPROM_READ` below `EEPROMReservedEnd`, `AG_ERR_AUTH_REQUIRED`
  otherwise, and lasts until the device resets. `RESET(1)` is deliberately
  NOT gated: mode 1 is not yet a distinct bootloader entry (it takes the same
  watchdog reset as mode 0), so a gate would protect nothing. Issue #28 makes
  it a real path and adds the gate there. The obvious software-tier primitive, ed25519-donna, does
  not fit this device family — its verify path alone overflows an
  ATmega328P's entire flash and is 31–47% over this project's own
  application-flash budget on an ATmega2560 — so `AUTH_CHALLENGE`/
  `AUTH_RESPONSE` are not compiled into the soft or `CRYPTO=none` tier at
  all, and `EEPROM_READ` is consequently ungated there, exactly as
  before. `WDT_SET`, `SLEEP` and `PULSE_COUNT_*` — the remaining allocated
  ids with no handler in any build — are now marked "reserved, not
  implemented" in `include/ag/protocol.h` rather than left silent. SDK:
  `Device.AuthChallenge`/`AuthResponse` (the latter taking a
  `crypto.Signer`, never a key — this repo holds no key material); `agctl
  auth`. Closes #4.
- **`agprov --bake-eeprom`** writes the platform-reserved EEPROM block —
  the device serial, among other fields — as a partial Intel HEX `.eep`
  image, flashed before any production firmware. There is deliberately no
  wire command that writes this region; `src/caps/storage.c`'s refusal of it
  stays absolute. Refuses an all-zero `--serial` outright, the reserved
  unprovisioned sentinel, rather than baking in a block a device with no
  platform block at all would also report; propagates the output file's
  `Close()` error instead of a deferred, error-discarding one, so a write
  that fails to reach disk is never reported as success. Closes #6.
- **Documented a real hardware failure mode for `--bake-eeprom`'s output.**
  On a Mega 2560 with its factory STK500v2 bootloader, `avrdude -c wiring
  -U eeprom:w:platform.eep:i` was observed to corrupt the write in transit
  — a prefix of the bytes landed, the rest was silently dropped, and
  avrdude itself reported the verification mismatch, against an `.eep` file
  independently confirmed correct offline. The firmware's response to the
  resulting bad-CRC block — "no provisioned identity" rather than a forged
  one — is correct, but `docs/device-provisioning/{user,engineering}.md`
  and `agprov --bake-eeprom`'s own output previously did not say this write
  needs an ISP programmer, not the stock serial bootloader; they do now.
  `ag-boot` has no EEPROM-write command, so it is not an alternative.
- **The application reads the DICE CDI handoff and the platform-reserved
  EEPROM block.** `ATTEST`'s `DeviceID` and `FWHash` fields, zero in every
  build until now, report the serial `agprov --bake-eeprom` baked in and the
  measurement `ag-boot` actually verified — through new read-only accessors
  (`src/core/platform_id.c`, `src/core/handoff.c`) linked into the
  application for boards with a boot section; a board without one (the
  ATtiny85) links a zero-cost stub instead. Verified end to end under
  simavr: EEPROM through the accessor through the wire. Closes #6.
- **The reset cause survives a bootloader that consumed `MCUSR` first.**
  `ag-boot` now saves the `MCUSR` it observed into the CDI handoff (version
  2 of that layout — see `include/ag/boot.h`), and
  `ag_hal_reset_cause()` reports it when a valid handoff exists, falling back
  to the application's own `.init3` observation otherwise — `AG_RESET_UNKNOWN`
  under a stock Arduino bootloader, which clears `MCUSR` before either
  bootloader's own `.init3` can. Closes #18.

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
- **`EEPROMReservedEnd` grows from `0x80` to `0xC0`** to hold the operator
  public key above (issue #4), and the platform-reserved EEPROM block's
  layout version bumps to 2 accordingly — a v1 region parses as
  unprovisioned rather than partially, so an element-tier build reads a
  zero serial against it. `BuildPlatformBlock`/`agprov --bake-eeprom` has no
  read-modify-write path, so re-baking is safe only for a part that has
  never been fielded: it always writes `rollback_floor`/`boot_count`/
  `boot_fail_count` as zero, which resets the anti-rollback floor on a
  device that has already advanced it. A device already in service needs a
  migration path that preserves those fields, which does not exist yet —
  known gap, not a deliberate omission.
  `EEPROM_READ` of the reserved region, previously unconditional
  on every tier ("its contents are public"), now additionally requires the
  auth session above on a tier that can enforce one; unconditional
  everywhere else. `EEPROM_WRITE`'s refusal of the region is unchanged and
  unconditional on every tier.

### Fixed

- **Four statements implied a per-boot, CDI-derived Alias key that does not
  exist** (#7). `docs/security/threat-model.md` said the design "derives
  per-boot keys that are zeroized"; `sdk/go`'s `WithTimeout` and
  `TimeoutError`, and `docs/host-sdk/engineering.md`, justified their deadlines with "a device deriving an Alias
  key in software". The element tier signs with its element-resident DeviceID
  key and zeroizes its CDI buffer after derivation (#25); the soft tier keeps
  its CDI resident because it is the HMAC key for every signature. All four
  now say that. No behaviour or timeout value changed.
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
- **The boot-event wait above had its own hole: a BOOT event left behind by
  the PREVIOUS session could be mistaken for the current one.** Closing a
  port does not stop the device from transmitting -- a `reset soft` followed
  by closing the port (as one `agctl` invocation does) leaves that BOOT event
  sitting in the OS's receive buffer, and the next open hands it straight
  back, measured 0.3 ms after `open()`, long before this open's own DTR reset
  could have produced a new one (never observed earlier than ~100 ms, usually
  ~906 ms). `Device` accepted whichever arrived first and sent its next
  request into the bootloader's window regardless -- the exact bug refs #17
  exists to prevent, reintroduced through the one path that closes and
  reopens the port between commands. Fixed by purging the receive buffer
  (`ResetInputBuffer`, `TCIFLUSH`) rather than by a discard window: both
  `serialport.Open` and `Device` (independently, so a conn that did not come
  from `serialport.Open` is still covered) ask to purge whatever is already
  buffered before trusting the stream. A time-based discard window was tried
  first and reverted -- no fixed size is both long enough to reliably catch
  backlog and short enough to never discard a GENUINE event, because how
  fast a genuine one can arrive is a property of the board: the ATtiny85 has
  no bootloader at all and its own BOOT event is fully on the wire within
  single-digit milliseconds of its reset. A purge has no such window; it
  cannot discard a byte that has not arrived yet, board speed notwithstanding.
  Refs #17.
- **No peripheral the HAL exports an init for was ever initialised.**
  `ADCSRA`'s `ADEN` was never set, so `ANALOG_READ` returned a
  plausible-looking zero on every board — a stuck-at-zero sensor reading as a
  valid measurement is worse than a visible failure. `SPCR`'s `SPE` was never
  set either, and `src/hal/avr/hal_bus.c`'s SPI wait was unbounded: the first
  `SPI_XFER` a host sent hung the device permanently, taking every later
  command down with it, including the scheduler's own sweep. Found on real
  hardware (an ATmega2560 with an Ethernet shield). Fixed by initialising
  every compiled-in peripheral at boot (above) and bounding the SPI wait the
  same way the I2C path already was, with `AG_ERR_TIMEOUT` on the failure
  path instead of a hang. Closes #21, closes #19, closes #16.
- **The new boot-time SPI init drove the hardware SS pin low for an instant
  before the caller's first transfer.** `ag_hal_spi_init()` set `SS`'s `DDRB`
  bit to output before its `PORTB` bit was ever driven high, so a real device
  wired to that pin as its chip-select was selected the moment boot-time init
  ran — with no transfer under way and nothing a host could have done about
  it. Fixed by asserting the `PORTB` bit high first, so the pin is never
  observed low before a caller actually wants it to be.
- **`I2C_CONFIG` could silently break the secure element.** `ag_atecc_wake()`
  assumed the TWI bus was still at the 100 kHz its own comment says the wake
  pulse depends on, but nothing re-asserted that after boot — a host calling
  `SetI2CClock` to, say, 400 kHz for a sensor left every later element
  operation (attestation included) failing until reboot, with the symptom
  reading as "no element" rather than "wrong clock". Fixed two ways: an
  element build now refuses `I2C_CONFIG` outright (`AG_ERR_PROTECTED_REGION`)
  — the same shape the element's address is already refused from `I2C_XFER`
  in, and for the same reason: a rate accepted here would be silently undone
  by the next crypto operation regardless — and `ag_atecc_wake()` reasserts
  `AG_ATECC_I2C_HZ` itself before every wake as a second, independent
  safeguard rather than trusting either the generic command or its own
  refusal alone.
- **`I2C_CONFIG` accepted clocks the HAL could not actually produce, and
  clocks the silicon is not rated for.** Below roughly 30 kHz on this
  family's 16 MHz boards, `TWBR`'s divisor overflows a byte; the truncated
  value silently ran the bus at approximately 125 kHz instead of the
  requested rate, with no error. Separately, the accepted range's ceiling was
  1 MHz (Fast-mode-plus) — reachable by the divisor math, but past what this
  family's TWI peripherals are actually specified for (Fast-mode, 400 kHz).
  Fixed by having `ag_hal_i2c_init()` check its own divisor against `F_CPU`
  and return `AG_ERR_RANGE` for anything it cannot represent, and by lowering
  `AG_I2C_HZ_MAX` (and the SDK's `I2CClockMax`) to 400 kHz.
- **`I2C_CONFIG` could program a bus FASTER than the rate requested.** A rate
  that does not divide `F_CPU` evenly — 350 kHz at 16 MHz, say — used to
  compute `TWBR` by dividing twice and truncating both times, which rounds
  the *divisor* down and therefore the *clock* up: 350 kHz became roughly
  363.6 kHz, past what a peripheral rated for exactly 350 kHz is specified to
  tolerate, with the command still answering `AG_OK`. Fixed by computing the
  ceiling of the exact divisor by cross-multiplying instead of dividing
  first, so the real clock never exceeds the one asked for.
- **The element-tier crypto provider now zeroizes its CDI probe.** Nothing
  on that tier reads the value after learning whether the element holds a
  UDS — the DeviceID key is element-resident, never CDI-derived — so it is
  now a stack buffer cleared before `ag_crypto_init` returns, making the
  file's own conformance comment (`src/crypto/atecc608a/provider.c`) true
  rather than aspirational. Related to #7.
- **The software-tier crypto provider prefers the handed-off CDI** —
  `HMAC(UDS, the measurement ag-boot verified)` — over its own
  placeholder-measurement derivation, falling back to the latter only when
  no valid handoff exists at all, **and only when the handoff names the
  software tier.** A CDI ag-boot derived from the element's UDS is accepted
  by neither: this tier has no way to reproduce a secret from a UDS it
  never provisions anywhere it reads from, so a verifier could never
  recompute a signature made under it. A mismatched-tier CDI is drained and
  discarded the same as one this tier simply has no use for, and **a
  right-tier handoff carrying ag-boot's own all-zero "unprovisioned"
  sentinel CDI refuses the same way** — neither case falls through to the
  local fallback, which two rounds of review each caught reaching from a
  different direction: a device with a mismatched-tier handoff, and
  separately a device with a right-tier handoff whose CDI is the sentinel,
  each combined with a real local UDS, used to sign successfully with an
  identity unrelated to the real measurement FWHash reported from the same
  handoff. Only the genuine absence of any valid handoff reaches the local
  UDS-plus-placeholder derivation now. The soft tier still cannot zeroize a
  CDI it does accept; it is the HMAC key for every operation and the code
  says so. Related to #7.
- **Every CDI-copy site clears its buffer through one non-elidable wipe**
  (`ag_secure_zero()`, `src/core/secure_zero.c`) instead of a per-call-site
  `memset`, after confirming on this codebase's own `-Os -flto` build that a
  `memset` on a buffer nothing reads afterward is exactly the dead store an
  optimizer is free to remove — verified by disassembling the compiled AVR
  object, not by reading the source. Applies to `src/core/handoff.c`,
  `boot/boot_main.c`, and every crypto provider's discard/consume path.
- **The element-tier and unattested-tier crypto providers drain any
  handed-off CDI too**, even though neither ever signs with one — left
  resident otherwise, for the process lifetime, which is the "consumed
  once" contract the handoff is built around not actually being honoured.
  `CRYPTO=none` is a valid build for any board with a boot section, not
  only the ATtiny85's own stubbed-out case, so it needed the same drain the
  other two tiers now have. Related to #7.
- **`ag_attest_device_id()`/`ag_attest_fw_hash()` read their sources
  directly** rather than a copy inside `ATTEST`'s own evidence buffer, which
  only its `build()` populates. A host sending `ATTEST_CERT` before `ATTEST`
  had ever run previously got a certificate signed over an all-zero UEID and
  measurement on a fully provisioned device, with nothing about the
  exchange saying so. Related to #6.
- **The CDI handoff's `.noinit`-style storage moved to its own `.handoff`
  section**, in both images, after two independent bugs surfaced together:
  (1) `ag_hal_early_flags` — the reset-cause latch `.init3` writes — was
  ordinary (`.bss`) storage, so the C runtime's own `.bss`-clear zeroed it a
  few instructions after `.init3` set it, on every boot, in both images;
  every reset cause this firmware had ever reported was the one value that
  survives that clear: none. Fixed by moving it to `.noinit`, which then
  collided with the handoff block's own pinned-address `.noinit` reuse — an
  ad hoc section name has no `NOLOAD` rule in the default linker script, so
  its 80 bytes were also riding into the FLASH `.hex` image at their SRAM
  address. (2) Stripping it per `avr-objcopy` invocation (`-R .handoff`,
  alongside the existing `-R .eeprom`) covered the app and bootloader
  `.hex` targets but missed `mk/release.mk`'s provenance `.bin` conversion
  entirely, so a release image's `image_len`/`fwhash_sha256` would not have
  matched a device's own attestation. Fixed at the source instead, in one
  place: `mk/handoff.ld` makes `.handoff` genuinely `NOLOAD`, so every
  derived image excludes it the way `.bss` is excluded, with no
  per-invocation flag left to miss; `provenance-one` also now bounds-checks
  the resulting `.bin` against the part's flash size so a recurrence fails
  the build instead of shipping a bad manifest.

### Known gaps, filed

`RESET(1)` is not a distinct path, so **a device running a valid image cannot
reach ag-boot's OTA session** from the serial line. Update works only after
recovery has been entered for another reason (#28). The operator auth session
gates `EEPROM_READ` of the reserved region only where it is compiled in: never
on the soft or `CRYPTO=none` tiers, and not on the ATmega328P element tier,
where it is 292 bytes over the part's flash (`boards/atmega328p/board_profile.h`).
Release readiness for a public release is tracked in #20.

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
