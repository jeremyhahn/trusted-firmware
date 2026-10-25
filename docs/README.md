# Documentation

> **Status: INFORMATIVE** · Scope: `docs/` · Last verified: 2026-10-20
> The map of this repository's documentation: which folder holds what.

trusted-firmware is AVR firmware plus a Layer 0 Go SDK. It is a drop-in
replacement for basic Arduino firmware: every Arduino capability is on the wire
and in the SDK, with secure boot, a DICE device identity and go-trusted-platform
integration added. It is the framework and SDK for device applications, which live in their own `trusted-firmware-<application>` repositories; the firmware itself carries no application logic.

Where the project is going: [`../ROADMAP.md`](../ROADMAP.md) and
[`multi-arch/architecture.md`](multi-arch/architecture.md).

Each capability is a feature folder — `architecture.md`,
`engineering.md`, `api.md`, `user.md`, and `cli.md` where there is a command
line. **NORMATIVE** means a conflict
with the code is a defect to fix in the document first.

| Feature | What it covers |
|---|---|
| [`wire-protocol/`](wire-protocol/architecture.md) | the frame, resync, versioning, the command table and the golden vectors |
| [`device-capabilities/`](device-capabilities/architecture.md) | GPIO, the pin scheduler, analog/PWM/tone, pulse, EEPROM, the buses, and the link-time HAL behind them |
| [`dice-attestation/`](dice-attestation/architecture.md) | the crypto tiers, the CDI, the alias certificate and its UCCS evidence |
| [`secure-boot/`](secure-boot/architecture.md) | `tfw-boot`: measure, verify, anti-rollback, recovery and handoff |
| [`device-provisioning/`](device-provisioning/architecture.md) | taking a blank part to an identity-bearing device, and the irreversible steps |
| [`host-sdk/`](host-sdk/architecture.md) | `sdk/go`, `tfwctl`, `tfwprov` and `tfwsim` — one host implementation |
| [`xkms-adapter/`](xkms-adapter/architecture.md) | the AVR backend for go-xkms, registered through go-xkms's backend registry hook |
| [`application-sdk/`](application-sdk/architecture.md) | the C interface a `trusted-firmware-<application>` repository links into the image: descriptor, hooks, configuration, build |
| [`ethernet-transport/`](ethernet-transport/architecture.md) | the wire protocol over TCP through a WIZnet W5100 or W5500 (draft, #55); [`cli.md`](ethernet-transport/cli.md): `tfwctl netcfg` and the `tcp://` device address |
| [`ota/`](ota/architecture.md) | signed over-the-air updates: the image format, trial boot, confirm and rollback (#52) |
| [`hsm-self-defence/`](hsm-self-defence/README.md) | the i.MX 95 M7's tamper response under a tenant-signed policy: requirements, policy format, test plan (design only, #50) |

Cross-cutting material that belongs to no single feature:

| | |
|---|---|
| [`device-onboarding/architecture.md`](device-onboarding/architecture.md) | first power-on to enrolled: each step, its owning repository, what this repo provides at the seam, and the requirements on the others (draft, #53); with [`api.md`](device-onboarding/api.md), [`user.md`](device-onboarding/user.md) and [`engineering.md`](device-onboarding/engineering.md) |
| [`security/threat-model.md`](security/threat-model.md) | attacker classes, what each profile defends against, the named residual gaps |
| [`security/fuses-and-lockbits.md`](security/fuses-and-lockbits.md) | the fuse and lock-bit reference the provisioning procedure applies |
| [`attestation/cryptoauthlib-evaluation.md`](attestation/cryptoauthlib-evaluation.md) | why not Microchip's own library, with the licence clause and the measured size |
| [`boards/`](boards/adding-a-board.md) | what a board is, the [board-id registry](boards/board-id-registry.md), the dev-complete [`atmega2560.md`](boards/atmega2560.md) and [`atmega328p.md`](boards/atmega328p.md), and [`attiny85.md`](boards/attiny85.md), the part that broke five assumptions; profiles of the Cortex-M boards on the bench, with no port yet: [`stm32h573i-dk.md`](boards/stm32h573i-dk.md), [`rp2350-pico2w.md`](boards/rp2350-pico2w.md), [`nrf54l15-dk.md`](boards/nrf54l15-dk.md); the ESP32 row, built on ESP-IDF and not in `make ci`: [`esp32-qemu.md`](boards/esp32-qemu.md), the emulated test target, and [`esp32p4-poe.md`](boards/esp32p4-poe.md), the PoE board on the bench |
| [`development/`](development/building.md) | building, testing, coverage, flashing, reproducible builds, [releasing](development/releasing.md) (`make release-alpha`), [public releasability](development/releasability.md) |

## Where the hard decisions are written down

Not in a decision log — beside the thing they constrain, where someone changing
it will read them.

- **Why not Microchip's own ATECC608A library** — `attestation/cryptoauthlib-evaluation.md`,
  with the licence clause and the measured 12 475 bytes.
- **Why this is not "TCG DICE compliant"** — `dice-attestation/architecture.md`.
  Two hardware requirements are met only procedurally on a classic AVR, and two
  more that earlier documents claimed as met are open gaps.
- **Why the build must be reproducible** — `development/reproducible-builds.md`.
  `FWHash` is signed evidence, so a verifier must be able to compute the expected
  hash from source.
- **Why provisioning commands are absent from production images** —
  `device-provisioning/architecture.md`. A device that can be re-provisioned over
  its own command channel has no identity worth attesting to.
- **Why the nonce travels in `tcg-dice-UCCS-evidence`** — `dice-attestation/architecture.md`.
  The spec's own mechanism rather than a repurposed `vendorInfo`, and the one
  sentence of §6.1.4 this device cannot satisfy, said plainly.
- **How the DeviceID key proves possession at ACME finalize** —
  [`dice-attestation/adr-possession-at-acme-finalize.md`](dice-attestation/adr-possession-at-acme-finalize.md).
  **ACCEPTED** (issue #6): a device-templated PKCS#10,
  no signing oracle, and what the 32U4 cannot afford.
- **Which Cortex-M part comes first, and what CI emulates** —
  [`cortex-m/adr-reference-part-and-emulation.md`](cortex-m/adr-reference-part-and-emulation.md).
  **PROPOSED** (issue #14): the i.MX 95 Cortex-M7 and QEMU `mps2-an500`.
- **Whose secure world runs on Cortex-M, ours or TF-M's** —
  [`cortex-m/adr-trust-model-vs-tf-m.md`](cortex-m/adr-trust-model-vs-tf-m.md).
  **PROPOSED** (issue #15): TF-M with our attestation partition on
  the M33, MPU-only isolation on the M7, and the owner's open questions.
- **How an OTA image is signed, verified and rolled back** —
  [`ota/adr-ota-image-format-and-trust.md`](ota/adr-ota-image-format-and-trust.md).
  **ACCEPTED** for the decisions it marks so, **PROPOSED** for the rest
  (issue #52): the trailer per arch class, the floor per tier,
  A/B where a part has room, and what AVR gets.
- **How a tamper-response policy is signed and checked on the i.MX 95 M7** —
  [`hsm-self-defence/adr-response-policy-format.md`](hsm-self-defence/adr-response-policy-format.md).
  **PROPOSED** (issue #50): SUIT envelopes with the payload integrated, the
  tenant's ES256 key in the M7 image, and the owner's open questions.
- **Why there is no `delay()`** — `device-capabilities/architecture.md`.
- **Why the pin timer stays in firmware** — `device-capabilities/engineering.md`.
  A pin told to hold a level for ten minutes must revert in ten minutes whether
  or not the host is still there.
