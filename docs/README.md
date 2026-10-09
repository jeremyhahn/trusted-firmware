# Documentation

trusted-firmware is AVR firmware plus a Layer 0 Go SDK. It is a drop-in
replacement for basic Arduino firmware: every Arduino capability is on the wire
and in the SDK, with secure boot, a DICE device identity and go-trusted-platform
integration added. It carries no application logic.

Where the project is going: [`../ROADMAP.md`](../ROADMAP.md) and
[`multi-arch/architecture.md`](multi-arch/architecture.md).

Six capabilities, each a five-file feature folder — `architecture.md`,
`engineering.md`, `cli.md`, `api.md`, `user.md`. **NORMATIVE** means a conflict
with the code is a defect to fix in the document first.

| Feature | What it covers |
|---|---|
| [`wire-protocol/`](wire-protocol/architecture.md) | the frame, resync, versioning, the command table and the golden vectors |
| [`device-capabilities/`](device-capabilities/architecture.md) | GPIO, the pin scheduler, analog/PWM/tone, pulse, EEPROM, the buses, and the link-time HAL behind them |
| [`dice-attestation/`](dice-attestation/architecture.md) | the crypto tiers, the CDI, the alias certificate and its UCCS evidence |
| [`secure-boot/`](secure-boot/architecture.md) | `tfw-boot`: measure, verify, anti-rollback, recovery and handoff |
| [`device-provisioning/`](device-provisioning/architecture.md) | taking a blank part to an identity-bearing device, and the irreversible steps |
| [`host-sdk/`](host-sdk/architecture.md) | `sdk/go`, `tfwctl`, `tfwprov` and `tfwsim` — one host implementation |

Cross-cutting material that belongs to no single feature:

| | |
|---|---|
| [`security/threat-model.md`](security/threat-model.md) | attacker classes, what each profile defends against, the named residual gaps |
| [`security/fuses-and-lockbits.md`](security/fuses-and-lockbits.md) | the fuse and lock-bit reference the provisioning procedure applies |
| [`attestation/cryptoauthlib-evaluation.md`](attestation/cryptoauthlib-evaluation.md) | why not Microchip's own library, with the licence clause and the measured size |
| [`boards/`](boards/adding-a-board.md) | what a board is, the [board-id registry](boards/board-id-registry.md), and [`attiny85.md`](boards/attiny85.md), the part that broke five assumptions |
| [`development/`](development/building.md) | building, testing, coverage, flashing, reproducible builds |

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
- **Why there is no `delay()`** — `device-capabilities/architecture.md`.
- **Why the pin timer stays in firmware** — `device-capabilities/engineering.md`.
  A pin told to hold a level for ten minutes must revert in ten minutes whether
  or not the host is still there.
