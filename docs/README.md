# Documentation

Each folder documents one thing and states its own scope and status. **NORMATIVE**
means a conflict with the code is a defect to fix in the document first.

| | |
|---|---|
| [`protocol/`](protocol/architecture.md) | the frame, resync, versioning, the CAPS pin page and the algorithm bitmap |
| [`firmware/`](firmware/architecture.md) | the layers, link-time substitution, the tick and scheduler, the AVR traps |
| [`boards/`](boards/adding-a-board.md) | what a board is, and the three times "data only" stopped being true |
| [`attestation/`](attestation/dice-profiles.md) | the three DICE profiles, conformance met and unmet, the UCCS evidence extension and the CryptoAuthLib decision |
| [`security/`](security/threat-model.md) | the threat model, provisioning, fuses and lock bits |
| [`clients/`](clients/README.md) | one host implementation, and why the codec exists exactly twice |
| [`development/`](development/building.md) | building, testing, coverage, flashing, reproducible builds |
| [`migration/`](migration/from-cropdroid-irrigation.md) | from the CropDroid irrigation firmware |

## Where the hard decisions are written down

Not in a decision log — beside the thing they constrain, where someone changing
it will read them.

- **Why not Microchip's own ATECC608A library** — `attestation/cryptoauthlib-evaluation.md`,
  with the licence clause and the measured 12 475 bytes.
- **Why this is not "TCG DICE compliant"** — `attestation/dice-profiles.md`. Two
  hardware requirements are met only procedurally on a classic AVR.
- **Why the build must be reproducible** — `development/reproducible-builds.md`.
  `FWHash` is signed evidence, so a verifier must be able to compute the expected
  hash from source.
- **Why provisioning commands are absent from production images** —
  `security/provisioning.md`. A device that can be re-provisioned over its own
  command channel has no identity worth attesting to.
- **Why the nonce travels in `tcg-dice-UCCS-evidence`** — `attestation/uccs-evidence.md`.
  The spec's own mechanism rather than a repurposed `vendorInfo`, and the one
  sentence of §6.1.4 this device cannot satisfy, said plainly.
- **Why the auto-off timer stayed in firmware** — `migration/from-cropdroid-irrigation.md`.
  A valve told to run ten minutes must close in ten minutes whether or not the
  host is still there.
