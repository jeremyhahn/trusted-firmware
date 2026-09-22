# Changelog

All notable changes to this project are documented here.

The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-1.0, MINOR carries breaking changes.

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
